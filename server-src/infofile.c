/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991 University of Maryland
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
 * $Id: infofile.c,v 1.25 1997/12/16 20:44:58 jrj Exp $
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
#  define HEADER	(sizeof(info_t)-DUMP_LEVELS*sizeof(stats_t))

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
    int rc;
    int len;
    char *p;
    char *shost, *sdisk;

    assert(infofile == (char *)0);

    writing = (*mode == 'w');

    shost = stralloc(sanitise_filename(host));
    sdisk = stralloc(disk);

    len = strlen(infodir) + strlen(shost) + strlen(sdisk) + 7;
    infofile = alloc(len + 1);
    ap_snprintf(infofile, len + 1, "%s/%s/%s/info", infodir, shost, sdisk);

    free(sdisk);
    free(shost);

    /* create the directory structure if in write mode */
    if (writing) {
        if (mkpdir(infofile, 0755, (uid_t)-1, (gid_t)-1) == -1) {
	    free(infofile);
#ifdef ASSERTIONS
	    infofile = (char *)0;
#endif
	    return NULL;
	}
    }

    newinfofile = alloc(len + 4 + 1);
    ap_snprintf(newinfofile, len + 4 + 1, "%s.new", infofile);

    if(writing) {
	infof = fopen(newinfofile, mode);
	amflock(fileno(infof), "info");
    }
    else {
	infof = fopen(infofile, mode);
	/* no need to lock readers */
    }

    if(infof == (FILE *)0) {
	free(infofile);
	free(newinfofile);
#ifdef ASSERTIONS
	infofile = (char *)0;
#endif
	return NULL;
    }

    return infof;
}

int close_txinfofile(infof)
FILE *infof;
{
    int rc;

    assert(infofile != (char *)0);

    if(writing) {
	rc = rename(newinfofile, infofile);

	amfunlock(fileno(infof), "info");
    }

    free(infofile);
    free(newinfofile);

#ifdef ASSERTIONS
    infofile = (char *)0;
#endif

    rc = fclose(infof);
    if (rc == EOF) rc = -1;

    return rc;
}

int read_txinfofile(infof, info) /* XXX - code assumes AVG_COUNT == 3 */
FILE *infof;
info_t *info;
{
    char line[1024];
    int version;
    int rc;
    stats_t *sp;
    perf_t *pp;

    /* get version: command: lines */

    if(!fgets(line, 1024, infof)) return -1;
    rc = sscanf(line, "version: %d", &version);
    if(rc != 1) return -2;

    if(!fgets(line, 1024, infof)) return -1;
    rc = sscanf(line, "command: %d", &info->command);
    if(rc != 1) return -2;

    /* get rate: and comp: lines for full dumps */

    pp = &info->full;

    if(!fgets(line, 1024, infof)) return -1;
    rc = sscanf(line, "full-rate: %f %f %f",
		&pp->rate[0], &pp->rate[1], &pp->rate[2]);
    if(rc > 3) return -2;

    if(!fgets(line, 1024, infof)) return -1;
    rc = sscanf(line, "full-comp: %f %f %f",
		&pp->comp[0], &pp->comp[1], &pp->comp[2]);
    if(rc > 3) return -2;

    /* get rate: and comp: lines for incr dumps */

    pp = &info->incr;

    if(!fgets(line, 1024, infof)) return -1;
    rc = sscanf(line, "incr-rate: %f %f %f",
		&pp->rate[0], &pp->rate[1], &pp->rate[2]);
    if(rc > 3) return -2;

    if(!fgets(line, 1024, infof)) return -1;
    rc = sscanf(line, "incr-comp: %f %f %f",
		&pp->comp[0], &pp->comp[1], &pp->comp[2]);
    if(rc > 3) return -2;

    /* get stats for dump levels */

    while(1) {
	stats_t s;	/* one stat record */
	long date;
	int level;

	if(!fgets(line, 1024, infof)) return -1;
	if(!strncmp(line, "//", 2)) {
	    /* end of record */
	    break;
	}
	memset(&s, 0, sizeof(s));
	rc = sscanf(line, "stats: %d %ld %ld %ld %ld %d %80[^\n]",
		    &level, &s.size, &s.csize, &s.secs,
		    &date, &s.filenum, s.label);
	if(rc < 5 || rc > 7) return -2;	/* filenum and label are optional */

	s.date = date;	/* time_t not guarranteed to be long */

	if(level < 0 || level > DUMP_LEVELS-1) return -2;

	info->inf[level] = s;
    }

    return 0;
}

