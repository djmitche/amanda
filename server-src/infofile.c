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

#define MAX_KEY 256
#define HEADER	(sizeof(info_t)-DUMP_LEVELS*sizeof(stats_t)) 

static DBM *infodb = NULL;
static lockfd = -1;

int open_infofile(filename)
char *filename;
{
    char lockname[256];

    /* lock the dbm file */

    sprintf(lockname, "%s.dir", filename);
    if((lockfd = open(lockname, O_CREAT|O_RDWR, 0666)) == -1)
	return 2;

    if(amflock(lockfd) == -1)
	return 3;

    infodb = dbm_open(filename, O_CREAT|O_RDWR, 0666);
    return (infodb == NULL);	/* return 1 on error */
}

void close_infofile()
{
    dbm_close(infodb);

    if(amfunlock(lockfd) == -1)
	error("could not unlock infofile: %s", strerror(errno));

    close(lockfd);
    lockfd = -1;
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
}


int get_firstkey(hostname, diskname)
char *hostname, *diskname;
{
    datum k;
    int rc;

    k = dbm_firstkey(infodb);
    if(k.dptr == NULL) return 0;

    rc = sscanf(k.dptr, "%[^:]:%s", hostname, diskname);
    if(rc != 2) return 0;
    return 1;
}


int get_nextkey(hostname, diskname)
char *hostname, *diskname;
{
    datum k;
    int rc;

    k = dbm_nextkey(infodb);
    if(k.dptr == NULL) return 0;

    rc = sscanf(k.dptr, "%[^:]:%s", hostname, diskname);
    if(rc != 2) return 0;
    return 1;
}


int put_info(hostname, diskname, record)
char *hostname, *diskname;
info_t *record;
{
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
}


int del_info(hostname, diskname)
char *hostname, *diskname;
{
    char key[MAX_KEY];
    datum k;

    /* setup key */
    
    sprintf(key, "%s:%s", hostname, diskname);
    k.dptr = key; 
    k.dsize = strlen(key)+1;

    /* delete key and record */

    if(dbm_delete(infodb, k) != 0) return -1;
    return 0;
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
