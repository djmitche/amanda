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
 * $Id: amadmin.c,v 1.16 1997/11/25 08:17:56 george Exp $
 *
 * controlling process for the Amanda backup system
 */
#include "amanda.h"
#include "conffile.h"
#include "diskfile.h"
#include "tapefile.h"
#include "infofile.h"
#include "logfile.h"
#include "version.h"
#include "holding.h"

char *pname = "amadmin";
disklist_t *diskqp;

struct find_result {
    struct find_result *next;
    char *datestamp;
    char *hostname;
    char *diskname;
    int level;
    char *label;
    int  filenum;
    char *status;
};
struct find_result *output_find=NULL;

int main P((int argc, char **argv));
void usage P((void));
void force P((int argc, char **argv));
void force_one P((disk_t *dp));
void unforce P((int argc, char **argv));
void unforce_one P((disk_t *dp));
void info P((int argc, char **argv));
void info_one P((disk_t *dp));
void find P((int argc, char **argv));
void delete P((int argc, char **argv));
void delete_one P((disk_t *dp));
void balance P((void));
void tape P((void));
void bumpsize P((void));
void diskloop P((int argc, char **argv, char *cmdname,
		 void (*func) P((disk_t *dp))));
char *seqdatestr P((int seq));
static int next_level0 P((disk_t *dp, info_t *ip));
void find P((int argc, char **argv));
int find_match P((char *host, char *disk));
char *nicedate P((int datestamp));
int bump_thresh P((int level));
int search_logfile P((char *label, int datestamp, char *logfile));
int get_logline P((FILE *logf));
void export_db P((int argc, char **argv));
void import_db P((int argc, char **argv));
void disklist P((int argc, char **argv));
void disklist_one P((disk_t *dp));
void sort_find_result();
void print_find_result();
void search_holding_disk();

int main(argc, argv)
int argc;
char **argv;
{
    char confdir[256];

    erroutput_type = ERR_INTERACTIVE;

    if(argc < 3) usage();

    if(!strcmp(argv[2],"version")) {
	for(argc=0; version_info[argc]; printf("%s",version_info[argc++]));
	return 0;
    }
    sprintf(confdir, "%s/%s", CONFIG_DIR, argv[1]);
    if(chdir(confdir)) {
	fprintf(stderr,"%s: could not find config dir %s\n", argv[0], confdir);
	usage();
    }

    if(read_conffile(CONFFILE_NAME))
	error("could not find \"%s\" in this directory.\n", CONFFILE_NAME);

    if((diskqp = read_diskfile(getconf_str(CNF_DISKFILE))) == NULL)
	error("could not load \"%s\"\n", getconf_str(CNF_DISKFILE));

    if(read_tapelist(getconf_str(CNF_TAPELIST)))
	error("could not load \"%s\"\n", getconf_str(CNF_TAPELIST));

    if(open_infofile(getconf_str(CNF_INFOFILE)))
	error("could not open info db \"%s\"\n", getconf_str(CNF_INFOFILE));

    if(!strcmp(argv[2],"force")) force(argc, argv);
    else if(!strcmp(argv[2],"unforce")) unforce(argc, argv);
    else if(!strcmp(argv[2],"info")) info(argc, argv);
    else if(!strcmp(argv[2],"find")) find(argc, argv);
    else if(!strcmp(argv[2],"delete")) delete(argc, argv);
    else if(!strcmp(argv[2],"balance")) balance();
    else if(!strcmp(argv[2],"tape")) tape();
    else if(!strcmp(argv[2],"bumpsize")) bumpsize();
    else if(!strcmp(argv[2],"import")) import_db(argc, argv);
    else if(!strcmp(argv[2],"export")) export_db(argc, argv);
    else if(!strcmp(argv[2],"disklist")) disklist(argc, argv);
    else {
	fprintf(stderr, "%s: unknown command \"%s\"\n", argv[0], argv[2]);
	usage();
    }
    close_infofile();
    return 0;
}


void usage P((void))
{
    fprintf(stderr, "\nUsage: %s%s <conf> <command> {<args>} ...\n", pname,
	    versionsuffix());
    fprintf(stderr, "    Valid <command>s are:\n");
    fprintf(stderr,"\tversion\t\t\t\t# Show version info.\n");
    fprintf(stderr,
	    "\tforce <hostname> <disks> ...\t# Force level 0 tonight.\n");
    fprintf(stderr,
	    "\tunforce <hostname> <disks> ...\t# Clear force command.\n");
    fprintf(stderr,
	    "\tfind <hostname> <disks> ...\t# Show which tapes these dumps are on.\n");
    fprintf(stderr,
	    "\tdelete <hostname> <disks> ...\t# Delete from database.\n");
    fprintf(stderr,
	    "\tinfo <hostname> <disks> ...\t# Show current info records.\n");
    fprintf(stderr,
	    "\tbalance\t\t\t\t# Show nightly dump size balance.\n");
    fprintf(stderr,
	    "\ttape\t\t\t\t# Show which tape is due next.\n");
    fprintf(stderr,
	    "\tbumpsize\t\t\t# Show current bump thresholds.\n");
    fprintf(stderr,
	    "\texport [<hostname> [<disks>]]\t# Export curinfo database to stdout.\n");
    fprintf(stderr,
	    "\timport\t\t\t\t# Import curinfo database from stdin.\n");
/*  fprintf(stderr,
**	    "\tdisklist [<hostname> [<disks> ...]]\t# Debug disklist entries.\n");
*/
    exit(1);
}


