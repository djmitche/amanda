/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991-1998 University of Maryland at College Park
 * All Rights Reserved.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of U.M. not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  U.M. makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * U.M. DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL U.M.
 * BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Author: James da Silva, Systems Design and Analysis Group
 *			   Computer Science Department
 *			   University of Maryland at College Park
 */
/*
 * $Id: infofile.c,v 1.44.4.4.8.3 2005/07/11 12:00:41 martinea Exp $
 *
 * manage current info file
 */
#include "amanda.h"
#include "conffile.h"
#include "infofile.h"
#include "token.h"

#ifdef TEXTDB
  static char *infodir = (char *)0;
  static char *infofile = (char *)0;
  static char *newinfofile;
  static int writing;
#else
#  define MAX_KEY 256
/*#  define HEADER	(sizeof(info_t)-DUMP_LEVELS*sizeof(stats_t))*/

  static DBM *infodb = NULL;
  static lockfd = -1;
#endif

#ifdef TEXTDB

FILE *open_txinfofile(host, disk, mode)
char *host;
char *disk;
char *mode;
{
    FILE *infof;

    assert(infofile == (char *)0);

    writing = (*mode == 'w');

    host = sanitise_filename(host);
    disk = sanitise_filename(disk);

    infofile = vstralloc(infodir,
			 "/", host,
			 "/", disk,
			 "/info",
			 NULL);

    amfree(host);
    amfree(disk);

    /* create the directory structure if in write mode */
    if (writing) {
        if (mkpdir(infofile, 02755, (uid_t)-1, (gid_t)-1) == -1) {
	    amfree(infofile);
	    return NULL;
	}
    }

    newinfofile = stralloc2(infofile, ".new");

    if(writing) {
	infof = fopen(newinfofile, mode);
	if(infof != NULL)
	    amflock(fileno(infof), "info");
    }
    else {
	infof = fopen(infofile, mode);
	/* no need to lock readers */
    }

    if(infof == (FILE *)0) {
	amfree(infofile);
	amfree(newinfofile);
	return NULL;
    }

    return infof;
}

int close_txinfofile(infof)
FILE *infof;
{
    int rc = 0;

    assert(infofile != (char *)0);

    if(writing) {
	rc = rename(newinfofile, infofile);

	amfunlock(fileno(infof), "info");
    }

    amfree(infofile);
    amfree(newinfofile);

    rc = rc || fclose(infof);
    infof = NULL;
    if (rc) rc = -1;

    return rc;
}

