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
 * $Id: find.c,v 1.21 2004/02/02 21:29:53 martinea Exp $
 *
 * controlling process for the Amanda backup system
 */
#include "amanda.h"
#include "conffile.h"
#include "tapefile.h"
#include "logfile.h"
#include "holding.h"
#include "find.h"

void find P((int argc, char **argv));
int find_match P((char *host, char *disk));
int search_logfile P((find_result_t **output_find, char *label, int datestamp, int datestamp_aux, char *logfile));
void search_holding_disk P((find_result_t **output_find));
char *find_nicedate P((int datestamp));
static int find_compare P((const void *, const void *));
static int parse_taper_datestamp_log P((char *, int *, char **));

static char *find_sort_order = NULL;
int dynamic_disklist = 0;
disklist_t* find_diskqp = NULL;

find_result_t *find_dump(dyna_disklist, diskqp)
int dyna_disklist;
disklist_t* diskqp;
{
    char *conf_logdir, *logfile = NULL;
    int tape, maxtape, seq, logs;
    tape_t *tp;
    find_result_t *output_find = NULL;

    dynamic_disklist = dyna_disklist;
    find_diskqp = diskqp;
    conf_logdir = getconf_str(CNF_LOGDIR);
    if (*conf_logdir == '/') {
	conf_logdir = stralloc(conf_logdir);
    } else {
	conf_logdir = stralloc2(config_dir, conf_logdir);
    }
    maxtape = lookup_nb_tape();

    for(tape = 1; tape <= maxtape; tape++) {
	char ds_str[NUM_STR_SIZE];

	tp = lookup_tapepos(tape);
	if(tp == NULL) continue;
	snprintf(ds_str, sizeof(ds_str), "%d", tp->datestamp);

	/* search log files */

	logs = 0;

	/* new-style log.<date>.<seq> */

	for(seq = 0; 1; seq++) {
	    char seq_str[NUM_STR_SIZE];

	    snprintf(seq_str, sizeof(seq_str), "%d", seq);
	    logfile = newvstralloc(logfile,
			conf_logdir, "/log.", ds_str, ".", seq_str, NULL);
	    if(access(logfile, R_OK) != 0) break;
	    logs += search_logfile(&output_find, tp->label, tp->datestamp, seq, logfile);
	}

	/* search old-style amflush log, if any */

	logfile = newvstralloc(logfile,
			       conf_logdir, "/log.", ds_str, ".amflush", NULL);
	if(access(logfile,R_OK) == 0) {
	    logs += search_logfile(&output_find, tp->label, tp->datestamp, 1000, logfile);
	}

	/* search old-style main log, if any */

	logfile = newvstralloc(logfile, conf_logdir, "/log.", ds_str, NULL);
	if(access(logfile,R_OK) == 0) {
	    logs += search_logfile(&output_find, tp->label, tp->datestamp, -1, logfile);
	}
	if(logs == 0 && tp->datestamp != 0)
	    printf("Warning: no log files found for tape %s written %s\n",
		   tp->label, find_nicedate(tp->datestamp));
    }
    amfree(logfile);
    amfree(conf_logdir);

    search_holding_disk(&output_find);
    return(output_find);
}