/* ----------------------------------------------- */

void diskloop(argc, argv, cmdname, func)
int argc;
char **argv;
char *cmdname;
void (*func) P((disk_t *dp));
{
    host_t *hp;
    disk_t *dp;
    char *diskname;
    int count;

    if(argc < 4) {
	fprintf(stderr,"%s: expecting \"%s <hostname> {<disks> ...}\"\n",
		argv[0], cmdname);
	usage();
    }

    if((hp = lookup_host(argv[3])) == NULL) {
	fprintf(stderr, "%s: host %s not in current disklist database.\n",
		argv[0], argv[3]);
	exit(1);
    }
    if(argc < 5) {
	for(dp = hp->disks; dp != NULL; dp = dp->hostnext)
	    func(dp);
    }
    else {
	for(argc -= 4, argv += 4; argc; argc--, argv++) {
	    count = 0;
	    diskname = *argv;
	    for(dp = hp->disks; dp != NULL; dp = dp->hostnext) {
		if(match(diskname, dp->name)) {
		    count++;
		    func(dp);
		}
	    }
	    if(count == 0)
		fprintf(stderr, "%s: host %s has no disks that match \"%s\"\n",
			argv[0], hp->hostname, diskname);
	}
    }
}

/* ----------------------------------------------- */


void force_one(dp)
disk_t *dp;
{
    char *hostname = dp->host->hostname;
    char *diskname = dp->name;
    info_t inf;

    get_info(hostname, diskname, &inf);
    inf.command = PLANNER_FORCE;
    put_info(hostname, diskname, &inf);
    printf("%s: %s:%s is set to a forced level 0 tonight.\n",
	   pname, hostname, diskname);
}


void force(argc, argv)
int argc;
char **argv;
{
    diskloop(argc, argv, "force", force_one);
}


/* ----------------------------------------------- */


void unforce_one(dp)
disk_t *dp;
{
    char *hostname = dp->host->hostname;
    char *diskname = dp->name;
    info_t inf;

    get_info(hostname, diskname, &inf);
    if(inf.command == PLANNER_FORCE) {
	inf.command = NO_COMMAND;
	put_info(hostname, diskname, &inf);
	printf("%s: force command for %s:%s cleared.\n",
	       pname, hostname, diskname);
    }
    else {
	printf("%s: no force command outstanding for %s:%s, unchanged.\n",
	       pname, hostname, diskname);
    }
}

void unforce(argc, argv)
int argc;
char **argv;
{
    diskloop(argc, argv, "unforce", unforce_one);
}


/* ----------------------------------------------- */

static int deleted;

void delete_one(dp)
disk_t *dp;
{
    char *hostname = dp->host->hostname;
    char *diskname = dp->name;
    info_t inf;

    if(get_info(hostname, diskname, &inf)) {
	printf("%s: %s:%s NOT currently in database.\n",
	       pname, hostname, diskname);
	return;
    }

    deleted++;
    if(del_info(hostname, diskname))
	error("couldn't delete %s:%s from database: %s",
	      hostname, diskname, strerror(errno));
    else
	printf("%s: %s:%s deleted from curinfo database.\n",
	       pname, hostname, diskname);
}

void delete(argc, argv)
int argc;
char **argv;
{
    deleted = 0;
    diskloop(argc, argv, "delete", delete_one);

   if(deleted)
	printf(
	 "%s: NOTE: you'll have to remove these from the disklist yourself.\n",
	 pname);
}

/* ----------------------------------------------- */

void info_one(dp)
disk_t *dp;
{
    info_t inf;
    int lev, lev0date;
    struct tm *tm;
    stats_t *sp;

    get_info(dp->host->hostname, dp->name, &inf);

    printf("\nCurrent info for %s %s:\n", dp->host->hostname, dp->name);
    if(inf.command) printf("  (Forcing to level 0 dump on next run)\n");
    printf("  Stats: dump rates (kps), Full:  %5.1f, %5.1f, %5.1f\n",
	   inf.full.rate[0], inf.full.rate[1], inf.full.rate[2]);
    printf("                    Incremental:  %5.1f, %5.1f, %5.1f\n",
	   inf.incr.rate[0], inf.incr.rate[1], inf.incr.rate[2]);
    printf("          compressed size, Full: %5.1f%%,%5.1f%%,%5.1f%%\n",
	   inf.full.comp[0]*100, inf.full.comp[1]*100, inf.full.comp[2]*100);
    printf("                    Incremental: %5.1f%%,%5.1f%%,%5.1f%%\n",
	   inf.incr.comp[0]*100, inf.incr.comp[1]*100, inf.incr.comp[2]*100);

    printf("  Dumps: lev datestmp  tape             file   origK   compK secs\n");
    lev0date = inf.inf[0].date;
    for(lev = 0, sp = &inf.inf[0]; lev < 9; lev++, sp++) {
	if(sp->date < (time_t)0 && sp->label[0] == '\0') continue;
	tm = localtime(&sp->date);
	printf("          %d  %04d%02d%02d  %-15s  %4d %7ld %7ld %4ld\n",
	       lev, tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
	       sp->label, sp->filenum, sp->size, sp->csize, sp->secs);
    }
}