int write_txinfofile(infof, info)
FILE *infof;
info_t *info;
{
    int i, l;
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

    fprintf(infof, "//\n");

    return 0;
}

int delete_txinfofile(host, disk)
char *host;
char *disk;
{
    int len;
    char *fn;
    int rc, rc2;
    char *shost, *sdisk;

    shost = stralloc(sanitise_filename(host));
    sdisk = stralloc(disk);

    len = strlen(infodir) + strlen(shost) + strlen(sdisk) + 7;
    fn = alloc(len + 4 + 1);

    ap_snprintf(fn, len + 4 + 1, "%s/%s/%s/info.new", infodir, shost, sdisk);

    free(sdisk);
    free(shost);

    rc = unlink(fn);

    fn[len] = '\0';	/* remove the '.new' */

    rc = rmpdir(fn, infodir);

    free(fn);

    return rc;
}
#endif

static char lockname[1024];

int open_infofile(filename)
char *filename;
{
#ifdef TEXTDB
    assert(infodir == (char *)0);

    infodir = stralloc(filename);

    return 0; /* success! */
#else
    /* lock the dbm file */

    ap_snprintf(lockname, sizeof(lockname), "%s.lck", filename);
    if((lockfd = open(lockname, O_CREAT|O_RDWR, 0644)) == -1)
	return 2;

    if(amflock(lockfd, "info") == -1)
	return 3;

    infodb = dbm_open(filename, O_CREAT|O_RDWR, 0644);
    return (infodb == NULL);	/* return 1 on error */
#endif
}

void close_infofile()
{
#ifdef TEXTDB
    assert(infodir != (char *)0);

    free(infodir);
#ifdef ASSERTIONS
    infodir = (char *)0;
#endif
#else
    dbm_close(infodb);

    if(amfunlock(lockfd, "info") == -1)
	error("could not unlock infofile: %s", strerror(errno));

    close(lockfd);
    lockfd = -1;

    unlink(lockname);
#endif
}