char **find_log()
{
    char *conf_logdir, *logfile = NULL;
    int tape, maxtape, seq, logs;
    tape_t *tp;
    char **output_find_log = NULL;
    char **current_log;

    conf_logdir = getconf_str(CNF_LOGDIR);
    if (*conf_logdir == '/') {
	conf_logdir = stralloc(conf_logdir);
    } else {
	conf_logdir = stralloc2(config_dir, conf_logdir);
    }
    maxtape = lookup_nb_tape();

    output_find_log = alloc((maxtape*5+10) * sizeof(char *));
    current_log = output_find_log;

    for(tape = 1; tape <= maxtape; tape++) {
	char ds_str[NUM_STR_SIZE];

	tp = lookup_tapepos(tape);
	if(tp == NULL) continue;
	snprintf(ds_str, sizeof(ds_str), "%d", tp->datestamp);

	/* search log files */

	logs = 0;

	/* new-style log.<date>.<seq> */

	for(seq = 0; 1; seq++) {
	    char seq_str[NUM_STR_SIZE];

	    snprintf(seq_str, sizeof(seq_str), "%d", seq);
	    logfile = newvstralloc(logfile,
			conf_logdir, "/log.", ds_str, ".", seq_str, NULL);
	    if(access(logfile, R_OK) != 0) break;
	    if( search_logfile(NULL, tp->label, tp->datestamp, seq, logfile)) {
		*current_log = vstralloc("log.", ds_str, ".", seq_str, NULL);
		current_log++;
		logs++;
		break;
	    }
	}

	/* search old-style amflush log, if any */

	logfile = newvstralloc(logfile,
			       conf_logdir, "/log.", ds_str, ".amflush", NULL);
	if(access(logfile,R_OK) == 0) {
	    if( search_logfile(NULL, tp->label, tp->datestamp, 1000, logfile)) {
		*current_log = vstralloc("log.", ds_str, ".amflush", NULL);
		current_log++;
		logs++;
	    }
	}

	/* search old-style main log, if any */

	logfile = newvstralloc(logfile, conf_logdir, "/log.", ds_str, NULL);
	if(access(logfile,R_OK) == 0) {
	    if(search_logfile(NULL, tp->label, tp->datestamp, -1, logfile)) {
		*current_log = vstralloc("log.", ds_str, NULL);
		current_log++;
		logs++;
	    }
	}
	if(logs == 0 && tp->datestamp != 0)
	    printf("Warning: no log files found for tape %s written %s\n",
		   tp->label, find_nicedate(tp->datestamp));
    }
    amfree(logfile);
    amfree(conf_logdir);
    *current_log = NULL;
    return(output_find_log);
}

void search_holding_disk(output_find)
find_result_t **output_find;
{
    holdingdisk_t *hdisk;
    sl_t  *holding_list;
    sle_t *dir;
    char *sdirname = NULL;
    char *destname = NULL;
    char *hostname = NULL;
    char *diskname = NULL;
    DIR *workdir;
    struct dirent *entry;
    int level;
    disk_t *dp;

    holding_list = pick_all_datestamp(1);

    for(hdisk = getconf_holdingdisks(); hdisk != NULL; hdisk = hdisk->next) {
	for(dir = holding_list->first; dir != NULL; dir = dir->next) {
	    sdirname = newvstralloc(sdirname,
				    hdisk->diskdir, "/", dir->name,
				    NULL);
	    if((workdir = opendir(sdirname)) == NULL) {
	        continue;
	    }

	    while((entry = readdir(workdir)) != NULL) {
		if(is_dot_or_dotdot(entry->d_name)) {
		    continue;
		}
		destname = newvstralloc(destname,
					sdirname, "/", entry->d_name,
					NULL);
		if(is_emptyfile(destname)) {
		    continue;
		}
		amfree(hostname);
		amfree(diskname);
		if(get_amanda_names(destname, &hostname, &diskname, &level) != F_DUMPFILE) {
		    continue;
		}
		if(level < 0 || level > 9)
		    continue;

		dp = NULL;
		for(;;) {
		    char *s;
		    if((dp = lookup_disk(hostname, diskname)))
	                break;
	            if((s = strrchr(hostname,'.')) == NULL)
           		break;
	            *s = '\0';
		}
		if ( dp == NULL ) {
		    continue;
		}

		if(find_match(hostname,diskname)) {
		    find_result_t *new_output_find =
			alloc(sizeof(find_result_t));
		    new_output_find->next=*output_find;
		    if(strlen(dir->name) == 8) {
			new_output_find->datestamp=atoi(dir->name);
			new_output_find->timestamp=stralloc2(dir->name, "000000");
		    }
		    else if(strlen(dir->name) == 14) {
			char *name = stralloc(dir->name);
			name[8] = '\0';
			new_output_find->datestamp=atoi(name);
			new_output_find->timestamp=stralloc(dir->name);
			amfree(name);
		    }
		    else {
			error("Bad date\n");
		    }
		    new_output_find->datestamp_aux=1001;
		    new_output_find->hostname=hostname;
		    hostname = NULL;
		    new_output_find->diskname=diskname;
		    diskname = NULL;
		    new_output_find->level=level;
		    new_output_find->label=stralloc(destname);
		    new_output_find->filenum=0;
		    new_output_find->status=stralloc("OK");
		    *output_find=new_output_find;
		}
	    }
	    closedir(workdir);
	}	
    }
    free_sl(holding_list);
    holding_list = NULL;
    amfree(destname);
    amfree(sdirname);
    amfree(hostname);
    amfree(diskname);
}