void info(argc, argv)
int argc;
char **argv;
{
    diskloop(argc, argv, "info", info_one);
}

/* ----------------------------------------------- */

void tape()
{
    tape_t *tp;
    int runtapes,tapecycle,i;

    runtapes = getconf_int(CNF_RUNTAPES);
    tapecycle = getconf_int(CNF_TAPECYCLE);

    for ( i=0 ; i < runtapes ; i++ ) {
	printf("The next Amanda run should go onto ");

	if((tp = lookup_tapepos(tapecycle)) != NULL)
	    printf("tape %s or ", tp->label);
	printf("a new tape.\n");
	tapecycle--;
    }
}

/* ----------------------------------------------- */

#define SECS_PER_DAY (24*60*60)
time_t today;
int runtapes, dumpcycle;

char *seqdatestr(seq)
int seq;
{
    static char str[16];
    static char *dow[7] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    time_t t = today + seq*SECS_PER_DAY;
    struct tm *tm;

    tm = localtime(&t);

    sprintf(str, "%2d/%02d %3s", tm->tm_mon+1, tm->tm_mday, dow[tm->tm_wday]);
    return str;
}


/* when is next level 0 due? 0 = tonight, 1 = tommorrow, etc*/
static int next_level0(dp, ip)
disk_t *dp;
info_t *ip;
{
    if(dp->strategy == DS_NOFULL)
	return 1;	/* fake it */
    else if(ip->inf[0].date < (time_t)0)
	return 0;	/* new disk */
    else
	return dp->dumpcycle - days_diff(ip->inf[0].date, today);
}


void balance()
{
    disk_t *dp;
    struct balance_stats {
	int disks;
	long origsize, outsize;
    } *sp;
    int seq, max_seq, total, balanced, runs_per_cycle, overdue, max_overdue;
    info_t inf;

    total = getconf_int(CNF_TAPECYCLE);
    max_seq = getconf_int(CNF_DUMPCYCLE)-1;	/* print at least this many */
    time(&today);
    runtapes = getconf_int(CNF_RUNTAPES);
    dumpcycle = getconf_int(CNF_DUMPCYCLE);
    overdue = 0;
    max_overdue = 0;

    runs_per_cycle = guess_runs_from_tapelist();

    sp = (struct balance_stats *)
	alloc(sizeof(struct balance_stats) * (total+1));

    for(seq=0; seq <= total; seq++)
	sp[seq].disks = sp[seq].origsize = sp[seq].outsize = 0;

    for(dp = diskqp->head; dp != NULL; dp = dp->next) {
	if(get_info(dp->host->hostname, dp->name, &inf)) {
	    printf("new disk %s:%s ignored.\n", dp->host->hostname, dp->name);
	    continue;
	}
	seq = next_level0(dp, &inf);
	if(seq > max_seq) max_seq = seq;
	if(seq < 0) {
	    overdue++;
	    if (-seq > max_overdue)
		max_overdue = -seq;
	    seq = 0;
	}
	if(seq >= total)
	    error("bogus seq number %d for %s:%s", seq,
		  dp->host->hostname, dp->name);

	sp[seq].disks++;
	sp[seq].origsize += inf.inf[0].size;
	sp[seq].outsize += inf.inf[0].csize;

	sp[total].disks++;
	sp[total].origsize += inf.inf[0].size;
	sp[total].outsize += inf.inf[0].csize;
    }

    if(sp[total].outsize == 0) {
	printf("\nNo data to report on yet.\n");
	return;
    }

    balanced = sp[total].outsize / runs_per_cycle;

    printf("\n due-date  #fs   orig KB    out KB  balance\n");
    printf("-------------------------------------------\n");
    for(seq = 0; seq <= max_seq; seq++) {
	printf("%-9.9s  %3d %9ld %9ld ",
	       seqdatestr(seq), sp[seq].disks,
	       sp[seq].origsize, sp[seq].outsize);
	if(!sp[seq].outsize) printf("    --- \n");
	else printf("%+7.1f%%\n",
		    (sp[seq].outsize-balanced)*100.0/(double)balanced);
    }
    printf("-------------------------------------------\n");
    printf("TOTAL      %3d %9ld %9ld %8d", sp[total].disks,
	   sp[total].origsize, sp[total].outsize, balanced);
    printf("  (estimated %d runs per dumpcycle)\n", runs_per_cycle);
    if (overdue) {
	printf(" (%d filesystems overdue, the most being overdue %d days)\n",
	       overdue, max_overdue);
    }
}


