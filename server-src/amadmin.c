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
 * $Id: amadmin.c,v 1.49.2.13.2.3.2.15.2.4 2004/08/02 18:54:00 martinea Exp $
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
#include "find.h"

disklist_t *diskqp;

int main P((int argc, char **argv));
void usage P((void));
void force P((int argc, char **argv));
void force_one P((disk_t *dp));
void unforce P((int argc, char **argv));
void unforce_one P((disk_t *dp));
void force_bump P((int argc, char **argv));
void force_bump_one P((disk_t *dp));
void force_no_bump P((int argc, char **argv));
void force_no_bump_one P((disk_t *dp));
void unforce_bump P((int argc, char **argv));
void unforce_bump_one P((disk_t *dp));
void reuse P((int argc, char **argv));
void noreuse P((int argc, char **argv));
void info P((int argc, char **argv));
void info_one P((disk_t *dp));
void due P((int argc, char **argv));
void due_one P((disk_t *dp));
void find P((int argc, char **argv));
void delete P((int argc, char **argv));
void delete_one P((disk_t *dp));
void balance P((int argc, char **argv));
void tape P((void));
void bumpsize P((void));
void diskloop P((int argc, char **argv, char *cmdname,
		 void (*func) P((disk_t *dp))));
char *seqdatestr P((int seq));
static int next_level0 P((disk_t *dp, info_t *info));
int bump_thresh P((int level));
void export_db P((int argc, char **argv));
void import_db P((int argc, char **argv));
void disklist P((int argc, char **argv));
void disklist_one P((disk_t *dp));

static char *conf_tapelist = NULL;

int main(argc, argv)
int argc;
char **argv;
{
    int fd;
    unsigned long malloc_hist_1, malloc_size_1;
    unsigned long malloc_hist_2, malloc_size_2;
    char *conf_diskfile;
    char *conf_infofile;
    char *conffile;

    for(fd = 3; fd < FD_SETSIZE; fd++) {
	/*
	 * Make sure nobody spoofs us with a lot of extra open files
	 * that would cause an open we do to get a very high file
	 * descriptor, which in turn might be used as an index into
	 * an array (e.g. an fd_set).
	 */
	close(fd);
    }

    safe_cd();

    set_pname("amadmin");

    malloc_size_1 = malloc_inuse(&malloc_hist_1);

    erroutput_type = ERR_INTERACTIVE;

    if(argc < 3) usage();

    if(strcmp(argv[2],"version") == 0) {
	for(argc=0; version_info[argc]; printf("%s",version_info[argc++]));
	return 0;
    }
    config_name = argv[1];
    config_dir = vstralloc(CONFIG_DIR, "/", config_name, "/", NULL);
    conffile = stralloc2(config_dir, CONFFILE_NAME);
    if(read_conffile(conffile)) {
	error("errors processing config file \"%s\"", conffile);
    }
    amfree(conffile);
    conf_diskfile = getconf_str(CNF_DISKFILE);
    if (*conf_diskfile == '/') {
	conf_diskfile = stralloc(conf_diskfile);
    } else {
	conf_diskfile = stralloc2(config_dir, conf_diskfile);
    }
    if((diskqp = read_diskfile(conf_diskfile)) == NULL) {
	error("could not load disklist \"%s\"", conf_diskfile);
    }
    amfree(conf_diskfile);
    conf_tapelist = getconf_str(CNF_TAPELIST);
    if (*conf_tapelist == '/') {
	conf_tapelist = stralloc(conf_tapelist);
    } else {
	conf_tapelist = stralloc2(config_dir, conf_tapelist);
    }
    if(read_tapelist(conf_tapelist)) {
	error("could not load tapelist \"%s\"", conf_tapelist);
    }
    conf_infofile = getconf_str(CNF_INFOFILE);
    if (*conf_infofile == '/') {
	conf_infofile = stralloc(conf_infofile);
    } else {
	conf_infofile = stralloc2(config_dir, conf_infofile);
    }
    if(open_infofile(conf_infofile)) {
	error("could not open info db \"%s\"", conf_infofile);
    }
    amfree(conf_infofile);

    if(strcmp(argv[2],"force-bump") == 0) force_bump(argc, argv);
    else if(strcmp(argv[2],"force-no-bump") == 0) force_no_bump(argc, argv);
    else if(strcmp(argv[2],"unforce-bump") == 0) unforce_bump(argc, argv);
    else if(strcmp(argv[2],"force") == 0) force(argc, argv);
    else if(strcmp(argv[2],"unforce") == 0) unforce(argc, argv);
    else if(strcmp(argv[2],"reuse") == 0) reuse(argc, argv);
    else if(strcmp(argv[2],"no-reuse") == 0) noreuse(argc, argv);
    else if(strcmp(argv[2],"info") == 0) info(argc, argv);
    else if(strcmp(argv[2],"due") == 0) due(argc, argv);
    else if(strcmp(argv[2],"find") == 0) find(argc, argv);
    else if(strcmp(argv[2],"delete") == 0) delete(argc, argv);
    else if(strcmp(argv[2],"balance") == 0) balance(argc, argv);
    else if(strcmp(argv[2],"tape") == 0) tape();
    else if(strcmp(argv[2],"bumpsize") == 0) bumpsize();
    else if(strcmp(argv[2],"import") == 0) import_db(argc, argv);
    else if(strcmp(argv[2],"export") == 0) export_db(argc, argv);
    else if(strcmp(argv[2],"disklist") == 0) disklist(argc, argv);
    else {
	fprintf(stderr, "%s: unknown command \"%s\"\n", argv[0], argv[2]);
	usage();
    }
    close_infofile();
    clear_tapelist();
    amfree(conf_tapelist);
    amfree(config_dir);

    malloc_size_2 = malloc_inuse(&malloc_hist_2);

    if(malloc_size_1 != malloc_size_2) {
	malloc_list(fileno(stderr), malloc_hist_1, malloc_hist_2);
    }

    return 0;
}