int read_txinfofile(infof, info) /* XXX - code assumes AVG_COUNT == 3 */
FILE *infof;
info_t *info;
{
    char *line = NULL;
    int version;
    int rc;
    perf_t *pp;
    char *s;
    int ch;
    int nb_history;
    int i;

    /* get version: command: lines */

    if((line = agets(infof)) == NULL) return -1;
    rc = sscanf(line, "version: %d", &version);
    amfree(line);
    if(rc != 1) return -2;

    if((line = agets(infof)) == NULL) return -1;
    rc = sscanf(line, "command: %d", &info->command);
    amfree(line);
    if(rc != 1) return -2;

    /* get rate: and comp: lines for full dumps */

    pp = &info->full;

    if((line = agets(infof)) == NULL) return -1;
    rc = sscanf(line, "full-rate: %f %f %f",
		&pp->rate[0], &pp->rate[1], &pp->rate[2]);
    amfree(line);
    if(rc > 3) return -2;

    if((line = agets(infof)) == NULL) return -1;
    rc = sscanf(line, "full-comp: %f %f %f",
		&pp->comp[0], &pp->comp[1], &pp->comp[2]);
    amfree(line);
    if(rc > 3) return -2;

    /* get rate: and comp: lines for incr dumps */

    pp = &info->incr;

    if((line = agets(infof)) == NULL) return -1;
    rc = sscanf(line, "incr-rate: %f %f %f",
		&pp->rate[0], &pp->rate[1], &pp->rate[2]);
    amfree(line);
    if(rc > 3) return -2;

    if((line = agets(infof)) == NULL) return -1;
    rc = sscanf(line, "incr-comp: %f %f %f",
		&pp->comp[0], &pp->comp[1], &pp->comp[2]);
    amfree(line);
    if(rc > 3) return -2;

    /* get stats for dump levels */

    for(rc = -2; (line = agets(infof)) != NULL; free(line)) {
	stats_t onestat;	/* one stat record */
	long date;
	int level;

	if(line[0] == '/' && line[1] == '/') {
	    rc = 0;
	    amfree(line);
	    return 0;				/* normal end of record */
	}
	else if (strncmp(line,"last_level:",11) == 0) {
	    break;				/* normal */
	}
	else if (strncmp(line,"history:",8) == 0) {
	    break;				/* normal */
	}
	memset(&onestat, 0, sizeof(onestat));

	s = line;
	ch = *s++;

#define sc "stats:"
	if(strncmp(line, sc, sizeof(sc)-1) != 0) {
	    break;
	}
	s += sizeof(sc)-1;
	ch = s[-1];
#undef sc

	skip_whitespace(s, ch);
	if(ch == '\0' || sscanf((s - 1), "%d", &level) != 1) {
	    break;
	}
	skip_integer(s, ch);

	skip_whitespace(s, ch);
	if(ch == '\0' || sscanf((s - 1), "%ld", &onestat.size) != 1) {
	    break;
	}
	skip_integer(s, ch);

	skip_whitespace(s, ch);
	if(ch == '\0' || sscanf((s - 1), "%ld", &onestat.csize) != 1) {
	    break;
	}
	skip_integer(s, ch);

	skip_whitespace(s, ch);
	if(ch == '\0' || sscanf((s - 1), "%ld", &onestat.secs) != 1) {
	    break;
	}
	skip_integer(s, ch);

	skip_whitespace(s, ch);
	if(ch == '\0' || sscanf((s - 1), "%ld", &date) != 1) {
	    break;
	}
	skip_integer(s, ch);

	skip_whitespace(s, ch);
	if(ch != '\0') {
	    if(sscanf((s - 1), "%d", &onestat.filenum) != 1) {
		break;
	    }
	    skip_integer(s, ch);

	    skip_whitespace(s, ch);
	    if(ch == '\0') {
		break;
	    }
	    strncpy(onestat.label, s-1, sizeof(onestat.label)-1);
	    onestat.label[sizeof(onestat.label)-1] = '\0';
	}

	onestat.date = date;	/* time_t not guarranteed to be long */

	if(level < 0 || level > DUMP_LEVELS-1) break;

	info->inf[level] = onestat;
    }
   
    if(line == NULL) return -1;

    rc = sscanf(line, "last_level: %d %d", 
		&info->last_level, &info->consecutive_runs);
		
    amfree(line);
    if(rc > 2) return -2;
    rc = 0;

    nb_history = 0;
    for(i=0;i<=NB_HISTORY;i++) {
	info->history[i].level = -2;
    }
    for(rc = -2; (line = agets(infof)) != NULL; free(line)) {
	history_t onehistory;	/* one history record */
	long date;

	if(line[0] == '/' && line[1] == '/') {
	    info->history[nb_history].level = -2;
	    rc = 0;
	    amfree(line);
	    return 0;				/* normal end of record */
	}

	memset(&onehistory, 0, sizeof(onehistory));

	s = line;
	ch = *s++;

#define sc "history:"
	if(strncmp(line, sc, sizeof(sc)-1) != 0) {
	    break;
	}
	s += sizeof(sc)-1;
	ch = s[-1];
#undef sc

	skip_whitespace(s, ch);
	if(ch == '\0' || sscanf((s - 1), "%d", &onehistory.level) != 1) {
	    break;
	}
	skip_integer(s, ch);

	skip_whitespace(s, ch);
	if(ch == '\0' || sscanf((s - 1), "%ld", &onehistory.size) != 1) {
	    break;
	}
	skip_integer(s, ch);

	skip_whitespace(s, ch);
	if(ch == '\0' || sscanf((s - 1), "%ld", &onehistory.csize) != 1) {
	    break;
	}
	skip_integer(s, ch);

	skip_whitespace(s, ch);
	if(ch == '\0' || sscanf((s - 1), "%ld", &date) != 1) {
	    break;
	}
	skip_integer(s, ch);

	onehistory.date = date;	/* time_t not guarranteed to be long */

	onehistory.secs = -1;
	skip_whitespace(s, ch);
	if(ch != '\0') {
	    if(sscanf((s - 1), "%ld", &onehistory.secs) != 1) {
		break;
	    }
	    skip_integer(s, ch);
	}

	info->history[nb_history++] = onehistory;
    }

    if((line = agets(infof)) == NULL) return -1; /* // line */
    amfree(line);

    return rc;
}