/* ----------------------------------------------- */

#define MAX_LINE 2048


typedef enum program_e {
    P_UNKNOWN, P_PLANNER, P_DRIVER, P_REPORTER, P_DUMPER, P_TAPER, P_AMFLUSH
} program_t;
#define P_LAST P_AMFLUSH

char *program_str[] = {
    "UNKNOWN", "planner", "driver", "reporter", "dumper", "taper", "amflush"
};

typedef struct line_s {
    struct line_s *next;
    char *str;
} line_t;

int curlinenum;
logtype_t curlog;
program_t curprog;
char *curstr;
char logline[MAX_LINE];

char *find_hostname;
char **find_diskstrs;
int  find_ndisks;
int  find_nhosts;
char sort_order[10]="hkdlb";

void find(argc, argv)
int argc;
char **argv;
{
    char *conflog, logfile[1024];
    host_t *hp;
    int tape, maxtape, seq, logs;
    tape_t *tp;
    int start_argc;

    if(argc < 3) {
	fprintf(stderr,
		"%s: expecting \"find [--sort <hkdlb>] [hostname [<disk> ...]]\"\n", pname);
	usage();
    }

	
    if(argc > 3 && !strcmp(argv[3],"--sort")) {
	int i, valid_sort=1;
	argv[4][9]=0; /* trunc */
	for(i=strlen(argv[4])-1;i>=0;i--) {
	    switch (argv[4][i]) {
	    case 'h':
	    case 'H':
	    case 'k':
	    case 'K':
	    case 'd':
	    case 'D':
	    case 'l':
	    case 'L':
	    case 'b':
	    case 'B':
		    break;
	    default: valid_sort=0;
	    }
	}
	if(valid_sort)
	    strcpy(sort_order,argv[4]);
	else
	    printf("Invalid sort order: %s\nUse default sort order: %s\n",
		    argv[4],sort_order);
	start_argc=6;
    } else {
	start_argc=4;
    }

    if(argc < start_argc)
	find_nhosts = 0;
    else {
	find_nhosts = 1;
        find_hostname = argv[start_argc-1];
        if((hp = lookup_host(find_hostname)) == NULL)
	    printf("Warning: host %s not in disklist.\n", find_hostname);
        else
	    find_hostname = hp->hostname;
    }
    find_diskstrs = &argv[start_argc];
    find_ndisks = argc - start_argc;


    conflog = getconf_str(CNF_LOGFILE);
    maxtape = getconf_int(CNF_TAPECYCLE);

    for(tape = 1; tape <= maxtape; tape++) {
	tp = lookup_tapepos(tape);
	if(tp == NULL) continue;

	/* search log files */

	logs = 0;

	/* new-style log.<date>.<seq> */

	for(seq = 0; 1; seq++) {
	    sprintf(logfile, "%s.%d.%d", conflog, tp->datestamp, seq);
	    if(access(logfile, R_OK) != 0) break;
	    logs += search_logfile(tp->label, tp->datestamp, logfile);
	}

	/* search old-style amflush log, if any */

	sprintf(logfile, "%s.%d.amflush", conflog, tp->datestamp);
	if(access(logfile,R_OK) == 0) {
	    logs += search_logfile(tp->label, tp->datestamp, logfile);
	}

	/* search old-style main log, if any */

	sprintf(logfile, "%s.%d", conflog, tp->datestamp);
	if(access(logfile,R_OK) == 0) {
	    logs += search_logfile(tp->label, tp->datestamp, logfile);
	}
	if(logs == 0)
	    printf("Warning: no log files found for tape %s written %s\n",
		   tp->label, nicedate(tp->datestamp));
    }

    search_holding_disk();

    sort_find_result();

    print_find_result();
}