void usage P((void))
{
    fprintf(stderr, "\nUsage: %s%s <conf> <command> {<args>} ...\n",
	    get_pname(), versionsuffix());
    fprintf(stderr, "    Valid <command>s are:\n");
    fprintf(stderr,"\tversion\t\t\t\t# Show version info.\n");
    fprintf(stderr,
	    "\tforce [<hostname> [<disks>]* ]+\t# Force level 0 at next run.\n");
    fprintf(stderr,
	    "\tunforce [<hostname> [<disks>]* ]+\t# Clear force command.\n");
    fprintf(stderr,
	    "\tforce-bump [<hostname> [<disks>]* ]+\t# Force bump at next run.\n");
    fprintf(stderr,
	    "\tforce-no-bump [<hostname> [<disks>]* ]+\t# Force no-bump at next run.\n");
    fprintf(stderr,
	    "\tunforce-bump [<hostname> [<disks>]* ]+\t# Clear bump command.\n");
    fprintf(stderr,
	    "\treuse <tapelabel> ...\t\t# re-use this tape.\n");
    fprintf(stderr,
	    "\tno-reuse <tapelabel> ...\t# never re-use this tape.\n");
    fprintf(stderr,
	    "\tfind [<hostname> [<disks>]* ]*\t# Show which tapes these dumps are on.\n");
    fprintf(stderr,
	    "\tdelete [<hostname> [<disks>]* ]*\t# Delete from database.\n");
    fprintf(stderr,
	    "\tinfo [<hostname> [<disks>]* ]*\t# Show current info records.\n");
    fprintf(stderr,
	    "\tdue [<hostname> [<disks>]* ]*\t# Show due date.\n");
    fprintf(stderr,
	    "\tbalance [-days <num>]\t\t# Show nightly dump size balance.\n");
    fprintf(stderr,
	    "\ttape\t\t\t\t# Show which tape is due next.\n");
    fprintf(stderr,
	    "\tbumpsize\t\t\t# Show current bump thresholds.\n");
    fprintf(stderr,
	    "\texport [<hostname> [<disks>]* ]*\t# Export curinfo database to stdout.\n");
    fprintf(stderr,
	    "\timport\t\t\t\t# Import curinfo database from stdin.\n");
    fprintf(stderr,
  	    "\tdisklist [<hostname> [<disks>]* ]*\t# Show disklist entries.\n");

    exit(1);
}


/* ----------------------------------------------- */

#define SECS_PER_DAY (24*60*60)
time_t today;

char *seqdatestr(seq)
int seq;
{
    static char str[16];
    static char *dow[7] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    time_t t = today + seq*SECS_PER_DAY;
    struct tm *tm;

    tm = localtime(&t);

    ap_snprintf(str, sizeof(str),
		"%2d/%02d %3s", tm->tm_mon+1, tm->tm_mday, dow[tm->tm_wday]);
    return str;
}

#undef days_diff
#define days_diff(a, b)        (((b) - (a) + SECS_PER_DAY) / SECS_PER_DAY)

/* when is next level 0 due? 0 = tonight, 1 = tommorrow, etc*/
static int next_level0(dp, info)
disk_t *dp;
info_t *info;
{
    if(dp->strategy == DS_NOFULL)
	return 1;	/* fake it */
    if(info->inf[0].date < (time_t)0)
	return 0;	/* new disk */
    else
	return dp->dumpcycle - days_diff(info->inf[0].date, today);
}

static void check_dumpuser()
{
    static int been_here = 0;
    uid_t uid_me;
    uid_t uid_dumpuser;
    char *dumpuser;
    struct passwd *pw;

    if (been_here) {
       return;
    }
    uid_me = getuid();
    uid_dumpuser = uid_me;
    dumpuser = getconf_str(CNF_DUMPUSER);

    if ((pw = getpwnam(dumpuser)) == NULL) {
	error("cannot look up dump user \"%s\"", dumpuser);
	/* NOTREACHED */
    }
    uid_dumpuser = pw->pw_uid;
    if ((pw = getpwuid(uid_me)) == NULL) {
	error("cannot look up my own uid %ld", (long)uid_me);
	/* NOTREACHED */
    }
    if (uid_me != uid_dumpuser) {
	error("ERROR: running as user \"%s\" instead of \"%s\"",
	      pw->pw_name, dumpuser);
    }
    been_here = 1;
    return;
}

/* ----------------------------------------------- */

void diskloop(argc, argv, cmdname, func)
int argc;
char **argv;
char *cmdname;
void (*func) P((disk_t *dp));
{
    disk_t *dp;
    int count = 0;

    if(argc < 4) {
	fprintf(stderr,"%s: expecting \"%s [<hostname> [<disks>]* ]+\"\n",
		get_pname(), cmdname);
	usage();
    }

    match_disklist(diskqp, argc-3, argv+3);

    for(dp = diskqp->head; dp != NULL; dp = dp->next) {
	if(dp->todo) {
	    count++;
	    func(dp);
	}
    }
    if(count==0) {
	fprintf(stderr,"%s: no disk matched\n",get_pname());
    }
}

/* ----------------------------------------------- */