/* Convert a dump level to a GMT based time stamp */
char *get_dumpdate(rec, lev)
info_t *rec;
int lev;
{
    static char stamp[20]; /* YYYY:MM:DD:hh:mm:ss */
    int l;
    time_t this, last;
    struct tm *t;
    int len;

    last = EPOCH;

    for(l = 0; l < lev; l++) {
	this = rec->inf[l].date;
	if (this > last) last = this;
    }

    t = gmtime(&last);
    len = ap_snprintf(stamp, sizeof(stamp), "%d:%d:%d:%d:%d:%d",
	t->tm_year+1900, t->tm_mon+1, t->tm_mday,
	t->tm_hour, t->tm_min, t->tm_sec);

    assert(len < sizeof(stamp));

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

void zero_info(ip)
info_t *ip;
{
    int i;

    memset(ip, '\0', sizeof(info_t));

    for(i = 0; i < AVG_COUNT; i++) {
	ip->full.comp[i] = ip->incr.comp[i] = -1.0;
	ip->full.rate[i] = ip->incr.rate[i] = -1.0;
    }

    for(i = 0; i < DUMP_LEVELS; i++) {
	ip->inf[i].date = (time_t)-1;
    }

    return;
}

int get_info(hostname, diskname, record)
char *hostname, *diskname;
info_t *record;
{
    int rc;

    (void) zero_info(record);

    {
#ifdef TEXTDB
	FILE *infof;

	infof = open_txinfofile(hostname, diskname, "r");

	if(infof == NULL) {
	    rc = -1; /* record not found */
	}
	else {
	    rc = read_txinfofile(infof, record);

	    close_txinfofile(infof);
	}
#else
	char key[MAX_KEY];
	datum k, d;

	/* setup key */

	ap_snprintf(key, sizeof(key), "%s:%s", hostname, diskname);
	k.dptr = key;
	k.dsize = strlen(key)+1;

	/* lookup record */

	d = dbm_fetch(infodb, k);
	if(d.dptr == NULL) {
	    rc = -1; /* record not found */
	}
	else {
	    memcpy(record, d.dptr, d.dsize);
	    rc = 0;
	}
#endif
    }

    return rc;
}


int get_firstkey(hostname, diskname)
char *hostname, *diskname;
{
#ifdef TEXTDB
    assert(0);
#else
    datum k;
    int rc;

    k = dbm_firstkey(infodb);
    if(k.dptr == NULL) return 0;

    rc = sscanf(k.dptr, "%[^:]:%s", hostname, diskname);
    if(rc != 2) return 0;
    return 1;
#endif
}


int get_nextkey(hostname, diskname)
char *hostname, *diskname;
{
#ifdef TEXTDB
    assert(0);
#else
    datum k;
    int rc;

    k = dbm_nextkey(infodb);
    if(k.dptr == NULL) return 0;

    rc = sscanf(k.dptr, "%[^:]:%s", hostname, diskname);
    if(rc != 2) return 0;
    return 1;
#endif
}


int put_info(hostname, diskname, record)
char *hostname, *diskname;
info_t *record;
{
#ifdef TEXTDB
    FILE *infof;
    int rc;

    infof = open_txinfofile(hostname, diskname, "w");

    if(infof == NULL) return -1;

    rc = write_txinfofile(infof, record);

    close_txinfofile(infof);

    return rc;
#else
    char key[MAX_KEY];
    datum k, d;
    int maxlev;

    /* setup key */

    ap_snprintf(key, sizeof(key), "%s:%s", hostname, diskname);
    k.dptr = key;
    k.dsize = strlen(key)+1;

    /* find last non-empty dump level */

    for(maxlev = DUMP_LEVELS-1; maxlev > 0; maxlev--) {
	if(record->inf[maxlev].date >= (time_t)0) break;
	if(record->inf[maxlev].label[0] != '\0') break;
    }

    d.dptr = (char *)record;
    d.dsize = HEADER + (maxlev+1)*sizeof(stats_t);

    /* store record */

    if(dbm_store(infodb, k, d, DBM_REPLACE) != 0) return -1;

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

    ap_snprintf(key, sizeof(key), "%s:%s", hostname, diskname);
    k.dptr = key;
    k.dsize = strlen(key)+1;

    /* delete key and record */

    if(dbm_delete(infodb, k) != 0) return -1;
    return 0;
#endif
}


#ifdef TEST

void dump_rec(r, num)
info_t *r;
int num;
{
    int i;
    stats_t *sp;

    printf("command word: %d\n", r->command);
    printf("full dump rate (K/s) %5.1f, %5.1f, %5.1f\n",
	   r->full.rate[0],r->full.rate[1],r->full.rate[2]);
    printf("full comp rate %5.1f, %5.1f, %5.1f\n",
	   r->full.comp[0]*100,r->full.comp[1]*100,r->full.comp[2]*100);
    printf("incr dump rate (K/s) %5.1f, %5.1f, %5.1f\n",
	   r->incr.rate[0],r->incr.rate[1],r->incr.rate[2]);
    printf("incr comp rate %5.1f, %5.1f, %5.1f\n",
	   r->incr.comp[0]*100,r->incr.comp[1]*100,r->incr.comp[2]*100);
    for(i = 0; i < num; i++) {
	sp = &r->inf[i];

	printf("lev %d date %d tape %s filenum %d size %ld csize %ld secs %ld\n",
	       i, sp->date, sp->label, sp->filenum,
	       sp->size, sp->csize, sp->secs);
    }
    putchar('\n');
}

void dump_db(str)
char *str;
{
    datum k,d;
    int rec,r,num;
    info_t record;


    printf("info database %s:\n--------\n", str);
    rec = 0;
    k = dbm_firstkey(infodb);
    while(k.dptr != NULL) {

	printf("%3d: KEY %s =\n", rec, k.dptr);

	d = dbm_fetch(infodb, k);
	memset(&record, '\0', sizeof(record));
	memcpy(&record, d.dptr, d.dsize);

	num = (d.dsize-HEADER)/sizeof(stats_t);
	dump_rec(&record, num);

	k = dbm_nextkey(infodb);
	rec++;
    }
    puts("--------\n");
}

main(argc, argv)
int argc;
char *argv[];
{
  int i;
  for(i = 1; i < argc; ++i) {
    open_infofile(argv[i]);
    dump_db(argv[i]);
    close_infofile();
  }
}

char *pname = "infofile";

#endif /* TEST */