static int find_compare(i1, j1)
const void *i1;
const void *j1;
{
    int compare=0;
    find_result_t **i = (find_result_t **)i1;
    find_result_t **j = (find_result_t **)j1;

    int nb_compare=strlen(find_sort_order);
    int k;

    for(k=0;k<nb_compare;k++) {
	switch (find_sort_order[k]) {
	case 'h' : compare=strcmp((*i)->hostname,(*j)->hostname);
		   break;
	case 'H' : compare=strcmp((*j)->hostname,(*i)->hostname);
		   break;
	case 'k' : compare=strcmp((*i)->diskname,(*j)->diskname);
		   break;
	case 'K' : compare=strcmp((*j)->diskname,(*i)->diskname);
		   break;
	case 'd' : compare=(*i)->datestamp - (*j)->datestamp;
		   if (compare == 0)
			compare = (*i)->datestamp_aux - (*j)->datestamp_aux;
		   break;
	case 'D' : compare=(*j)->datestamp - (*i)->datestamp;
		   if (compare == 0)
			compare = (*j)->datestamp_aux - (*i)->datestamp_aux;
		   break;
	case 'l' : compare=(*j)->level - (*i)->level;
		   break;
	case 'L' : compare=(*i)->level - (*j)->level;
		   break;
	case 'b' : compare=strcmp((*i)->label,(*j)->label);
		   break;
	case 'B' : compare=strcmp((*j)->label,(*i)->label);
		   break;
	}
	if(compare != 0)
	    return compare;
    }
    return 0;
}

void sort_find_result(sort_order, output_find)
char *sort_order;
find_result_t **output_find;
{
    find_result_t *output_find_result;
    find_result_t **array_find_result = NULL;
    int nb_result=0;
    int no_result;

    find_sort_order = sort_order;
    /* qsort core dump if nothing to sort */
    if(*output_find==NULL)
	return;

    /* How many result */
    for(output_find_result=*output_find;
	output_find_result;
	output_find_result=output_find_result->next) {
	nb_result++;
    }

    /* put the list in an array */
    array_find_result=alloc(nb_result * sizeof(find_result_t *));
    for(output_find_result=*output_find,no_result=0;
	output_find_result;
	output_find_result=output_find_result->next,no_result++) {
	array_find_result[no_result]=output_find_result;
    }

    /* sort the array */
    qsort(array_find_result,nb_result,sizeof(find_result_t *),
	  find_compare);

    /* put the sorted result in the list */
    for(no_result=0;
	no_result<nb_result-1; no_result++) {
	array_find_result[no_result]->next = array_find_result[no_result+1];
    }
    array_find_result[nb_result-1]->next=NULL;
    *output_find=array_find_result[0];
    amfree(array_find_result);
}

void print_find_result(output_find)
find_result_t *output_find;
{
    find_result_t *output_find_result;
    int max_len_datestamp = 4;
    int max_len_hostname  = 4;
    int max_len_diskname  = 4;
    int max_len_level     = 2;
    int max_len_label     =12;
    int max_len_filenum   = 4;
    int max_len_status    = 6;
    int len;

    for(output_find_result=output_find;
	output_find_result;
	output_find_result=output_find_result->next) {

	len=strlen(find_nicedate(output_find_result->datestamp));
	if(len>max_len_datestamp) max_len_datestamp=len;

	len=strlen(output_find_result->hostname);
	if(len>max_len_hostname) max_len_hostname=len;

	len=strlen(output_find_result->diskname);
	if(len>max_len_diskname) max_len_diskname=len;

	len=strlen(output_find_result->label);
	if(len>max_len_label) max_len_label=len;

	len=strlen(output_find_result->status);
	if(len>max_len_status) max_len_status=len;
    }

    /*
     * Since status is the rightmost field, we zap the maximum length
     * because it is not needed.  The code is left in place in case
     * another column is added later.
     */
    max_len_status = 1;

    if(output_find==NULL) {
	printf("\nNo dump to list\n");
    }
    else {
	printf("\ndate%*s host%*s disk%*s lv%*s tape or file%*s file%*s status\n",
	       max_len_datestamp-4,"",
	       max_len_hostname-4 ,"",
	       max_len_diskname-4 ,"",
	       max_len_level-2    ,"",
	       max_len_label-12   ,"",
	       max_len_filenum-4  ,"");
        for(output_find_result=output_find;
	        output_find_result;
	        output_find_result=output_find_result->next) {

	    printf("%-*s %-*s %-*s %*d %-*s %*d %-*s\n",
		    max_len_datestamp, 
			find_nicedate(output_find_result->datestamp),
		    max_len_hostname,  output_find_result->hostname,
		    max_len_diskname,  output_find_result->diskname,
		    max_len_level,     output_find_result->level,
		    max_len_label,     output_find_result->label,
		    max_len_filenum,   output_find_result->filenum,
		    max_len_status,    output_find_result->status);
	}
    }
}

