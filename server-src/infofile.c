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
 * infofile.c - manage current info file
 */
#include "amanda.h"
#include "conffile.h"
#include "infofile.h"

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

    assert(infofile == (char *)0);

    writing = (*mode == 'w');

/* XXX - need to sanitise host and disk */
    len = strlen(infodir) + strlen(host) + strlen(disk) + 7;
    infofile = alloc(len + 1);

    /* create the directory structure if in write mode */
    if (writing) {
	rc = mkdir(infodir, 0755);
	if (rc == -1 && errno == EEXIST) rc = 0;

	if (rc == 0) {
	    sprintf(infofile, "%s/%s", infodir, host);
	    rc = mkdir(infofile, 0755);
	    if (rc == -1 && errno == EEXIST) rc = 0;

	    if (rc == 0) {
		sprintf(infofile, "%s/%s/%s", infodir, host, disk);
		rc = mkdir(infofile, 0755);
		if (rc == -1 && errno == EEXIST) rc = 0;
	    }
	}
	if (rc == -1) {
	    free(infofile);
#ifdef ASSERTIONS
	    infofile = (char *)0;
#endif
	    return NULL;
	}
    }

    sprintf(infofile, "%s/%s/%s/info", infodir, host, disk);

    newinfofile = alloc(len + 4 + 1);
    sprintf(newinfofile, "%s.new", infofile);

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

int read_txinfofile(infof, info)
FILE *infof;
info_t *info;
{
    char line[1024];
    int version;
    int rc, level;
    stats_t onestat;
    long onedate;

    /* get version: command: lines */

    if(!fgets(line, 1024, infof)) return -1;
    if(sscanf(line, "version: %d", &version) != 1) return -2;

    if(!fgets(line, 1024, infof)) return -1;
    if(sscanf(line, "command: %d", &info->command) != 1) return -2;

    /* get rate: and comp: lines for full dumps */

    if(!fgets(line, 1024, infof)) return -1;
    rc = sscanf(line, "full-rate: %f %f %f", 
		&info->full.rate[0], &info->full.rate[1], &info->full.rate[2]);
    if(rc != 3) return -2;

    if(!fgets(line, 1024, infof)) return -1;
    rc = sscanf(line, "full-comp: %f %f %f", 
		&info->full.comp[0], &info->full.comp[1], &info->full.comp[2]);
    if(rc != 3) return -2;

    /* get rate: and comp: lines for incr dumps */

    if(!fgets(line, 1024, infof)) return -1;
    rc = sscanf(line, "incr-rate: %f %f %f", 
		&info->incr.rate[0], &info->incr.rate[1], &info->incr.rate[2]);
    if(rc != 3) return -2;

    if(!fgets(line, 1024, infof)) return -1;
    rc = sscanf(line, "incr-comp: %f %f %f", 
		&info->incr.comp[0], &info->incr.comp[1], &info->incr.comp[2]);
    if(rc != 3) return -2;

    /* get stats for dump levels */

    while(1) {
	if(!fgets(line, 1024, infof)) return -1;
	if(!strncmp(line, "//", 2)) {
	    /* end of record */
	    break;
	}
	memset(&onestat, 0, sizeof(onestat));
	rc = sscanf(line, "stats: %d %d %d %d %ld %d %80[^\n]",
		    &level, &onestat.size, &onestat.csize, &onestat.secs,
		    &onedate, &onestat.filenum, onestat.label);
	if(rc != 7) return -2;

	/* time_t not guarranteed to be long */
	onestat.date = onedate;
	if(level < 0 || level > DUMP_LEVELS-1) return -2;

	info->inf[level] = onestat;
    }

    return 0;
}