int write_txinfofile(infof, info)
FILE *infof;
info_t *info;
{
    int i;
    stats_t *sp;
    perf_t *pp;
    int level;

    fprintf(infof, "version: %d\n", 0);

    fprintf(infof, "command: %d\n", info->command);

    pp = &info->full;

    fprintf(infof, "full-rate:");
    for(i=0; i<AVG_COUNT; i++)
	if(pp->rate[i] >= 0.0)
	    fprintf(infof, " %f", pp->rate[i]);
    fprintf(infof, "\n");

    fprintf(infof, "full-comp:");
    for(i=0; i<AVG_COUNT; i++)
	if(pp->comp[i] >= 0.0)
	    fprintf(infof, " %f", pp->comp[i]);
    fprintf(infof, "\n");

    pp = &info->incr;

    fprintf(infof, "incr-rate:");
    for(i=0; i<AVG_COUNT; i++)
	if(pp->rate[i] >= 0.0)
	    fprintf(infof, " %f", pp->rate[i]);
    fprintf(infof, "\n");

    fprintf(infof, "incr-comp:");
    for(i=0; i<AVG_COUNT; i++)
	if(pp->comp[i] >= 0.0)
	    fprintf(infof, " %f", pp->comp[i]);
    fprintf(infof, "\n");

    for(level=0; level<DUMP_LEVELS; level++) {
	sp = &info->inf[level];

	if(sp->date < (time_t)0 && sp->label[0] == '\0') continue;

	fprintf(infof, "stats: %d %ld %ld %ld %ld", level,
	       sp->size, sp->csize, sp->secs, (long)sp->date);
	if(sp->label[0] != '\0')
	    fprintf(infof, " %d %s", sp->filenum, sp->label);
	fprintf(infof, "\n");
    }

    fprintf(infof, "last_level: %d %d\n", info->last_level, info->consecutive_runs);

    for(i=0;info->history[i].level > -1;i++) {
	fprintf(infof, "history: %d %ld %ld %ld %ld\n",info->history[i].level,
		info->history[i].size, info->history[i].csize,
		info->history[i].date, info->history[i].secs);
    }
    fprintf(infof, "//\n");

    return 0;
}

int delete_txinfofile(host, disk)
char *host;
char *disk;
{
    char *fn = NULL, *fn_new = NULL;
    int rc;

    host = sanitise_filename(host);
    disk = sanitise_filename(disk);
    fn = vstralloc(infodir,
		   "/", host,
		   "/", disk,
		   "/info",
		   NULL);
    fn_new = stralloc2(fn, ".new");

    amfree(host);
    amfree(disk);

    unlink(fn_new);
    amfree(fn_new);

    rc = rmpdir(fn, infodir);
    amfree(fn);

    return rc;
}
#endif

#ifndef TEXTDB
static char *lockname = NULL;
#endif

int open_infofile(filename)
char *filename;
{
#ifdef TEXTDB
    assert(infodir == (char *)0);

    infodir = stralloc(filename);

    return 0; /* success! */
#else
    /* lock the dbm file */

    lockname = newstralloc2(lockname, filename, ".lck");
    if((lockfd = open(lockname, O_CREAT|O_RDWR, 0644)) == -1)
	return 2;

    if(amflock(lockfd, "info") == -1) {
	aclose(lockfd);
	unlink(lockname);
	return 3;
    }

    if(!(infodb = dbm_open(filename, O_CREAT|O_RDWR, 0644))) {
	amfunlock(lockfd, "info");
	aclose(lockfd);
	unlink(lockname);
	return 1;
    }

    return (infodb == NULL);	/* return 1 on error */
#endif
}

void close_infofile()
{
#ifdef TEXTDB
    assert(infodir != (char *)0);

    amfree(infodir);
#else
    dbm_close(infodb);

    if(amfunlock(lockfd, "info") == -1)
	error("could not unlock infofile: %s", strerror(errno));

    aclose(lockfd);
    lockfd = -1;

    unlink(lockname);
#endif
}

/* Convert a dump level to a GMT based time stamp */
char *get_dumpdate(info, lev)
info_t *info;
int lev;
{
    static char stamp[20]; /* YYYY:MM:DD:hh:mm:ss */
    int l;
    time_t this, last;
    struct tm *t;

    last = EPOCH;

    for(l = 0; l < lev; l++) {
	this = info->inf[l].date;
	if (this > last) last = this;
    }

    t = gmtime(&last);
    ap_snprintf(stamp, sizeof(stamp), "%d:%d:%d:%d:%d:%d",
		t->tm_year+1900, t->tm_mon+1, t->tm_mday,
		t->tm_hour, t->tm_min, t->tm_sec);

    return stamp;
}