void force_one(dp)
disk_t *dp;
{
    char *hostname = dp->host->hostname;
    char *diskname = dp->name;
    info_t info;

#if TEXTDB
    check_dumpuser();
#endif
    get_info(hostname, diskname, &info);
    info.command |= FORCE_FULL;
    if(info.command & FORCE_BUMP) {
	info.command ^= FORCE_BUMP;
	printf("%s: WARNING: %s:%s FORCE_BUMP command was cleared.\n",
	       get_pname(), hostname, diskname);
    }
    if(put_info(hostname, diskname, &info) == 0) {
	printf("%s: %s:%s is set to a forced level 0 at next run.\n",
	       get_pname(), hostname, diskname);
    } else {
	fprintf(stderr, "%s: %s:%s could not be forced.\n",
		get_pname(), hostname, diskname);
    }
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
    info_t info;

    get_info(hostname, diskname, &info);
    if(info.command & FORCE_FULL) {
#if TEXTDB
	check_dumpuser();
#endif
	info.command ^= FORCE_FULL;
	if(put_info(hostname, diskname, &info) == 0){
	    printf("%s: force command for %s:%s cleared.\n",
		   get_pname(), hostname, diskname);
	} else {
	    fprintf(stderr,
		    "%s: force command for %s:%s could not be cleared.\n",
		    get_pname(), hostname, diskname);
	}
    }
    else {
	printf("%s: no force command outstanding for %s:%s, unchanged.\n",
	       get_pname(), hostname, diskname);
    }
}

void unforce(argc, argv)
int argc;
char **argv;
{
    diskloop(argc, argv, "unforce", unforce_one);
}


/* ----------------------------------------------- */


void force_bump_one(dp)
disk_t *dp;
{
    char *hostname = dp->host->hostname;
    char *diskname = dp->name;
    info_t info;

#if TEXTDB
    check_dumpuser();
#endif
    get_info(hostname, diskname, &info);
    info.command |= FORCE_BUMP;
    if(info.command & FORCE_NO_BUMP) {
	info.command ^= FORCE_NO_BUMP;
	printf("%s: WARNING: %s:%s FORCE_NO_BUMP command was cleared.\n",
	       get_pname(), hostname, diskname);
    }
    if (info.command & FORCE_FULL) {
	info.command ^= FORCE_FULL;
	printf("%s: WARNING: %s:%s FORCE_FULL command was cleared.\n",
	       get_pname(), hostname, diskname);
    }
    if(put_info(hostname, diskname, &info) == 0) {
	printf("%s: %s:%s is set to bump at next run.\n",
	       get_pname(), hostname, diskname);
    } else {
	fprintf(stderr, "%s: %s:%s could not be forced to bump.\n",
		get_pname(), hostname, diskname);
    }
}


void force_bump(argc, argv)
int argc;
char **argv;
{
    diskloop(argc, argv, "force-bump", force_bump_one);
}


/* ----------------------------------------------- */


void force_no_bump_one(dp)
disk_t *dp;
{
    char *hostname = dp->host->hostname;
    char *diskname = dp->name;
    info_t info;

#if TEXTDB
    check_dumpuser();
#endif
    get_info(hostname, diskname, &info);
    info.command |= FORCE_NO_BUMP;
    if(info.command & FORCE_BUMP) {
	info.command ^= FORCE_BUMP;
	printf("%s: WARNING: %s:%s FORCE_BUMP command was cleared.\n",
	       get_pname(), hostname, diskname);
    }
    if(put_info(hostname, diskname, &info) == 0) {
	printf("%s: %s:%s is set to not bump at next run.\n",
	       get_pname(), hostname, diskname);
    } else {
	fprintf(stderr, "%s: %s:%s could not be force to not bump.\n",
		get_pname(), hostname, diskname);
    }
}


void force_no_bump(argc, argv)
int argc;
char **argv;
{
    diskloop(argc, argv, "force-no-bump", force_no_bump_one);
}


/* ----------------------------------------------- */


void unforce_bump_one(dp)
disk_t *dp;
{
    char *hostname = dp->host->hostname;
    char *diskname = dp->name;
    info_t info;

    get_info(hostname, diskname, &info);
    if(info.command & (FORCE_BUMP | FORCE_NO_BUMP)) {
#if TEXTDB
	check_dumpuser();
#endif
	if(info.command & FORCE_BUMP)
	    info.command ^= FORCE_BUMP;
	if(info.command & FORCE_NO_BUMP)
	    info.command ^= FORCE_NO_BUMP;
	if(put_info(hostname, diskname, &info) == 0) {
	    printf("%s: bump command for %s:%s cleared.\n",
		   get_pname(), hostname, diskname);
	} else {
	    fprintf(stderr, "%s: %s:%s bump command could not be cleared.\n",
		    get_pname(), hostname, diskname);
	}
    }
    else {
	printf("%s: no bump command outstanding for %s:%s, unchanged.\n",
	       get_pname(), hostname, diskname);
    }
}


void unforce_bump(argc, argv)
int argc;
char **argv;
{
    diskloop(argc, argv, "unforce-bump", unforce_bump_one);
}


/* ----------------------------------------------- */

void reuse(argc, argv)
int argc;
char **argv;
{
    tape_t *tp;
    int count;

    if(argc < 4) {
	fprintf(stderr,"%s: expecting \"reuse <tapelabel> ...\"\n",
		get_pname());
	usage();
    }

    check_dumpuser();
    for(count=3; count< argc; count++) {
	tp = lookup_tapelabel(argv[count]);
	if ( tp == NULL) {
	    fprintf(stderr, "reuse: tape label %s not found in tapelist.\n",
		argv[count]);
	    continue;
	}
	if( tp->reuse == 0 ) {
	    tp->reuse = 1;
	    printf("%s: marking tape %s as reusable.\n",
		   get_pname(), argv[count]);
	} else {
	    fprintf(stderr, "%s: tape %s already reusable.\n",
		    get_pname(), argv[count]);
	}
    }

    if(write_tapelist(conf_tapelist)) {
	error("could not write tapelist \"%s\"", conf_tapelist);
    }
}