void free_find_result(output_find)
find_result_t **output_find;
{
    find_result_t *output_find_result, *prev;

    prev=NULL;
    for(output_find_result=*output_find;
	    output_find_result;
	    output_find_result=output_find_result->next) {
	if(prev != NULL) amfree(prev);
	amfree(output_find_result->hostname);
	amfree(output_find_result->diskname);
	amfree(output_find_result->label);
	amfree(output_find_result->status);
	prev = output_find_result;
    }
    if(prev != NULL) amfree(prev);
    output_find = NULL;
}

int find_match(host, disk)
char *host, *disk;
{
    disk_t *dp = lookup_disk(host,disk);
    return (dp && dp->todo);
}

char *find_nicedate(datestamp)
int datestamp;
{
    static char nice[20];
    int year, month, day;

    year  = datestamp / 10000;
    month = (datestamp / 100) % 100;
    day   = datestamp % 100;

    snprintf(nice, sizeof(nice), "%4d-%02d-%02d", year, month, day);

    return nice;
}

static int parse_taper_datestamp_log(logline, datestamp, label)
char *logline;
int *datestamp;
char **label;
{
    char *s;
    int ch;

    s = logline;
    ch = *s++;

    skip_whitespace(s, ch);
    if(ch == '\0') {
	return 0;
    }
#define sc "datestamp"
    if(strncmp(s - 1, sc, sizeof(sc)-1) != 0) {
	return 0;
    }
    s += sizeof(sc)-1;
    ch = s[-1];
#undef sc

    skip_whitespace(s, ch);
    if(ch == '\0' || sscanf(s - 1, "%d", datestamp) != 1) {
	return 0;
    }
    skip_integer(s, ch);

    skip_whitespace(s, ch);
    if(ch == '\0') {
	return 0;
    }
#define sc "label"
    if(strncmp(s - 1, sc, sizeof(sc)-1) != 0) {
	return 0;
    }
    s += sizeof(sc)-1;
    ch = s[-1];
#undef sc

    skip_whitespace(s, ch);
    if(ch == '\0') {
	return 0;
    }
    *label = s - 1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';

    return 1;
}