double perf_average(a, d)
/* Weighted average */
float *a;	/* array of items to average */
double d;	/* default value */
{
    double sum;	/* running total */
    int n;	/* number of items in sum */
    int w;	/* weight */
    int i;	/* counter */

    sum = 0.0;
    n = 0;

    for(i = 0; i < AVG_COUNT; i++) {
	if(a[i] >= 0.0) {
	    w = AVG_COUNT - i;
	    sum += a[i] * w;
	    n += w;
	}
    }

    if(n == 0) return d;
    return sum / n;
}

void zero_info(info)
info_t *info;
{
    int i;

    memset(info, '\0', sizeof(info_t));

    for(i = 0; i < AVG_COUNT; i++) {
	info->full.comp[i] = info->incr.comp[i] = -1.0;
	info->full.rate[i] = info->incr.rate[i] = -1.0;
    }

    for(i = 0; i < DUMP_LEVELS; i++) {
	info->inf[i].date = (time_t)-1;
    }

    info->last_level = -1;
    info->consecutive_runs = -1;

    for(i=0;i<=NB_HISTORY;i++) {
	info->history[i].level = -2;
	info->history[i].size = 0;
	info->history[i].csize = 0;
	info->history[i].date = 0;
    }
    return;
}

int get_info(hostname, diskname, info)
char *hostname, *diskname;
info_t *info;
{
    int rc;

    (void) zero_info(info);

    {
#ifdef TEXTDB
	FILE *infof;

	infof = open_txinfofile(hostname, diskname, "r");

	if(infof == NULL) {
	    rc = -1; /* record not found */
	}
	else {
	    rc = read_txinfofile(infof, info);

	    close_txinfofile(infof);
	}
#else
	datum k, d;

	/* setup key */

	k.dptr = vstralloc(hostname, ":", diskname, NULL);
	k.dsize = strlen(k.dptr)+1;

	/* lookup record */

	d = dbm_fetch(infodb, k);
	amfree(k.dptr);
	if(d.dptr == NULL) {
	    rc = -1; /* record not found */
	}
	else {
	    memcpy(info, d.dptr, d.dsize);
	    rc = 0;
	}
#endif
    }

    return rc;
}


int get_firstkey(hostname, hostname_size, diskname, diskname_size)
char *hostname, *diskname;
int hostname_size, diskname_size;
{
#ifdef TEXTDB
    assert(0);
    return 0;
#else
    datum k;
    int rc;
    char *s, *fp;
    int ch;

    k = dbm_firstkey(infodb);
    if(k.dptr == NULL) return 0;

    s = k.dptr;
    ch = *s++;

    skip_whitespace(s, ch);
    if(ch == '\0') return 0;
    fp = hostname;
    while(ch && ch != ':') {
	if(fp >= hostname+hostname_size-1) {
	    fp = NULL;
	    break;
	}
	*fp = ch;
	ch = *s++;
    }
    if(fp == NULL) return 0;
    *fp = '\0';

    if(ch != ':') return 0;
    ch = *s++;
    copy_string(s, ch, diskname, diskname_size, fp);
    if(fp == NULL) return 0;

    return 1;
#endif
}


int get_nextkey(hostname, hostname_size, diskname, diskname_size)
char *hostname, *diskname;
int hostname_size, diskname_size;
{
#ifdef TEXTDB
    assert(0);
    return 0;
#else
    datum k;
    int rc;
    char *s, *fp;
    int ch;

    k = dbm_nextkey(infodb);
    if(k.dptr == NULL) return 0;

    s = k.dptr;
    ch = *s++;

    skip_whitespace(s, ch);
    if(ch == '\0') return 0;
    fp = hostname;
    while(ch && ch != ':') {
	if(fp >= hostname+hostname_size-1) {
	    fp = NULL;
	    break;
	}
	*fp = ch;
	ch = *s++;
    }
    if(fp == NULL) return 0;
    *fp = '\0';

    if(ch != ':') return 0;
    ch = *s++;
    copy_string(s, ch, diskname, diskname_size, fp);
    if(fp == NULL) return 0;

    return 1;
#endif
}


int put_info(hostname, diskname, info)
     char *hostname, *diskname;
     info_t *info;
{
#ifdef TEXTDB
    FILE *infof;
    int rc;

    infof = open_txinfofile(hostname, diskname, "w");

    if(infof == NULL) return -1;

    rc = write_txinfofile(infof, info);

    rc = rc || close_txinfofile(infof);

    return rc;
#else
    datum k, d;
    int maxlev;

    /* setup key */

    k.dptr = vstralloc(hostname, ":", diskname, NULL);
    k.dsize = strlen(k.dptr)+1;

    d.dptr = (char *)info;
    d.dsize = sizeof(info_t);

    /* store record */

    if(dbm_store(infodb, k, d, DBM_REPLACE) != 0) {
	amfree(k.dptr);
	return -1;
    }

    amfree(k.dptr);
    return 0;
#endif
}