void noreuse(argc, argv)
int argc;
char **argv;
{
    tape_t *tp;
    int count;

    if(argc < 4) {
	fprintf(stderr,"%s: expecting \"no-reuse <tapelabel> ...\"\n",
		get_pname());
	usage();
    }

    check_dumpuser();
    for(count=3; count< argc; count++) {
	tp = lookup_tapelabel(argv[count]);
	if ( tp == NULL) {
	    fprintf(stderr, "no-reuse: tape label %s not found in tapelist.\n",
		argv[count]);
	    continue;
	}
	if( tp->reuse == 1 ) {
	    tp->reuse = 0;
	    printf("%s: marking tape %s as not reusable.\n",
		   get_pname(), argv[count]);
	} else {
	    fprintf(stderr, "%s: tape %s already not reusable.\n",
		    get_pname(), argv[count]);
	}
    }

    if(write_tapelist(conf_tapelist)) {
	error("could not write tapelist \"%s\"", conf_tapelist);
    }
}


/* ----------------------------------------------- */

static int deleted;

void delete_one(dp)
disk_t *dp;
{
    char *hostname = dp->host->hostname;
    char *diskname = dp->name;
    info_t info;

    if(get_info(hostname, diskname, &info)) {
	printf("%s: %s:%s NOT currently in database.\n",
	       get_pname(), hostname, diskname);
	return;
    }

    deleted++;
    if(del_info(hostname, diskname))
	error("couldn't delete %s:%s from database: %s",
	      hostname, diskname, strerror(errno));
    else
	printf("%s: %s:%s deleted from curinfo database.\n",
	       get_pname(), hostname, diskname);
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
	 get_pname());
}

/* ----------------------------------------------- */