void search_holding_disk()
{
    holdingdisk_t *hdisk;
    struct dirname *dir;
    char sdirname[80], destname[128], hostname[256], diskname[80];
    DIR *workdir;
    struct dirent *entry;
    int level;
    disk_t *dp;
	
    for(hdisk = holdingdisks; hdisk != NULL; hdisk = hdisk->next)
	scan_holdingdisk(hdisk->diskdir,0);

    for(hdisk = holdingdisks; hdisk != NULL; hdisk = hdisk->next) {
	for(dir = dir_list; dir != NULL; dir = dir->next) {
            sprintf(sdirname, "%s/%s", hdisk->diskdir,dir->name);
	    if((workdir = opendir(sdirname)) == NULL) {
	        continue;
	    }

	    chdir(sdirname);
	    while((entry = readdir(workdir)) != NULL) {
		if(!strcmp(entry->d_name, ".") ||  !strcmp(entry->d_name, ".."))
		    continue;
		if(is_emptyfile(entry->d_name))
		    continue;
		sprintf(destname, "%s/%s", sdirname, entry->d_name);
		if(get_amanda_names(destname, hostname, diskname, &level))
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
		if ( dp == NULL )
		    continue;
		if(level < 0 || level > 9)
		    continue;
		    
		if(find_match(hostname,diskname)) {
		    struct find_result *new_output_find=
			alloc(sizeof(struct find_result));
		    new_output_find->next=output_find;
		    new_output_find->datestamp=stralloc(nicedate(atoi(dir->name)));
		    new_output_find->hostname=stralloc(hostname);
		    new_output_find->diskname=stralloc(diskname);
		    new_output_find->level=level;
		    new_output_find->label=stralloc(destname);
		    new_output_find->filenum=0;
		    new_output_find->status=stralloc("OK");
		    output_find=new_output_find;
		}
	    }
	    closedir(workdir);
	}	
    }
}

int find_compare(const void *i1, const void *j1)
{
    int compare=0;
    struct find_result **i = (struct find_result **)i1;
    struct find_result **j = (struct find_result **)j1;

    int nb_compare=strlen(sort_order);
    int k;

    for(k=0;k<nb_compare;k++) {
	switch (sort_order[k]) {
	case 'h' : compare=strcmp((*i)->hostname,(*j)->hostname);
		   break;
	case 'H' : compare=strcmp((*j)->hostname,(*i)->hostname);
		   break;
	case 'k' : compare=strcmp((*i)->diskname,(*j)->diskname);
		   break;
	case 'K' : compare=strcmp((*j)->diskname,(*i)->diskname);
		   break;
	case 'd' : compare=strcmp((*i)->datestamp,(*j)->datestamp);
		   break;
	case 'D' : compare=strcmp((*j)->datestamp,(*i)->datestamp);
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

void sort_find_result()
{
    struct find_result **array_find_result;
    struct find_result *output_find_result;
    int nb_result=0;
    int no_result;

    /* qsort core dump if nothing to sort */
    if(output_find==NULL)
	return;

    /* How many result */
    for(output_find_result=output_find;
	output_find_result;
	output_find_result=output_find_result->next) {
	nb_result++;
    }

    /* put the list in an array */
    array_find_result=alloc(nb_result * sizeof(struct find_result *));
    for(output_find_result=output_find,no_result=0;
	output_find_result;
	output_find_result=output_find_result->next,no_result++) {
	array_find_result[no_result]=output_find_result;
    }

    /* sort the array */
    qsort(array_find_result,nb_result,sizeof(struct find_result *),
	  find_compare);

    /* put the sorted result in the list */
    for(no_result=0;
	no_result<nb_result-1; no_result++) {
	array_find_result[no_result]->next = array_find_result[no_result+1];
    }
    array_find_result[nb_result-1]->next=NULL;
    output_find=array_find_result[0];

}

void print_find_result()
{
    struct find_result *output_find_result;
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

	len=strlen(output_find_result->datestamp);
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
		    max_len_datestamp, output_find_result->datestamp,
		    max_len_hostname,  output_find_result->hostname,
		    max_len_diskname,  output_find_result->diskname,
		    max_len_level,     output_find_result->level,
		    max_len_label,     output_find_result->label,
		    max_len_filenum,   output_find_result->filenum,
		    max_len_status,    output_find_result->status);
	}
    }
}

int find_match(host, disk)
char *host, *disk;
{
    int d;

    if(find_nhosts == 0) return 1;
    if(strcmp(host, find_hostname)) return 0;
    if(find_ndisks == 0) return 1;

    for(d = 0; d < find_ndisks; d++) {
	if(match(find_diskstrs[d], disk))
	    return 1;
    }
    return 0;
}

char *nicedate(datestamp)
int datestamp;
{
    static char nice[20];
    int year, month, day;

    year  = datestamp / 10000;
    month = (datestamp / 100) % 100;
    day   = datestamp % 100;

    sprintf(nice, "%4d-%02d-%02d", year, month, day);

    return nice;
}