int write_txinfofile(infof, info)
FILE *infof;
info_t *info;
{
    int i,l;

    fprintf(infof, "version: %d\n", 0);
    fprintf(infof, "command: %d\n", info->command);

    fprintf(infof, "full-rate:");
    for(i=0; i<AVG_COUNT; i++)
	fprintf(infof, " %f", info->full.rate[i]);
    fprintf(infof, "\nfull-comp:");
    for(i=0; i<AVG_COUNT; i++)
	fprintf(infof, " %f", info->full.comp[i]);

    fprintf(infof, "\nincr-rate:");
    for(i=0; i<AVG_COUNT; i++)
	fprintf(infof, " %f", info->incr.rate[i]);
    fprintf(infof, "\nincr-comp:");
    for(i=0; i<AVG_COUNT; i++)
	fprintf(infof, " %f", info->incr.comp[i]);

    fprintf(infof, "\n");
    for(l=0; l<DUMP_LEVELS; l++) {
	if(info->inf[l].date == EPOCH) continue;
	fprintf(infof, "stats: %d %d %d %d %ld %d %s\n", l, 
	       info->inf[l].size, info->inf[l].csize, info->inf[l].secs,
	       (long)info->inf[l].date, info->inf[l].filenum,
	       info->inf[l].label);
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

/* XXX - need to sanitise host and disk */
    len = strlen(infodir) + strlen(host) + strlen(disk) + 7;
    fn = alloc(len + 4 + 1);

    sprintf(fn, "%s/%s/%s/info.new", infodir, host, disk);
    rc = unlink(fn);

    sprintf(fn, "%s/%s/%s/info", infodir, host, disk);
    rc = unlink(fn);
    if(errno == ENOENT) rc = 0; /* already gone! */

    /* try to clean up */
    if (rc == 0) {
	sprintf(fn, "%s/%s/%s", infodir, host, disk);
	rc2 = rmdir(fn);
	if (rc2 == 0) {
	    sprintf(fn, "%s/%s", infodir, host);
	    rc2 = rmdir(fn);
	}
    }

    free(fn);

    return rc;
}
#endif

int open_infofile(filename)
char *filename;
{
#ifdef TEXTDB
    assert(infodir == (char *)0);

    infodir = stralloc(filename);

    return 0; /* success! */
#else
    char lockname[256];

    /* lock the dbm file */

    sprintf(lockname, "%s.dir", filename);
    if((lockfd = open(lockname, O_CREAT|O_RDWR, 0666)) == -1)
	return 2;

    if(amflock(lockfd, "info") == -1)
	return 3;

    infodb = dbm_open(filename, O_CREAT|O_RDWR, 0666);
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
    len = sprintf(stamp, "%d:%d:%d:%d:%d:%d",
	t->tm_year+1900, t->tm_mon+1, t->tm_mday,
	t->tm_hour, t->tm_min, t->tm_sec);

    assert(len < sizeof(stamp));

    return stamp;
}

double perf_average(a, d)
float *a;
double d;
{
    int total = 3;
    double avg = 0.0;

    if(a[0] == -1.0)
	return d;

    avg = a[0]*3;
    if(a[1] != -1.0) avg += a[1]*2, total += 2;
    if(a[2] != -1.0) avg += a[2],   total += 1;
    return avg / total;
}

int get_info(hostname, diskname, record)
char *hostname, *diskname;
info_t *record;
{
#ifdef TEXTDB
    FILE *infof;
    int rc;
    int i;

    memset(record, '\0', sizeof(info_t));

    infof = open_txinfofile(hostname, diskname, "r");

    if(infof == NULL) {
	rc = -1; /* record not found */
    }
    else {
	rc = read_txinfofile(infof, record);

	close_txinfofile(infof);
    }

    if (rc != 0) {
	for(i = 0; i < AVG_COUNT; i++) {
	    record->full.comp[i] = record->incr.comp[i] = -1.0;
	    record->full.rate[i] = record->incr.rate[i] = -1.0;
	}
    }

    return rc;
#else
    char key[MAX_KEY];
    datum k, d;
    int i;

    /* setup key */
    
    sprintf(key, "%s:%s", hostname, diskname);
    k.dptr = key; 
    k.dsize = strlen(key)+1;

    memset(record, '\0', sizeof(info_t));

    /* lookup record */

    d = dbm_fetch(infodb, k);
    if(d.dptr == NULL)	{		/* doesn't exist */
	for(i = 0; i < AVG_COUNT; i++) {
	    record->full.comp[i] = record->incr.comp[i] = -1.0;
	    record->full.rate[i] = record->incr.rate[i] = -1.0;
	}
	return -1;
    }

    /* return record */
    memcpy(record, d.dptr, d.dsize);
    return 0;
#endif
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
    
    sprintf(key, "%s:%s", hostname, diskname);
    k.dptr = key; 
    k.dsize = strlen(key)+1;

    /* find last non-empty dump level */

    for(maxlev = DUMP_LEVELS-1; maxlev > 0; maxlev--)
	if(record->inf[maxlev].date != EPOCH) break;

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
    
    sprintf(key, "%s:%s", hostname, diskname);
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
	printf("lev %d date %d tape %s filenum %d size %d csize %d secs %d\n",
	       i, r->inf[i].date, r->inf[i].label, r->inf[i].filenum,
	       r->inf[i].size, r->inf[i].csize, r->inf[i].secs);
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

#endif /* TEST */