/* if output_find is NULL					*/
/*	return 1 if this is the logfile for this label		*/
/*	return 0 if this is not the logfile for this label	*/
/* else								*/
/*	add to output_find all the dump for this label		*/
/*	return the number of dump added.			*/
int search_logfile(output_find, label, datestamp, datestamp_aux, logfile)
find_result_t **output_find;
char *label, *logfile;
int datestamp, datestamp_aux;
{
    FILE *logf;
    char *host, *host_undo;
    char *disk, *disk_undo;
    int   datestampI;
    char *rest;
    char *ck_label;
    int level, filenum, ck_datestamp, tapematch;
    int passlabel, ck_datestamp2;
    char *s;
    int ch;
    disk_t *dp;

    if((logf = fopen(logfile, "r")) == NULL)
	error("could not open logfile %s: %s", logfile, strerror(errno));

    /* check that this log file corresponds to the right tape */
    tapematch = 0;
    while(!tapematch && get_logline(logf)) {
	if(curlog == L_START && curprog == P_TAPER) {
	    if(parse_taper_datestamp_log(curstr,
					 &ck_datestamp, &ck_label) == 0) {
		printf("strange log line \"start taper %s\"\n", curstr);
	    } else if(ck_datestamp == datestamp
		      && strcmp(ck_label, label) == 0) {
		tapematch = 1;
	    }
	}
    }

    if(output_find == NULL) {
	afclose(logf);
        if(tapematch == 0)
	    return 0;
	else
	    return 1;
    }

    if(tapematch == 0) {
	afclose(logf);
	return 0;
    }

    filenum = 0;
    passlabel = 1;
    while(get_logline(logf) && passlabel) {
	if(curlog == L_SUCCESS && curprog == P_TAPER && passlabel) filenum++;
	if(curlog == L_START && curprog == P_TAPER) {
	    if(parse_taper_datestamp_log(curstr,
					 &ck_datestamp2, &ck_label) == 0) {
		printf("strange log line \"start taper %s\"\n", curstr);
	    } else if (strcmp(ck_label, label)) {
		passlabel = !passlabel;
	    }
	}
	if(curlog == L_SUCCESS || curlog == L_FAIL) {
	    s = curstr;
	    ch = *s++;

	    skip_whitespace(s, ch);
	    if(ch == '\0') {
		printf("strange log line \"%s\"\n", curstr);
		continue;
	    }
	    host = s - 1;
	    skip_non_whitespace(s, ch);
	    host_undo = s - 1;
	    *host_undo = '\0';

	    skip_whitespace(s, ch);
	    if(ch == '\0') {
		printf("strange log line \"%s\"\n", curstr);
		continue;
	    }
	    disk = s - 1;
	    skip_non_whitespace(s, ch);
	    disk_undo = s - 1;
	    *disk_undo = '\0';

	    skip_whitespace(s, ch);
	    if(ch == '\0' || sscanf(s - 1, "%d", &datestampI) != 1) {
		printf("strange log line \"%s\"\n", curstr);
		continue;
	    }
	    skip_integer(s, ch);

	    if(datestampI < 100)  { /* old log didn't have datestamp */
		level = datestampI;
		datestampI = datestamp;
	    }
	    else {
		skip_whitespace(s, ch);
		if(ch == '\0' || sscanf(s - 1, "%d", &level) != 1) {
		    printf("strange log line \"%s\"\n", curstr);
		    continue;
		}
		skip_integer(s, ch);
	    }

	    skip_whitespace(s, ch);
	    if(ch == '\0') {
		printf("strange log line \"%s\"\n", curstr);
		continue;
	    }
	    rest = s - 1;
	    if((s = strchr(s, '\n')) != NULL) {
		*s = '\0';
	    }

	    dp = lookup_disk(host,disk);
	    if ( dp == NULL ) {
		if (dynamic_disklist == 0) {
		    continue;
		}
		dp = add_disk(find_diskqp, host, disk);
		enqueue_disk(find_diskqp, dp);
	    }
	    if(find_match(host, disk)) {
		if(curprog == P_TAPER) {
		    find_result_t *new_output_find =
			(find_result_t *)alloc(sizeof(find_result_t));
		    new_output_find->next=*output_find;
		    new_output_find->datestamp=datestampI;
		    new_output_find->timestamp = alloc(15);
		    snprintf(new_output_find->timestamp, 15, "%d000000", datestampI);
		    new_output_find->datestamp_aux=datestamp_aux;
		    new_output_find->hostname=stralloc(host);
		    new_output_find->diskname=stralloc(disk);
		    new_output_find->level=level;
		    new_output_find->label=stralloc(label);
		    new_output_find->filenum=filenum;
		    if(curlog == L_SUCCESS) 
			new_output_find->status=stralloc("OK");
		    else
			new_output_find->status=stralloc(rest);
		    *output_find=new_output_find;
		}
		else if(curlog == L_FAIL) {	/* print other failures too */
		    find_result_t *new_output_find =
			(find_result_t *)alloc(sizeof(find_result_t));
		    new_output_find->next=*output_find;
		    new_output_find->datestamp=datestamp;
		    new_output_find->datestamp_aux=datestamp_aux;
		    new_output_find->timestamp = alloc(15);
		    snprintf(new_output_find->timestamp, 15, "%d000000", datestamp);
		    new_output_find->hostname=stralloc(host);
		    new_output_find->diskname=stralloc(disk);
		    new_output_find->level=level;
		    new_output_find->label=stralloc("---");
		    new_output_find->filenum=0;
		    new_output_find->status=vstralloc(
			 "FAILED (",
			 program_str[(int)curprog],
			 ") ",
			 rest,
			 NULL);
		    *output_find=new_output_find;
		}
	    }
	}
    }
    afclose(logf);
    return 1;
}

find_result_t *dump_exist(output_find, hostname, diskname, datestamp, level)
find_result_t *output_find;
char *hostname;
char *diskname;
int datestamp;
int level;
{
    find_result_t *output_find_result;

    for(output_find_result=output_find;
	output_find_result;
	output_find_result=output_find_result->next) {
	if( !strcmp(output_find_result->hostname, hostname) &&
	    !strcmp(output_find_result->diskname, diskname) &&
	    output_find_result->datestamp == datestamp &&
	    output_find_result->level == level) {

	    return output_find_result;
	}
    }
    return(NULL);
}