int search_logfile(label, datestamp, logfile)
char *label, *logfile;
int datestamp;
{
    FILE *logf;
    char host[80], disk[80], ck_label[80], rest[MAX_LINE];
    int level, rc, filenum, ck_datestamp, tapematch;
    int passlabel, ck_datestamp2;

    if((logf = fopen(logfile, "r")) == NULL)
	error("could not open logfile %s: %s", logfile, strerror(errno));

    /* check that this log file corresponds to the right tape */
    tapematch = 0;
    while(!tapematch && get_logline(logf)) {
	if(curlog == L_START && curprog == P_TAPER) {
	    rc = sscanf(curstr, " datestamp %d label %s",
			&ck_datestamp, ck_label);
	    if(rc != 2)
		printf("strange log line \"start taper %s\"\n", curstr);
	    else if(ck_datestamp == datestamp && !strcmp(ck_label,label))
		tapematch = 1;
	}
    }

    if(tapematch == 0) {
	fclose(logf);
	return 0;
    }

    filenum = 0;
    passlabel = 1;
    while(get_logline(logf) && passlabel) {
	if(curlog == L_SUCCESS && curprog == P_TAPER && passlabel) filenum++;
	if(curlog == L_START && curprog == P_TAPER) {
	    rc = sscanf(curstr, " datestamp %d label %s",
			&ck_datestamp2, ck_label);
	    if(rc != 2)
		printf("strange log line \"start taper %s\"\n", curstr);
	    else
		if (strcmp(ck_label,label))
		    passlabel = !passlabel;
	}
	if(curlog == L_SUCCESS || curlog == L_FAIL) {
	    rc =sscanf(curstr,"%s %s %d %[^\n]", host, disk, &level, rest);
	    if(rc != 4) {
		printf("strange log line \"%s\"\n", curstr);
		continue;
	    }
	    if(find_match(host, disk)) {
		if(curprog == P_TAPER) {
		    struct find_result *new_output_find=
			alloc(sizeof(struct find_result));
		    new_output_find->next=output_find;
		    new_output_find->datestamp=stralloc(nicedate(datestamp));
		    new_output_find->hostname=stralloc(host);
		    new_output_find->diskname=stralloc(disk);
		    new_output_find->level=level;
		    new_output_find->label=stralloc(label);
		    new_output_find->filenum=filenum;
		    if(curlog == L_SUCCESS) 
			new_output_find->status=stralloc("OK");
		    else
			new_output_find->status=stralloc(rest);
		    output_find=new_output_find;
		}
		else if(curlog == L_FAIL) {	/* print other failures too */
		    struct find_result *new_output_find=
			alloc(sizeof(struct find_result));
		    new_output_find->next=output_find;
		    new_output_find->datestamp=stralloc(nicedate(datestamp));
		    new_output_find->hostname=stralloc(host);
		    new_output_find->diskname=stralloc(disk);
		    new_output_find->level=level;
		    new_output_find->label=stralloc("---");
		    new_output_find->filenum=0;
		    new_output_find->status=(char*)alloc(11+strlen(program_str[(int)curprog])+strlen(rest));
		    sprintf(new_output_find->status,"FAILED (%s) %s", 
			    program_str[(int)curprog], rest);
		    output_find=new_output_find;
		}
	    }
	}
    }
    fclose(logf);
    return 1;
}


/* shared code with reporter.c -- should generalize into log-reading library */

int get_logline(logf)
FILE *logf;
{
    char *cp, *logstr, *progstr;

    if(fgets(logline, MAX_LINE, logf) == NULL)
	return 0;
    curlinenum++;
    cp = logline;

    /* continuation lines are special */

    if(*cp == ' ') {
	curlog = L_CONT;
	/* curprog stays the same */
	curstr = cp + 2;
	return 1;
    }

    /* isolate logtype field */

    logstr = cp;
    while(*cp && *cp != ' ') cp++;
    if(*cp) *cp++ = '\0';

    /* isolate program name field */

    while(*cp == ' ') cp++;	/* skip blanks */
    progstr = cp;
    while(*cp && *cp != ' ') cp++;
    if(*cp) *cp++ = '\0';

    /* rest of line is logtype dependent string */

    curstr = cp;

    /* lookup strings */

    for(curlog = L_MARKER; curlog != L_BOGUS; curlog--)
	if(!strcmp(logtype_str[curlog], logstr)) break;

    for(curprog = P_LAST; curprog != P_UNKNOWN; curprog--)
	if(!strcmp(program_str[curprog], progstr)) break;

    return 1;
}

/* ------------------------ */


/* shared code with planner.c */

int bump_thresh(level)
int level;
{
    int bump = getconf_int(CNF_BUMPSIZE);
    double mult = getconf_real(CNF_BUMPMULT);

    while(--level) bump = (int) bump * mult;
    return bump;
}

void bumpsize()
{
    int l;

    printf("Current bump parameters:\n");
    printf("  bumpsize %5d KB\t- minimum savings (threshold) to bump level 1 -> 2\n",
	   getconf_int(CNF_BUMPSIZE));
    printf("  bumpdays %5d\t- minimum days at each level\n",
	   getconf_int(CNF_BUMPDAYS));
    printf("  bumpmult %5.5g\t- threshold = bumpsize * (level-1)**bumpmult\n\n",
	   getconf_real(CNF_BUMPMULT));

    printf("      Bump -> To  Threshold\n");
    for(l = 1; l < 9; l++)
	printf("\t%d  ->  %d  %9d KB\n", l, l+1, bump_thresh(l));
    putchar('\n');
}

/* ----------------------------------------------- */

void export_one P((disk_t *dp));