int del_info(hostname, diskname)
char *hostname, *diskname;
{
#ifdef TEXTDB
    return delete_txinfofile(hostname, diskname);
#else
    char key[MAX_KEY];
    datum k;

    /* setup key */

    k.dptr = vstralloc(hostname, ":", diskname, NULL);
    k.dsize = strlen(key)+1;

    /* delete key and record */

    if(dbm_delete(infodb, k) != 0) {
	amfree(k.dptr);
	return -1;
    }
    amfree(k.dptr);
    return 0;
#endif
}


#ifdef TEST

void dump_rec(info)
info_t *info;
{
    int i;
    stats_t *sp;

    printf("command word: %d\n", info->command);
    printf("full dump rate (K/s) %5.1f, %5.1f, %5.1f\n",
	   info->full.rate[0],info->full.rate[1],info->full.rate[2]);
    printf("full comp rate %5.1f, %5.1f, %5.1f\n",
	   info->full.comp[0]*100,info->full.comp[1]*100,info->full.comp[2]*100);
    printf("incr dump rate (K/s) %5.1f, %5.1f, %5.1f\n",
	   info->incr.rate[0],info->incr.rate[1],info->incr.rate[2]);
    printf("incr comp rate %5.1f, %5.1f, %5.1f\n",
	   info->incr.comp[0]*100,info->incr.comp[1]*100,info->incr.comp[2]*100);
    for(i = 0; i < DUMP_LEVELS; i++) {
	sp = &info->inf[i];
	if( sp->size != -1) {

	    printf("lev %d date %ld tape %s filenum %d size %ld csize %ld secs %ld\n",
	           i, (long)sp->date, sp->label, sp->filenum,
	           sp->size, sp->csize, sp->secs);
	}
    }
    putchar('\n');
   printf("last_level: %d %d\n", info->last_level, info->consecutive_runs);
}

#ifdef TEXTDB
void dump_db(host, disk)
char *host, *disk;
{
    info_t info;
    int rc;

    if((rc = get_info(host, disk, &info)) == 0) {
	dump_rec(&info);
    } else {
	printf("cannot fetch information for %s:%s rc=%d\n", host, disk, rc);
    }
}
#else
void dump_db(str)
char *str;
{
    datum k,d;
    int rec,r,num;
    info_t info;


    printf("info database %s:\n--------\n", str);
    rec = 0;
    k = dbm_firstkey(infodb);
    while(k.dptr != NULL) {

	printf("%3d: KEY %s =\n", rec, k.dptr);

	d = dbm_fetch(infodb, k);
	memset(&info, '\0', sizeof(info));
	memcpy(&info, d.dptr, d.dsize);

	num = (d.dsize-HEADER)/sizeof(stats_t);
	dump_rec(&info);

	k = dbm_nextkey(infodb);
	rec++;
    }
    puts("--------\n");
}
#endif

int
main(argc, argv)
int argc;
char *argv[];
{
  int i;
  int fd;
  unsigned long malloc_hist_1, malloc_size_1;
  unsigned long malloc_hist_2, malloc_size_2;

  for(fd = 3; fd < FD_SETSIZE; fd++) {
    /*
     * Make sure nobody spoofs us with a lot of extra open files
     * that would cause an open we do to get a very high file
     * descriptor, which in turn might be used as an index into
     * an array (e.g. an fd_set).
     */
    close(fd);
  }

  set_pname("infofile");

  malloc_size_1 = malloc_inuse(&malloc_hist_1);

  for(i = 1; i < argc; ++i) {
#ifdef TEXTDB
    if(i+1 >= argc) {
      fprintf(stderr,"usage: %s host disk [host disk ...]\n",argv[0]);
      return 1;
    }
    open_infofile("curinfo");
    dump_db(argv[i], argv[i+1]);
    i++;
#else
    open_infofile(argv[i]);
    dump_db(argv[i]);
#endif
    close_infofile();
  }

  malloc_size_2 = malloc_inuse(&malloc_hist_2);

  if(malloc_size_1 != malloc_size_2) {
    malloc_list(fileno(stderr), malloc_hist_1, malloc_hist_2);
  }

  return 0;
}

#endif /* TEST */