void info_one(dp)
disk_t *dp;
{
    info_t info;
    int lev;
    struct tm *tm;
    stats_t *sp;

    get_info(dp->host->hostname, dp->name, &info);

    printf("\nCurrent info for %s %s:\n", dp->host->hostname, dp->name);
    if(info.command & FORCE_FULL) 
	printf("  (Forcing to level 0 dump at next run)\n");
    if(info.command & FORCE_BUMP) 
	printf("  (Forcing bump at next run)\n");
    if(info.command & FORCE_NO_BUMP) 
	printf("  (Forcing no-bump at next run)\n");
    printf("  Stats: dump rates (kps), Full:  %5.1f, %5.1f, %5.1f\n",
	   info.full.rate[0], info.full.rate[1], info.full.rate[2]);
    printf("                    Incremental:  %5.1f, %5.1f, %5.1f\n",
	   info.incr.rate[0], info.incr.rate[1], info.incr.rate[2]);
    printf("          compressed size, Full: %5.1f%%,%5.1f%%,%5.1f%%\n",
	   info.full.comp[0]*100, info.full.comp[1]*100, info.full.comp[2]*100);
    printf("                    Incremental: %5.1f%%,%5.1f%%,%5.1f%%\n",
	   info.incr.comp[0]*100, info.incr.comp[1]*100, info.incr.comp[2]*100);

    printf("  Dumps: lev datestmp  tape             file   origK   compK secs\n");
    for(lev = 0, sp = &info.inf[0]; lev < 9; lev++, sp++) {
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
    disk_t *dp;

    if(argc >= 4)
	diskloop(argc, argv, "info", info_one);
    else
	for(dp = diskqp->head; dp != NULL; dp = dp->next)
	    info_one(dp);
}

/* ----------------------------------------------- */

void due_one(dp)
disk_t *dp;
{
    am_host_t *hp;
    int days;
    info_t info;

    hp = dp->host;
    if(get_info(hp->hostname, dp->name, &info)) {
	printf("new disk %s:%s ignored.\n", hp->hostname, dp->name);
    }
    else {
	days = next_level0(dp, &info);
	if(days < 0) {
	    printf("Overdue %2d day%s %s:%s\n",
		   -days, (-days == 1) ? ": " : "s:",
		   hp->hostname, dp->name);
	}
	else if(days == 0) {
	    printf("Due today: %s:%s\n", hp->hostname, dp->name);
	}
	else {
	    printf("Due in %2d day%s %s:%s\n", days,
		   (days == 1) ? ": " : "s:",
		   hp->hostname, dp->name);
	}
    }
}

void due(argc, argv)
int argc;
char **argv;
{
    disk_t *dp;

    time(&today);
    if(argc >= 4)
	diskloop(argc, argv, "due", due_one);
    else
	for(dp = diskqp->head; dp != NULL; dp = dp->next)
	    due_one(dp);
}

/* ----------------------------------------------- */

void tape()
{
    tape_t *tp, *lasttp;
    int runtapes, i;

    runtapes = getconf_int(CNF_RUNTAPES);
    tp = lookup_last_reusable_tape(0);

    for ( i=0 ; i < runtapes ; i++ ) {
	printf("The next Amanda run should go onto ");

	if(tp != NULL)
	    printf("tape %s or ", tp->label);
	printf("a new tape.\n");
	
	tp = lookup_last_reusable_tape(i + 1);
    }
    lasttp = lookup_tapepos(lookup_nb_tape());
    i = runtapes;
    if(lasttp && i > 0 && lasttp->datestamp == 0) {
	int c = 0;
	while(lasttp && i > 0 && lasttp->datestamp == 0) {
	    c++;
	    lasttp = lasttp->prev;
	    i--;
	}
	lasttp = lookup_tapepos(lookup_nb_tape());
	i = runtapes;
	if(c == 1) {
	    printf("The next new tape already labelled is: %s.\n",
		   lasttp->label);
	}
	else {
	    printf("The next %d new tapes already labelled are: %s", c,
		   lasttp->label);
	    lasttp = lasttp->prev;
	    c--;
	    while(lasttp && c > 0 && lasttp->datestamp == 0) {
		printf(", %s", lasttp->label);
		lasttp = lasttp->prev;
		c--;
	    }
	    printf(".\n");
	}
    }
}

/* ----------------------------------------------- */

void balance(argc, argv)
int argc;
char **argv;
{
    disk_t *dp;
    struct balance_stats {
	int disks;
	long origsize, outsize;
    } *sp;
    int conf_runspercycle, conf_dumpcycle;
    int seq, runs_per_cycle, overdue, max_overdue;
    int later, total, balance, distinct;
    float fseq, disk_dumpcycle;
    info_t info;
    long int total_balanced, balanced;

    time(&today);
    conf_dumpcycle = getconf_int(CNF_DUMPCYCLE);
    conf_runspercycle = getconf_int(CNF_RUNSPERCYCLE);
    later = conf_dumpcycle;
    overdue = 0;
    max_overdue = 0;

    if(argc > 4 && strcmp(argv[3],"--days") == 0) {
	later = atoi(argv[4]);
	if(later < 0) later = conf_dumpcycle;
    }

    if(conf_runspercycle == 0) {
	runs_per_cycle = conf_dumpcycle;
    } else if(conf_runspercycle == -1 ) {
	runs_per_cycle = guess_runs_from_tapelist();
    } else
	runs_per_cycle = conf_runspercycle;

    if (runs_per_cycle <= 0) {
	runs_per_cycle = 1;
    }

    total = later + 1;
    balance = later + 2;
    distinct = later + 3;

    sp = (struct balance_stats *)
	alloc(sizeof(struct balance_stats) * (distinct+1));

    for(seq=0; seq <= distinct; seq++)
	sp[seq].disks = sp[seq].origsize = sp[seq].outsize = 0;

    for(dp = diskqp->head; dp != NULL; dp = dp->next) {
	if(get_info(dp->host->hostname, dp->name, &info)) {
	    printf("new disk %s:%s ignored.\n", dp->host->hostname, dp->name);
	    continue;
	}
	if (dp->strategy == DS_NOFULL) {
	    continue;
	}
	sp[distinct].disks++;
	sp[distinct].origsize += info.inf[0].size;
	sp[distinct].outsize += info.inf[0].csize;

	sp[balance].disks++;
	if(dp->dumpcycle == 0) {
	    sp[balance].origsize += info.inf[0].size * runs_per_cycle;
	    sp[balance].outsize += info.inf[0].csize * runs_per_cycle;
	}
	else {
	    sp[balance].origsize += info.inf[0].size *
				    (conf_dumpcycle / dp->dumpcycle);
	    sp[balance].outsize += info.inf[0].csize *
				   (conf_dumpcycle / dp->dumpcycle);
	}

	disk_dumpcycle = dp->dumpcycle;
	if(dp->dumpcycle <= 0)
	    disk_dumpcycle = ((float)conf_dumpcycle) / ((float)runs_per_cycle);

	seq = next_level0(dp, &info);
	fseq = seq + 0.0001;
	do {
	    if(seq < 0) {
		overdue++;
		if (-seq > max_overdue)
		    max_overdue = -seq;
		seq = 0;
		fseq = seq + 0.0001;
	    }
	    if(seq > later) {
	       	seq = later;
	    }
	    
	    sp[seq].disks++;
	    sp[seq].origsize += info.inf[0].size;
	    sp[seq].outsize += info.inf[0].csize;

	    if(seq < later) {
		sp[total].disks++;
		sp[total].origsize += info.inf[0].size;
		sp[total].outsize += info.inf[0].csize;
	    }
	    
	    /* See, if there's another run in this dumpcycle */
	    fseq += disk_dumpcycle;
	    seq = fseq;
	} while (seq < later);
    }

    if(sp[total].outsize == 0) {
	printf("\nNo data to report on yet.\n");
	amfree(sp);
	return;
    }

    balanced = sp[balance].outsize / runs_per_cycle;
    if(conf_dumpcycle == later) {
	total_balanced = sp[total].outsize / runs_per_cycle;
    }
    else {
	total_balanced = 1024*(((sp[total].outsize/1024) * conf_dumpcycle)
			    / (runs_per_cycle * later));
    }

    printf("\n due-date  #fs    orig KB     out KB   balance\n");
    printf("----------------------------------------------\n");
    for(seq = 0; seq < later; seq++) {
	printf("%-9.9s  %3d %10ld %10ld ",
	       seqdatestr(seq), sp[seq].disks,
	       sp[seq].origsize, sp[seq].outsize);
	if(!sp[seq].outsize) printf("     --- \n");
	else printf("%+8.1f%%\n",
		    (sp[seq].outsize-balanced)*100.0/(double)balanced);
    }
    if(sp[later].disks != 0) {
	printf("later      %3d %10ld %10ld ",
	       sp[later].disks,
	       sp[later].origsize, sp[later].outsize);
	if(!sp[later].outsize) printf("     --- \n");
	else printf("%+8.1f%%\n",
		    (sp[later].outsize-balanced)*100.0/(double)balanced);
    }
    printf("----------------------------------------------\n");
    printf("TOTAL      %3d %10ld %10ld %9ld\n", sp[total].disks,
	   sp[total].origsize, sp[total].outsize, total_balanced);
    if (sp[balance].origsize != sp[total].origsize ||
        sp[balance].outsize != sp[total].outsize ||
	balanced != total_balanced) {
	printf("BALANCED       %10ld %10ld %9ld\n",
	       sp[balance].origsize, sp[balance].outsize, balanced);
    }
    if (sp[distinct].disks != sp[total].disks) {
	printf("DISTINCT   %3d %10ld %10ld\n", sp[distinct].disks,
	       sp[distinct].origsize, sp[distinct].outsize);
    }
    printf("  (estimated %d run%s per dumpcycle)\n",
	   runs_per_cycle, (runs_per_cycle == 1) ? "" : "s");
    if (overdue) {
	printf(" (%d filesystem%s overdue, the most being overdue %d day%s)\n",
	       overdue, (overdue == 1) ? "" : "s",
	       max_overdue, (max_overdue == 1) ? "" : "s");
    }
    amfree(sp);
}


/* ----------------------------------------------- */

void find(argc, argv)
int argc;
char **argv;
{
    int start_argc;
    char *sort_order = NULL;
    find_result_t *output_find;

    if(argc < 3) {
	fprintf(stderr,
		"%s: expecting \"find [--sort <hkdlb>] [hostname [<disk>]]*\"\n",
		get_pname());
	usage();
    }


    sort_order = newstralloc(sort_order, DEFAULT_SORT_ORDER);
    if(argc > 4 && strcmp(argv[3],"--sort") == 0) {
	int i, valid_sort=1;

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
	if(valid_sort) {
	    sort_order = newstralloc(sort_order, argv[4]);
	} else {
	    printf("Invalid sort order: %s\n", argv[4]);
	    printf("Use default sort order: %s\n", sort_order);
	}
	start_argc=6;
    } else {
	start_argc=4;
    }
    match_disklist(diskqp, argc-(start_argc-1), argv+(start_argc-1));
    output_find = find_dump(1, diskqp);

    if(argc-(start_argc-1) > 0) {
	free_find_result(&output_find);
	match_disklist(diskqp, argc-(start_argc-1), argv+(start_argc-1));
	output_find = find_dump(0, NULL);
    }

    sort_find_result(sort_order, &output_find);
    print_find_result(output_find);
    free_find_result(&output_find);

    amfree(sort_order);
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
    int conf_bumppercent = getconf_int(CNF_BUMPPERCENT);
    double conf_bumpmult = getconf_real(CNF_BUMPMULT);

    printf("Current bump parameters:\n");

    if(conf_bumppercent == 0) {
	printf("  bumpsize %5d KB\t- minimum savings (threshold) to bump level 1 -> 2\n",
	       getconf_int(CNF_BUMPSIZE));
	printf("  bumpdays %5d\t- minimum days at each level\n",
	       getconf_int(CNF_BUMPDAYS));
	printf("  bumpmult %5.5g\t- threshold = bumpsize * bumpmult**(level-1)\n\n",
	       conf_bumpmult);

	printf("      Bump -> To  Threshold\n");
	for(l = 1; l < 9; l++) {
	    printf("\t%d  ->  %d  %9d KB\n", l, l+1, bump_thresh(l));
	}
	putchar('\n');
    }
    else {
	double bumppercent = conf_bumppercent;

	printf("  bumppercent %3d %%\t- minimum savings (threshold) to bump level 1 -> 2\n",
	       conf_bumppercent);
	printf("  bumpdays %5d\t- minimum days at each level\n",
	       getconf_int(CNF_BUMPDAYS));
	printf("  bumpmult %5.5g\t- threshold = disk_size * bumppercent * bumpmult**(level-1)\n\n",
	       conf_bumpmult);

	printf("      Bump -> To  Threshold\n");
	for(l = 1; l < 9; l++) {
	    printf("\t%d  ->  %d  %7.2f %%\n", l, l+1, bumppercent);
	    bumppercent *= conf_bumpmult;
	    if(bumppercent >= 100.000) { bumppercent = 100.0;}
	}
	putchar('\n');
    }
}

/* ----------------------------------------------- */

void export_one P((disk_t *dp));

void export_db(argc, argv)
int argc;
char **argv;
{
    disk_t *dp;
    time_t curtime;
    char hostname[MAX_HOSTNAME_LENGTH+1];
    int i;

    printf("CURINFO Version %s CONF %s\n", version(), getconf_str(CNF_ORG));

    curtime = time(0);
    if(gethostname(hostname, sizeof(hostname)-1) == -1) {
	error("could not determine host name: %s", strerror(errno));
    }
    hostname[sizeof(hostname)-1] = '\0';
    printf("# Generated by:\n#    host: %s\n#    date: %s",
	   hostname, ctime(&curtime));

    printf("#    command:");
    for(i = 0; i < argc; i++) {
	printf(" %s", argv[i]);
    }

    printf("\n# This file can be merged back in with \"amadmin import\".\n");
    printf("# Edit only with care.\n");

    if(argc >= 4) {
	diskloop(argc, argv, "export", export_one);
    } else for(dp = diskqp->head; dp != NULL; dp = dp->next) {
	export_one(dp);
    }
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
    printf("last_level: %d\n",info.last_level);
    printf("consecutive_runs: %d\n",info.consecutive_runs);
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
    for(l=0;info.history[l].level > -1;l++) {
	printf("history: %d %ld %ld %ld\n",info.history[l].level,
	       info.history[l].size, info.history[l].csize,
	       info.history[l].date);
    }
    printf("//\n");
}

/* ----------------------------------------------- */

int import_one P((void));
char *impget_line P((void));

void import_db(argc, argv)
int argc;
char **argv;
{
    int vers_maj, vers_min, vers_patch, newer;
    char *org;
    char *line = NULL;
    char *hdr;
    char *s;
    int ch;

    /* process header line */

    if((line = agets(stdin)) == NULL) {
	fprintf(stderr, "%s: empty input.\n", get_pname());
	return;
    }

    s = line;
    ch = *s++;

    hdr = "version";
#define sc "CURINFO Version"
    if(strncmp(s - 1, sc, sizeof(sc)-1) != 0) {
	goto bad_header;
    }
    s += sizeof(sc)-1;
    ch = *s++;
#undef sc
    skip_whitespace(s, ch);
    if(ch == '\0'
       || sscanf(s - 1, "%d.%d.%d", &vers_maj, &vers_min, &vers_patch) != 3) {
	goto bad_header;
    }

    skip_integer(s, ch);			/* skip over major */
    if(ch != '.') {
	goto bad_header;
    }
    ch = *s++;
    skip_integer(s, ch);			/* skip over minor */
    if(ch != '.') {
	goto bad_header;
    }
    ch = *s++;
    skip_integer(s, ch);			/* skip over patch */

    hdr = "comment";
    if(ch == '\0') {
	goto bad_header;
    }
    skip_non_whitespace(s, ch);
    s[-1] = '\0';

    hdr = "CONF";
    skip_whitespace(s, ch);			/* find the org keyword */
#define sc "CONF"
    if(ch == '\0' || strncmp(s - 1, sc, sizeof(sc)-1) != 0) {
	goto bad_header;
    }
    s += sizeof(sc)-1;
    ch = *s++;
#undef sc

    hdr = "org";
    skip_whitespace(s, ch);			/* find the org string */
    if(ch == '\0') {
	goto bad_header;
    }
    org = s - 1;

    newer = (vers_maj != VERSION_MAJOR)? vers_maj > VERSION_MAJOR :
	    (vers_min != VERSION_MINOR)? vers_min > VERSION_MINOR :
					 vers_patch > VERSION_PATCH;
    if(newer)
	fprintf(stderr,
	     "%s: WARNING: input is from newer Amanda version: %d.%d.%d.\n",
		get_pname(), vers_maj, vers_min, vers_patch);

    if(strcmp(org, getconf_str(CNF_ORG)) != 0) {
	fprintf(stderr, "%s: WARNING: input is from different org: %s\n",
		get_pname(), org);
    }

    while(import_one());

    amfree(line);
    return;

 bad_header:

    amfree(line);
    fprintf(stderr, "%s: bad CURINFO header line in input: %s.\n",
	    get_pname(), hdr);
    fprintf(stderr, "    Was the input in \"amadmin export\" format?\n");
    return;
}


int import_one P((void))
{
    info_t info;
    stats_t onestat;
    int rc, level;
    long onedate;
    char *line = NULL;
    char *s, *fp;
    int ch;
    int nb_history, i;
    char *hostname = NULL;
    char *diskname = NULL;

#if TEXTDB
    check_dumpuser();
#endif

    memset(&info, 0, sizeof(info_t));

    for(level = 0; level < DUMP_LEVELS; level++) {
        info.inf[level].date = (time_t)-1;
    }

    /* get host: disk: command: lines */

    hostname = diskname = NULL;

    if((line = impget_line()) == NULL) return 0;	/* nothing there */
    s = line;
    ch = *s++;

    skip_whitespace(s, ch);
#define sc "host:"
    if(ch == '\0' || strncmp(s - 1, sc, sizeof(sc)-1) != 0) goto parse_err;
    s += sizeof(sc)-1;
    ch = s[-1];
#undef sc
    skip_whitespace(s, ch);
    if(ch == '\0') goto parse_err;
    fp = s-1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';
    hostname = stralloc(fp);
    s[-1] = ch;

    skip_whitespace(s, ch);
    while (ch == 0) {
      amfree(line);
      if((line = impget_line()) == NULL) goto shortfile_err;
      s = line;
      ch = *s++;
      skip_whitespace(s, ch);
    }
#define sc "disk:"
    if(strncmp(s - 1, sc, sizeof(sc)-1) != 0) goto parse_err;
    s += sizeof(sc)-1;
    ch = s[-1];
#undef sc
    skip_whitespace(s, ch);
    if(ch == '\0') goto parse_err;
    fp = s-1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';
    diskname = stralloc(fp);
    s[-1] = ch;

    amfree(line);
    if((line = impget_line()) == NULL) goto shortfile_err;
    if(sscanf(line, "command: %d", &info.command) != 1) goto parse_err;

    /* get last_level and consecutive_runs */

    amfree(line);
    if((line = impget_line()) == NULL) goto shortfile_err;
    rc = sscanf(line, "last_level: %d", &info.last_level);
    if(rc == 1) {
	amfree(line);
	if((line = impget_line()) == NULL) goto shortfile_err;
	if(sscanf(line, "consecutive_runs: %d", &info.consecutive_runs) != 1) goto parse_err;
	amfree(line);
	if((line = impget_line()) == NULL) goto shortfile_err;
    }

    /* get rate: and comp: lines for full dumps */

    rc = sscanf(line, "full-rate: %f %f %f",
		&info.full.rate[0], &info.full.rate[1], &info.full.rate[2]);
    if(rc != 3) goto parse_err;

    amfree(line);
    if((line = impget_line()) == NULL) goto shortfile_err;
    rc = sscanf(line, "full-comp: %f %f %f",
		&info.full.comp[0], &info.full.comp[1], &info.full.comp[2]);
    if(rc != 3) goto parse_err;

    /* get rate: and comp: lines for incr dumps */

    amfree(line);
    if((line = impget_line()) == NULL) goto shortfile_err;
    rc = sscanf(line, "incr-rate: %f %f %f",
		&info.incr.rate[0], &info.incr.rate[1], &info.incr.rate[2]);
    if(rc != 3) goto parse_err;

    amfree(line);
    if((line = impget_line()) == NULL) goto shortfile_err;
    rc = sscanf(line, "incr-comp: %f %f %f",
		&info.incr.comp[0], &info.incr.comp[1], &info.incr.comp[2]);
    if(rc != 3) goto parse_err;

    /* get stats for dump levels */

    while(1) {
	amfree(line);
	if((line = impget_line()) == NULL) goto shortfile_err;
	if(strncmp(line, "//", 2) == 0) {
	    /* end of record */
	    break;
	}
	if(strncmp(line, "history:", 8) == 0) {
	    /* end of record */
	    break;
	}
	memset(&onestat, 0, sizeof(onestat));

	s = line;
	ch = *s++;

	skip_whitespace(s, ch);
#define sc "stats:"
	if(ch == '\0' || strncmp(s - 1, sc, sizeof(sc)-1) != 0) {
	    goto parse_err;
	}
	s += sizeof(sc)-1;
	ch = s[-1];
#undef sc

	skip_whitespace(s, ch);
	if(ch == '\0' || sscanf(s - 1, "%d", &level) != 1) {
	    goto parse_err;
	}
	skip_integer(s, ch);

	skip_whitespace(s, ch);
	if(ch == '\0' || sscanf(s - 1, "%ld", &onestat.size) != 1) {
	    goto parse_err;
	}
	skip_integer(s, ch);

	skip_whitespace(s, ch);
	if(ch == '\0' || sscanf(s - 1, "%ld", &onestat.csize) != 1) {
	    goto parse_err;
	}
	skip_integer(s, ch);

	skip_whitespace(s, ch);
	if(ch == '\0' || sscanf(s - 1, "%ld", &onestat.secs) != 1) {
	    goto parse_err;
	}
	skip_integer(s, ch);

	skip_whitespace(s, ch);
	if(ch == '\0' || sscanf(s - 1, "%ld", &onedate) != 1) {
	    goto parse_err;
	}
	skip_integer(s, ch);

	skip_whitespace(s, ch);
	if(ch != '\0') {
	    if(sscanf(s - 1, "%d", &onestat.filenum) != 1) {
		goto parse_err;
	    }
	    skip_integer(s, ch);

	    skip_whitespace(s, ch);
	    if(ch == '\0') {
		if (onestat.filenum != 0)
		    goto parse_err;
		onestat.label[0] = '\0';
	    } else {
		strncpy(onestat.label, s - 1, sizeof(onestat.label)-1);
		onestat.label[sizeof(onestat.label)-1] = '\0';
	    }
	}

	/* time_t not guarranteed to be long */
	onestat.date = onedate;
	if(level < 0 || level > 9) goto parse_err;

	info.inf[level] = onestat;
    }
    nb_history = 0;
    for(i=0;i<=NB_HISTORY+1;i++) {
	info.history[i].level = -2;
    }
    while(1) {
	history_t onehistory;
	long date;

	if(line[0] == '/' && line[1] == '/') {
	    info.history[nb_history].level = -2;
	    rc = 0;
	    break;
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
	onehistory.date = date; /* time_t not guarranteed to be long */

	info.history[nb_history++] = onehistory;
	amfree(line);
	if((line = impget_line()) == NULL) goto shortfile_err;
    }
    amfree(line);

    /* got a full record, now write it out to the database */

    if(put_info(hostname, diskname, &info)) {
	fprintf(stderr, "%s: error writing record for %s:%s\n",
		get_pname(), hostname, diskname);
    }
    amfree(hostname);
    amfree(diskname);
    return 1;

 parse_err:
    amfree(line);
    amfree(hostname);
    amfree(diskname);
    fprintf(stderr, "%s: parse error reading import record.\n", get_pname());
    return 0;

 shortfile_err:
    amfree(line);
    amfree(hostname);
    amfree(diskname);
    fprintf(stderr, "%s: short file reading import record.\n", get_pname());
    return 0;
}

char *
impget_line ()
{
    char *line;
    char *s;
    int ch;

    for(; (line = agets(stdin)) != NULL; free(line)) {
	s = line;
	ch = *s++;

	skip_whitespace(s, ch);
	if(ch == '#') {
	    /* ignore comment lines */
	    continue;
	} else if(ch) {
	    /* found non-blank, return line */
	    return line;
	}
	/* otherwise, a blank line, so keep going */
    }
    if(ferror(stdin)) {
	fprintf(stderr, "%s: reading stdin: %s\n",
		get_pname(), strerror(errno));
    }
    return NULL;
}

/* ----------------------------------------------- */

void disklist_one(dp)
disk_t *dp;
{
    am_host_t *hp;
    interface_t *ip;
    sle_t *excl;

    hp = dp->host;
    ip = hp->netif;

    printf("line %d:\n", dp->line);

    printf("    host %s:\n", hp->hostname);
    printf("        interface %s\n",
	   ip->name[0] ? ip->name : "default");

    printf("    disk %s:\n", dp->name);
    if(dp->device) printf("        device %s\n", dp->device);

    printf("        program \"%s\"\n", dp->program);
    if(dp->exclude_file != NULL && dp->exclude_file->nb_element > 0) {
	printf("        exclude file");
	for(excl = dp->exclude_file->first; excl != NULL; excl = excl->next) {
	    printf(" \"%s\"", excl->name);
	}
	printf("\n");
    }
    if(dp->exclude_list != NULL && dp->exclude_list->nb_element > 0) {
	printf("        exclude list");
	if(dp->exclude_optional) printf(" optional");
	for(excl = dp->exclude_list->first; excl != NULL; excl = excl->next) {
	    printf(" \"%s\"", excl->name);
	}
	printf("\n");
    }
    if(dp->include_file != NULL && dp->include_file->nb_element > 0) {
	printf("        include file");
	for(excl = dp->include_file->first; excl != NULL; excl = excl->next) {
	    printf(" \"%s\"", excl->name);
	}
	printf("\n");
    }
    if(dp->include_list != NULL && dp->include_list->nb_element > 0) {
	printf("        include list");
	if(dp->include_optional) printf(" optional");
	for(excl = dp->include_list->first; excl != NULL; excl = excl->next) {
	    printf(" \"%s\"", excl->name);
	}
	printf("\n");
    }
    printf("        priority %ld\n", dp->priority);
    printf("        dumpcycle %ld\n", dp->dumpcycle);
    printf("        maxdumps %d\n", dp->maxdumps);
    printf("        maxpromoteday %d\n", dp->maxpromoteday);
    if(dp->bumppercent > 0) {
	printf("        bumppercent %d\n", dp->bumppercent);
    }
    else {
	printf("        bumpsize %d\n", dp->bumpsize);
    }
    printf("        bumpdays %d\n", dp->bumpdays);
    printf("        bumpmult %f\n", dp->bumpmult);

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
    case DS_INCRONLY:
	printf("INCRONLY\n");
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