void export_db(argc, argv)
int argc;
char **argv;
{
    disk_t *dp;
    time_t curtime;
    char hostname[MAX_HOSTNAME_LENGTH];
    int i;

    printf("CURINFO Version %s CONF %s\n", version(), getconf_str(CNF_ORG));

    curtime = time(0);
    hostname[sizeof(hostname)-1] = '\0';
    gethostname(hostname, sizeof(hostname)-1);
    printf("# Generated by:\n#    host: %s\n#    date: %s",
	   hostname, ctime(&curtime));

    printf("#    command:");
    for(i = 0; i < argc; i++)
	printf(" %s", argv[i]);

    printf("\n# This file can be merged back in with \"amadmin import\".\n");
    printf("# Edit only with care.\n");

    if(argc >= 4)
	diskloop(argc, argv, "export", export_one);
    else for(dp = diskqp->head; dp != NULL; dp = dp->next)
	export_one(dp);
}

void export_one(dp)
disk_t *dp;
{
    info_t info;
    int i,l;

    if(get_info(dp->host->hostname, dp->name, &info)) {
	fprintf(stderr, "Warning: no curinfo record for %s:%s\n",
		dp->host->hostname, dp->name);
	return;
    }
    printf("host: %s\ndisk: %s\n", dp->host->hostname, dp->name);
    printf("command: %d\n", info.command);
    printf("full-rate:");
    for(i=0;i<AVG_COUNT;i++) printf(" %f", info.full.rate[i]);
    printf("\nfull-comp:");
    for(i=0;i<AVG_COUNT;i++) printf(" %f", info.full.comp[i]);

    printf("\nincr-rate:");
    for(i=0;i<AVG_COUNT;i++) printf(" %f", info.incr.rate[i]);
    printf("\nincr-comp:");
    for(i=0;i<AVG_COUNT;i++) printf(" %f", info.incr.comp[i]);
    printf("\n");
    for(l=0;l<DUMP_LEVELS;l++) {
	if(info.inf[l].date < (time_t)0 && info.inf[l].label[0] == '\0') continue;
	printf("stats: %d %ld %ld %ld %ld %d %s\n", l,
	       info.inf[l].size, info.inf[l].csize, info.inf[l].secs,
	       (long)info.inf[l].date, info.inf[l].filenum,
	       info.inf[l].label);
    }
    printf("//\n");
}

/* ----------------------------------------------- */

char line[1024];

int import_one P((void));
int impget_line P((void));

void import_db(argc, argv)
int argc;
char **argv;
{
    int vers_maj, vers_min, vers_patch, newer;
    char org[256], vers_comment[256];

    /* process header line */

    fgets(line, 1024, stdin);
    if(sscanf(line, "CURINFO Version %d.%d.%d%s CONF %[^\n]\n",
	      &vers_maj, &vers_min, &vers_patch, vers_comment, org) != 5) {
	fprintf(stderr, "%s: bad CURINFO header line in input.\n", pname);
	fprintf(stderr, "    Was the input in \"amadmin export\" format?\n");
	return;
    }

    newer = (vers_maj != VERSION_MAJOR)? vers_maj > VERSION_MAJOR :
	    (vers_min != VERSION_MINOR)? vers_min > VERSION_MINOR :
					 vers_patch > VERSION_PATCH;
    if(newer)
	fprintf(stderr,
	     "%s: WARNING: input is from newer Amanda version: %d.%d.%d.\n",
		pname, vers_maj, vers_min, vers_patch);

    if(strcmp(org, getconf_str(CNF_ORG)))
	fprintf(stderr, "%s: WARNING: input is from different org: %s\n",
		pname, org);

    while(import_one());
}


int import_one P((void))
{
    info_t info;
    stats_t onestat;
    int rc, level;
    long onedate;

    char hostname[256];
    char diskname[256];

    memset(&info, 0, sizeof(info_t));

    for(level = 0; level < DUMP_LEVELS; level++) {
        info.inf[level].date = (time_t)-1;
    }

    /* get host: disk: command: lines */

    if(!impget_line()) return 0;	/* nothing there */
    if(sscanf(line, "host: %255[^\n]", hostname) != 1) goto parse_err;

    if(!impget_line()) goto shortfile_err;
    if(sscanf(line, "disk: %255[^\n]", diskname) != 1) goto parse_err;

    if(!impget_line()) goto shortfile_err;
    if(sscanf(line, "command: %d", &info.command) != 1) goto parse_err;

    /* get rate: and comp: lines for full dumps */

    if(!impget_line()) goto shortfile_err;
    rc = sscanf(line, "full-rate: %f %f %f",
		&info.full.rate[0], &info.full.rate[1], &info.full.rate[2]);
    if(rc != 3) goto parse_err;

    if(!impget_line()) goto shortfile_err;
    rc = sscanf(line, "full-comp: %f %f %f",
		&info.full.comp[0], &info.full.comp[1], &info.full.comp[2]);
    if(rc != 3) goto parse_err;

    /* get rate: and comp: lines for incr dumps */

    if(!impget_line()) goto shortfile_err;
    rc = sscanf(line, "incr-rate: %f %f %f",
		&info.incr.rate[0], &info.incr.rate[1], &info.incr.rate[2]);
    if(rc != 3) goto parse_err;

    if(!impget_line()) goto shortfile_err;
    rc = sscanf(line, "incr-comp: %f %f %f",
		&info.incr.comp[0], &info.incr.comp[1], &info.incr.comp[2]);
    if(rc != 3) goto parse_err;

    /* get stats for dump levels */

    while(1) {
	if(!impget_line()) goto shortfile_err;
	if(!strncmp(line, "//", 2)) {
	    /* end of record */
	    break;
	}
	memset(&onestat, 0, sizeof(onestat));
	rc = sscanf(line, "stats: %d %ld %ld %ld %ld %d %80[^\n]",
		    &level, &onestat.size, &onestat.csize, &onestat.secs,
		    &onedate, &onestat.filenum, onestat.label);
	if(rc < 5 || rc > 7) goto parse_err;

	/* time_t not guarranteed to be long */
	onestat.date = onedate;
	if(level < 0 || level > 9) goto parse_err;

	info.inf[level] = onestat;
    }

    /* got a full record, now write it out to the database */

    if(put_info(hostname, diskname, &info)) {
	fprintf(stderr, "%s: error writing record for %s:%s\n",
		pname, hostname, diskname);
    }
    return 1;

 parse_err:
    fprintf(stderr, "%s: parse error reading import record.\n", pname);
    return 0;

 shortfile_err:
    fprintf(stderr, "%s: short file reading import record.\n", pname);
    return 0;
}

int impget_line P((void))
{
    char *p;

    while(1) {

	if(!fgets(line, 1024, stdin)) {
	    /* EOF or error */
	    if(ferror(stdin))
		fprintf(stderr, "%s: reading stdin: %s\n",
			pname, strerror(errno));
	    return 0;
	}

	if(*line == '#') {
	    /* ignore comment lines */
	    continue;
	}

	/* find first non-blank */

	for(p = line; isspace(*p); p++);
	if(*p) {
	    /* found non-blank, return line */
	    return 1;
	}
	/* otherwise, a blank line, so keep going */
    }
}

/* ----------------------------------------------- */

void disklist_one(dp)
disk_t *dp;
{
    host_t *hp;
    interface_t *ip;

    hp = dp->host;
    ip = hp->netif;

    printf("line %d:\n", dp->line);

    printf("    host %s:\n", hp->hostname);
    printf("        interface %s\n",
	   ip->name[0] ? ip->name : "default");

    printf("    disk %s:\n", dp->name);

    printf("        program \"%s\"\n", dp->program);
    if(dp->exclude != (char *)0)
	printf("        exclude %s\"%s\"\n", dp->exclude_list? "list ":"", dp->exclude);
    printf("        priority %ld\n", dp->priority);
    printf("        dumpcycle %ld\n", dp->dumpcycle);
    printf("        maxdumps %d\n", dp->maxdumps);

    printf("        strategy ");
    switch(dp->strategy) {
    case DS_SKIP:
	printf("SKIP\n");
	break;
    case DS_STANDARD:
	printf("STANDARD\n");
	break;
    case DS_NOFULL:
	printf("NOFULL\n");
	break;
    case DS_NOINC:
	printf("NOINC\n");
	break;
    case DS_HANOI:
	printf("HANOI\n");
	break;
    }

    printf("        compress ");
    switch(dp->compress) {
    case COMP_NONE:
	printf("NONE\n");
	break;
    case COMP_FAST:
	printf("CLIENT FAST\n");
	break;
    case COMP_BEST:
	printf("CLIENT BEST\n");
	break;
    case COMP_SERV_FAST:
	printf("SERVER FAST\n");
	break;
    case COMP_SERV_BEST:
	printf("SERVER BEST\n");
	break;
    }
    if(dp->compress != COMP_NONE) {
	printf("        comprate %.2f %.2f\n",
	       dp->comprate[0], dp->comprate[1]);
    }

    printf("        auth ");
    switch(dp->auth) {
    case AUTH_BSD:
	printf("BSD\n");
	break;
    case AUTH_KRB4:
	printf("KRB4\n");
	break;
    }
    printf("        kencrypt %s\n", (dp->kencrypt? "YES" : "NO"));

    printf("        holdingdisk %s\n", (!dp->no_hold? "YES" : "NO"));
    printf("        record %s\n", (dp->record? "YES" : "NO"));
    printf("        index %s\n", (dp->index? "YES" : "NO"));
    printf("        skip-incr %s\n", (dp->skip_incr? "YES" : "NO"));
    printf("        skip-full %s\n", (dp->skip_full? "YES" : "NO"));

    printf("\n");
}

void disklist(argc, argv)
int argc;
char **argv;
{
    disk_t *dp;

    if(argc >= 4)
	diskloop(argc, argv, "disklist", disklist_one);
    else
	for(dp = diskqp->head; dp != NULL; dp = dp->next)
	    disklist_one(dp);
}
