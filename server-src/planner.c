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
 * Authors: the Amanda Development Team.  Its members are listed in a
 * file named AUTHORS, in the root directory of this distribution.
 */
/*
 * $Id: planner.c,v 1.76.2.15.2.13.2.36 2004/09/17 11:45:05 martinea Exp $
 *
 * backup schedule planner for the Amanda backup system.
 */
#include "amanda.h"
#include "arglist.h"
#include "conffile.h"
#include "diskfile.h"
#include "tapefile.h"
#include "infofile.h"
#include "logfile.h"
#include "clock.h"
#include "dgram.h"
#include "protocol.h"
#include "version.h"
#include "amfeatures.h"
#include "server_util.h"
#include "holding.h"

#define MAX_LEVELS		    3	/* max# of estimates per filesys */

#define RUNS_REDZONE		    5	/* should be in conf file? */

#define PROMOTE_THRESHOLD	 0.05	/* if <5% unbalanced, don't promote */
#define DEFAULT_DUMPRATE	 30.0	/* K/s */

/* configuration file stuff */

char *conf_tapetype;
int conf_maxdumpsize;
int conf_runtapes;
int conf_dumpcycle;
int conf_runspercycle;
int conf_tapecycle;
int conf_bumpdays;
int conf_bumpsize;
int conf_etimeout;
int conf_reserve;
int conf_autoflush;
double conf_bumpmult;

#define HOST_READY				((void *)0)	/* must be 0 */
#define HOST_ACTIVE				((void *)1)
#define HOST_DONE				((void *)2)

#define DISK_READY				0		/* must be 0 */
#define DISK_ACTIVE				1
#define DISK_DONE				2

typedef struct est_s {
    int state;
    int got_estimate;
    int dump_priority;
    int dump_level;
    long dump_size;
    int degr_level;	/* if dump_level == 0, what would be the inc level */
    long degr_size;
    int last_level;
    long last_lev0size;
    int next_level0;
    int level_days;
    int promote;
    double fullrate, incrrate;
    double fullcomp, incrcomp;
    char *errstr;
    int level[MAX_LEVELS];
    char *dumpdate[MAX_LEVELS];
    long est_size[MAX_LEVELS];
} est_t;

#define est(dp)	((est_t *)(dp)->up)

disklist_t startq, waitq, estq, failq, schedq;
long total_size;
double total_lev0, balanced_size, balance_threshold;
unsigned long tape_length, tape_mark;
int result_port, amanda_port;

static am_feature_t *our_features = NULL;
static char *our_feature_string = NULL;

#ifdef KRB4_SECURITY
int kamanda_port;
#endif

tapetype_t *tape;
long tt_blocksize;
long tt_blocksize_kb;
int runs_per_cycle = 0;
time_t today;

dgram_t *msg;

/* We keep a LIFO queue of before images for all modifications made
 * to schedq in our attempt to make the schedule fit on the tape.
 * Enough information is stored to reinstate a dump if it turns out
 * that it shouldn't have been touched after all.
 */
typedef struct bi_s {
    struct bi_s *next;
    struct bi_s *prev;
    int deleted;		/* 0=modified, 1=deleted */
    disk_t *dp;			/* The disk that was changed */
    int level;			/* The original level */
    long size;			/* The original size */
    char *errstr;		/* A message describing why this disk is here */
} bi_t;

typedef struct bilist_s {
    bi_t *head, *tail;
} bilist_t;

bilist_t biq;			/* The BI queue itself */

char *datestamp = NULL;

/*
 * ========================================================================
 * MAIN PROGRAM
 *
 */

static void setup_estimate P((disk_t *dp));
static void get_estimates P((void));
static void analyze_estimate P((disk_t *dp));
static void handle_failed P((disk_t *dp));
static void delay_dumps P((void));
static int promote_highest_priority_incremental P((void));
static int promote_hills P((void));
static void output_scheduleline P((disk_t *dp));

int main(argc, argv)
int argc;
char **argv;
{
    disklist_t *origqp;
    disk_t *dp;
    int moved_one;
    char **vp;
    unsigned long malloc_hist_1, malloc_size_1;
    unsigned long malloc_hist_2, malloc_size_2;
    long initial_size;
    int fd;
    char *conffile;
    char *conf_diskfile;
    char *conf_tapelist;
    char *conf_infofile;
    times_t section_start;

    for(fd = 3; fd < FD_SETSIZE; fd++) {
	/*
	 * Make sure nobody spoofs us with a lot of extra open files
	 * that would cause an open we do to get a very high file
	 * descriptor, which in turn might be used as an index into
	 * an array (e.g. an fd_set).
	 */
	close(fd);
    }

    if (argc > 1) {
	config_name = stralloc(argv[1]);
	config_dir = vstralloc(CONFIG_DIR, "/", config_name, "/", NULL);
    } else {
	char my_cwd[STR_SIZE];

	if (getcwd(my_cwd, sizeof(my_cwd)) == NULL) {
	    error("cannot determine current working directory");
	}
	config_dir = stralloc2(my_cwd, "/");
	if ((config_name = strrchr(my_cwd, '/')) != NULL) {
	    config_name = stralloc(config_name + 1);
	}
    }

    safe_cd();

    set_pname("planner");

    malloc_size_1 = malloc_inuse(&malloc_hist_1);

    erroutput_type = (ERR_AMANDALOG|ERR_INTERACTIVE);
    set_logerror(logerror);
    startclock();
    section_start = curclock();

    our_features = am_init_feature_set();
    our_feature_string = am_feature_to_string(our_features);

    fprintf(stderr, "%s: pid %ld executable %s version %s\n",
	    get_pname(), (long) getpid(), argv[0], version());
    for(vp = version_info; *vp != NULL; vp++)
	fprintf(stderr, "%s: %s", get_pname(), *vp);

    /*
     * 1. Networking Setup
     *
     * Planner runs setuid to get a priviledged socket for BSD security.
     * We get the socket right away as root, then setuid back to a normal
     * user.  If we are not using BSD security, planner is not installed
     * setuid root.
     */

    /* set up dgram port first thing */

    msg = dgram_alloc();

    if(dgram_bind(msg, &result_port) == -1) {
	error("could not bind result datagram port: %s", strerror(errno));
    }

    if(geteuid() == 0) {
	/* set both real and effective uid's to real uid, likewise for gid */
	setgid(getgid());
	setuid(getuid());
    }

    /*
     * From this point on we are running under our real uid, so we don't
     * have to worry about opening security holes below.  Make sure we
     * are a valid user.
     */

    if(getpwuid(getuid()) == NULL) {
	error("can't get login name for my uid %ld", (long)getuid());
    }

    /*
     * 2. Read in Configuration Information
     *
     * All the Amanda configuration files are loaded before we begin.
     */

    fprintf(stderr,"READING CONF FILES...\n");

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
    if((origqp = read_diskfile(conf_diskfile)) == NULL) {
	error("could not load disklist \"%s\"", conf_diskfile);
    }
    match_disklist(origqp, argc-2, argv+2);
    for(dp = origqp->head; dp != NULL; dp = dp->next) {
	if(dp->todo)
	    log_add(L_DISK, "%s %s", dp->host->hostname, dp->name);
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
    amfree(conf_tapelist);
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

    conf_tapetype = getconf_str(CNF_TAPETYPE);
    conf_maxdumpsize = getconf_int(CNF_MAXDUMPSIZE);
    conf_runtapes = getconf_int(CNF_RUNTAPES);
    conf_dumpcycle = getconf_int(CNF_DUMPCYCLE);
    conf_runspercycle = getconf_int(CNF_RUNSPERCYCLE);
    conf_tapecycle = getconf_int(CNF_TAPECYCLE);
    conf_bumpdays = getconf_int(CNF_BUMPDAYS);
    conf_bumpsize = getconf_int(CNF_BUMPSIZE);
    conf_bumpmult = getconf_real(CNF_BUMPMULT);
    conf_etimeout = getconf_int(CNF_ETIMEOUT);
    conf_reserve  = getconf_int(CNF_RESERVE);
    conf_autoflush = getconf_int(CNF_AUTOFLUSH);

    amfree(datestamp);
    today = time(0);
    datestamp = construct_datestamp(NULL);
    log_add(L_START, "date %s", datestamp);

    /* some initializations */

    if(conf_runspercycle == 0) {
	runs_per_cycle = conf_dumpcycle;
    } else if(conf_runspercycle == -1 ) {
	runs_per_cycle = guess_runs_from_tapelist();
    } else
	runs_per_cycle = conf_runspercycle;

    if (runs_per_cycle <= 0) {
	runs_per_cycle = 1;
    }

    /*
     * do some basic sanity checking
     */
     if(conf_tapecycle <= runs_per_cycle) {
	log_add(L_WARNING, "tapecycle (%d) <= runspercycle (%d)",
		conf_tapecycle, runs_per_cycle);
     }
    
    tape = lookup_tapetype(conf_tapetype);
    if(conf_maxdumpsize > 0) {
	tape_length = conf_maxdumpsize;
    }
    else {
	tape_length = tape->length * conf_runtapes;
    }
    tape_mark   = tape->filemark;
    tt_blocksize_kb = tape->blocksize;
    tt_blocksize = tt_blocksize_kb * 1024;

    proto_init(msg->socket, today, 1000); /* XXX handles should eq nhosts */

#ifdef KRB4_SECURITY
    kerberos_service_init();
#endif

    fprintf(stderr, "%s: time %s: startup took %s secs\n",
		    get_pname(),
		    walltime_str(curclock()),
		    walltime_str(timessub(curclock(), section_start)));

    /*
     * 3. Calculate Preliminary Dump Levels
     *
     * Before we can get estimates from the remote slave hosts, we make a
     * first attempt at guessing what dump levels we will be dumping at
     * based on the curinfo database.
     */

    fprintf(stderr,"\nSETTING UP FOR ESTIMATES...\n");
    section_start = curclock();

    startq.head = startq.tail = NULL;
    while(!empty(*origqp)) {
	disk_t *dp = dequeue_disk(origqp);
	if(dp->todo == 1) {
	    setup_estimate(dp);
	}
    }

    fprintf(stderr, "%s: time %s: setting up estimates took %s secs\n",
		    get_pname(),
		    walltime_str(curclock()),
		    walltime_str(timessub(curclock(), section_start)));


    /*
     * 4. Get Dump Size Estimates from Remote Client Hosts
     *
     * Each host is queried (in parallel) for dump size information on all
     * of its disks, and the results gathered as they come in.
     */

    /* go out and get the dump estimates */

    fprintf(stderr,"\nGETTING ESTIMATES...\n");
    section_start = curclock();

    estq.head = estq.tail = NULL;
    failq.head = failq.tail = NULL;

    get_estimates();

    fprintf(stderr, "%s: time %s: getting estimates took %s secs\n",
		    get_pname(),
		    walltime_str(curclock()),
		    walltime_str(timessub(curclock(), section_start)));

    /*
     * At this point, all disks with estimates are in estq, and
     * all the disks on hosts that didn't respond to our inquiry
     * are in failq.
     */

    dump_queue("FAILED", failq, 15, stderr);
    dump_queue("DONE", estq, 15, stderr);


    /*
     * 5. Analyze Dump Estimates
     *
     * Each disk's estimates are looked at to determine what level it
     * should dump at, and to calculate the expected size and time taking
     * historical dump rates and compression ratios into account.  The
     * total expected size is accumulated as well.
     */

    fprintf(stderr,"\nANALYZING ESTIMATES...\n");
    section_start = curclock();

			/* an empty tape still has a label and an endmark */
    total_size = (tt_blocksize_kb + tape_mark) * 2;
    total_lev0 = 0.0;
    balanced_size = 0.0;

    schedq.head = schedq.tail = NULL;
    while(!empty(estq)) analyze_estimate(dequeue_disk(&estq));
    while(!empty(failq)) handle_failed(dequeue_disk(&failq));

    /*
     * At this point, all the disks are on schedq sorted by priority.
     * The total estimated size of the backups is in total_size.
     */

    {
	disk_t *dp;

	fprintf(stderr, "INITIAL SCHEDULE (size %ld):\n", total_size);
	for(dp = schedq.head; dp != NULL; dp = dp->next) {
	    fprintf(stderr, "  %s %s pri %d lev %d size %ld\n",
		    dp->host->hostname, dp->name, est(dp)->dump_priority,
		    est(dp)->dump_level, est(dp)->dump_size);
	}
    }


    /*
     * 6. Delay Dumps if Schedule Too Big
     *
     * If the generated schedule is too big to fit on the tape, we need to
     * delay some full dumps to make room.  Incrementals will be done
     * instead (except for new or forced disks).
     *
     * In extreme cases, delaying all the full dumps is not even enough.
     * If so, some low-priority incrementals will be skipped completely
     * until the dumps fit on the tape.
     */

    fprintf(stderr,
      "\nDELAYING DUMPS IF NEEDED, total_size %ld, tape length %lu mark %lu\n",
	    total_size, tape_length, tape_mark);

    initial_size = total_size;

    delay_dumps();

    /* XXX - why bother checking this? */
    if(empty(schedq) && total_size < initial_size)
	error("cannot fit anything on tape, bailing out");


    /*
     * 7. Promote Dumps if Schedule Too Small
     *
     * Amanda attempts to balance the full dumps over the length of the
     * dump cycle.  If this night's full dumps are too small relative to
     * the other nights, promote some high-priority full dumps that will be
     * due for the next run, to full dumps for tonight, taking care not to
     * overflow the tape size.
     *
     * This doesn't work too well for small sites.  For these we scan ahead
     * looking for nights that have an excessive number of dumps and promote
     * one of them.
     *
     * Amanda never delays full dumps just for the sake of balancing the
     * schedule, so it can take a full cycle to balance the schedule after
     * a big bump.
     */

    fprintf(stderr,
     "\nPROMOTING DUMPS IF NEEDED, total_lev0 %1.0f, balanced_size %1.0f...\n",
	    total_lev0, balanced_size);

    balance_threshold = balanced_size * PROMOTE_THRESHOLD;
    moved_one = 1;
    while((balanced_size - total_lev0) > balance_threshold && moved_one)
	moved_one = promote_highest_priority_incremental();

    moved_one = promote_hills();

    fprintf(stderr, "%s: time %s: analysis took %s secs\n",
		    get_pname(),
		    walltime_str(curclock()),
		    walltime_str(timessub(curclock(), section_start)));


    /*
     * 8. Output Schedule
     *
     * The schedule goes to stdout, presumably to driver.  A copy is written
     * on stderr for the debug file.
     */

    fprintf(stderr,"\nGENERATING SCHEDULE:\n--------\n");

    if(conf_autoflush) {
	dumpfile_t file;
	sl_t *holding_list;
	sle_t *holding_file;
	holding_list = get_flush(NULL, datestamp, 0, 0);
	for(holding_file=holding_list->first; holding_file != NULL;
				       holding_file = holding_file->next) {
	    get_dumpfile(holding_file->name, &file);
	    
	    log_add(L_DISK, "%s %s", file.name, file.disk);
	    fprintf(stderr,
		    "FLUSH %s %s %s %d %s\n",
		    file.name,
		    file.disk,
		    file.datestamp,
		    file.dumplevel,
		    holding_file->name);
	    fprintf(stdout,
		    "FLUSH %s %s %s %d %s\n",
		    file.name,
		    file.disk,
		    file.datestamp,
		    file.dumplevel,
		    holding_file->name);
	}
	free_sl(holding_list);
	holding_list = NULL;
    }
    fprintf(stderr, "ENDFLUSH\n");
    fprintf(stdout, "ENDFLUSH\n");
    fflush(stdout);

    while(!empty(schedq)) output_scheduleline(dequeue_disk(&schedq));
    fprintf(stderr, "--------\n");

    close_infofile();
    log_add(L_FINISH, "date %s", datestamp);

    amfree(msg);
    amfree(datestamp);
    amfree(config_dir);
    amfree(config_name);
    amfree(our_feature_string);
    am_release_feature_set(our_features);
    our_features = NULL;

    malloc_size_2 = malloc_inuse(&malloc_hist_2);

    if(malloc_size_1 != malloc_size_2) {
	malloc_list(fileno(stderr), malloc_hist_1, malloc_hist_2);
    }

    return 0;
}



/*
 * ========================================================================
 * SETUP FOR ESTIMATES
 *
 */

static int last_level P((info_t *info));		  /* subroutines */
static long est_size P((disk_t *dp, int level));
static long est_tape_size P((disk_t *dp, int level));
static int next_level0 P((disk_t *dp, info_t *info));
static int runs_at P((info_t *info, int lev));
static long bump_thresh P((int level));
static int when_overwrite P((char *label));

static void askfor(ep, seq, lev, info)
est_t *ep;	/* esimate data block */
int seq;	/* sequence number of request */
int lev;	/* dump level being requested */
info_t *info;	/* info block for disk */
{
    stats_t *stat;

    if(seq < 0 || seq >= MAX_LEVELS) {
	error("error [planner askfor: seq out of range 0..%d: %d]",
	      MAX_LEVELS, seq);
    }
    if(lev < -1 || lev >= DUMP_LEVELS) {
	error("error [planner askfor: lev out of range -1..%d: %d]",
	      DUMP_LEVELS, lev);
    }

    if (lev == -1) {
	ep->level[seq] = -1;
	ep->dumpdate[seq] = (char *)0;
	ep->est_size[seq] = -1;
	return;
    }

    ep->level[seq] = lev;

    ep->dumpdate[seq] = stralloc(get_dumpdate(info,lev));
    malloc_mark(ep->dumpdate[seq]);

    stat = &info->inf[lev];
    if(stat->date == EPOCH) ep->est_size[seq] = -1;
    else ep->est_size[seq] = stat->size;

    return;
}

static void
setup_estimate(dp)
     disk_t *dp;
{
    est_t *ep;
    info_t info;
    int i;

    assert(dp && dp->host);
    fprintf(stderr, "%s: time %s: setting up estimates for %s:%s\n",
		    get_pname(), walltime_str(curclock()),
		    dp->host->hostname, dp->name);

    /* get current information about disk */

    if(get_info(dp->host->hostname, dp->name, &info)) {
	/* no record for this disk, make a note of it */
	log_add(L_INFO, "Adding new disk %s:%s.", dp->host->hostname, dp->name);
    }

    /* setup working data struct for disk */

    ep = alloc(sizeof(est_t));
    malloc_mark(ep);
    dp->up = (void *) ep;
    ep->state = DISK_READY;
    ep->dump_size = -1;
    ep->dump_priority = dp->priority;
    ep->errstr = 0;
    ep->promote = 0;

    /* calculated fields */

    if(info.command & FORCE_FULL) {
	/* force a level 0, kind of like a new disk */
	if(dp->strategy == DS_NOFULL) {
	    /*
	     * XXX - Not sure what it means to force a no-full disk.  The
	     * purpose of no-full is to just dump changes relative to a
	     * stable base, for example root partitions that vary only
	     * slightly from a site-wide prototype.  Only the variations
	     * are dumped.
	     *
	     * If we allow a level 0 onto the Amanda cycle, then we are
	     * hosed when that tape gets re-used next.  Disallow this for
	     * now.
	     */
	    log_add(L_ERROR,
		    "Cannot force full dump of %s:%s with no-full option.",
		    dp->host->hostname, dp->name);

	    /* clear force command */
	    if(info.command & FORCE_FULL)
		info.command ^= FORCE_FULL;
	    if(put_info(dp->host->hostname, dp->name, &info))
		error("could not put info record for %s:%s: %s",
		      dp->host->hostname, dp->name, strerror(errno));
	    ep->last_level = last_level(&info);
	    ep->next_level0 = next_level0(dp, &info);
	}
	else {
	    ep->last_level = -1;
	    ep->next_level0 = -conf_dumpcycle;
	    log_add(L_INFO, "Forcing full dump of %s:%s as directed.",
		    dp->host->hostname, dp->name);
	}
    }
    else if(dp->strategy == DS_NOFULL) {
	/* force estimate of level 1 */
	ep->last_level = 1;
	ep->next_level0 = next_level0(dp, &info);
    }
    else {
	ep->last_level = last_level(&info);
	ep->next_level0 = next_level0(dp, &info);
    }

    /* adjust priority levels */

    if(ep->next_level0 < 0) {
	fprintf(stderr,"%s:%s overdue %d day%s for level 0\n",
		dp->host->hostname, dp->name,
		- ep->next_level0, ((- ep->next_level0) == 1) ? "" : "s");
	ep->dump_priority -= ep->next_level0;
	/* warn if dump will be overwritten */
	if(ep->last_level > -1) {
	    int overwrite_runs = when_overwrite(info.inf[0].label);
	    if(overwrite_runs == 0) {
		log_add(L_WARNING,
		  "Last full dump of %s:%s on tape %s overwritten on this run.",
		        dp->host->hostname, dp->name, info.inf[0].label);
	    }
	    else if(overwrite_runs < RUNS_REDZONE) {
		log_add(L_WARNING,
		  "Last full dump of %s:%s on tape %s overwritten in %d run%s.",
		        dp->host->hostname, dp->name, info.inf[0].label,
		    overwrite_runs, overwrite_runs == 1? "" : "s");
	    }
	}
    }
    else if(info.command & FORCE_FULL)
	ep->dump_priority += 1;
    /* else XXX bump up the priority of incrementals that failed last night */

    /* handle external level 0 dumps */

    if(dp->skip_full && dp->strategy != DS_NOINC) {
	if(ep->next_level0 <= 0) {
	    /* update the date field */
	    info.inf[0].date = today;
	    if(info.command & FORCE_FULL)
		info.command ^= FORCE_FULL;
	    ep->next_level0 += conf_dumpcycle;
	    ep->last_level = 0;
	    if(put_info(dp->host->hostname, dp->name, &info))
		error("could not put info record for %s:%s: %s",
		      dp->host->hostname, dp->name, strerror(errno));
	    log_add(L_INFO, "Skipping full dump of %s:%s today.",
		    dp->host->hostname, dp->name);
	    fprintf(stderr,"%s:%s lev 0 skipped due to skip-full flag\n",
		    dp->host->hostname, dp->name);
	    /* don't enqueue the disk */
	    askfor(ep, 0, -1, &info);
	    askfor(ep, 1, -1, &info);
	    askfor(ep, 2, -1, &info);
	    fprintf(stderr, "%s: SKIPPED %s %s 0 [skip-full]\n",
		    get_pname(), dp->host->hostname, dp->name);
	    log_add(L_SUCCESS, "%s %s %s 0 [skipped: skip-full]",
		    dp->host->hostname, dp->name, datestamp);
	    return;
	}

	if(ep->last_level == -1) {
	    /* probably a new disk, but skip-full means no full! */
	    ep->last_level = 0;
	}

	if(ep->next_level0 == 1) {
	    log_add(L_WARNING, "Skipping full dump of %s:%s tomorrow.",
		    dp->host->hostname, dp->name);
	}
    }

    /* handle "skip-incr" type archives */

    if(dp->skip_incr && ep->next_level0 > 0) {
	fprintf(stderr,"%s:%s lev 1 skipped due to skip-incr flag\n",
		dp->host->hostname, dp->name);
	/* don't enqueue the disk */
	askfor(ep, 0, -1, &info);
	askfor(ep, 1, -1, &info);
	askfor(ep, 2, -1, &info);

	fprintf(stderr, "%s: SKIPPED %s %s 1 [skip-incr]\n",
		get_pname(), dp->host->hostname, dp->name);

	log_add(L_SUCCESS, "%s %s %s 1 [skipped: skip-incr]",
	        dp->host->hostname, dp->name, datestamp);
	return;
    }

    if( ep->last_level == -1 && ep->next_level0 > 0 && 
	dp->strategy != DS_NOFULL && dp->strategy != DS_INCRONLY &&
	conf_reserve == 100) {
	log_add(L_WARNING,
	     "%s:%s mismatch: no tapelist record, but curinfo next_level0: %d.",
	        dp->host->hostname, dp->name, ep->next_level0);
	ep->next_level0 = 0;
    }

    if(ep->last_level == 0) ep->level_days = 0;
    else ep->level_days = runs_at(&info, ep->last_level);
    ep->last_lev0size = info.inf[0].csize;

    ep->fullrate = perf_average(info.full.rate, 0.0);
    ep->incrrate = perf_average(info.incr.rate, 0.0);

    ep->fullcomp = perf_average(info.full.comp, dp->comprate[0]);
    ep->incrcomp = perf_average(info.incr.comp, dp->comprate[1]);

    /* determine which estimates to get */

    i = 0;

    if(dp->strategy == DS_NOINC ||
       (!dp->skip_full &&
        (!(info.command & FORCE_BUMP) ||
         dp->skip_incr ||
         ep->last_level == -1))){

	if(info.command & FORCE_BUMP && ep->last_level == -1) {
	    log_add(L_INFO,
		  "Remove force-bump command of %s:%s because it's a new disk.",
		    dp->host->hostname, dp->name);
	}
	switch (dp->strategy) {
	case DS_STANDARD: 
	case DS_NOINC:
	    askfor(ep, i++, 0, &info);
	    if(dp->skip_full) {
		log_add(L_INFO,
                  "Ignoring skip_full for %s:%s because the strategy is NOINC.",
			dp->host->hostname, dp->name);
	    }
	    if(info.command & FORCE_BUMP) {
		log_add(L_INFO,
		 "Ignoring FORCE_BUMP for %s:%s because the strategy is NOINC.",
			dp->host->hostname, dp->name);
	    }
	    
	    break;

	case DS_NOFULL:
	    break;

	case DS_INCRONLY:
	    if (info.command & FORCE_FULL)
		askfor(ep, i++, 0, &info);
	    break;
	}
    }

    if(!dp->skip_incr && !(dp->strategy == DS_NOINC)) {
	if(ep->last_level == -1) {		/* a new disk */
	    if(dp->strategy == DS_NOFULL || dp->strategy == DS_INCRONLY) {
		askfor(ep, i++, 1, &info);
	    } else {
		assert(!dp->skip_full);		/* should be handled above */
	    }
	} else {				/* not new, pick normally */
	    int curr_level;

	    curr_level = ep->last_level;

	    if(info.command & FORCE_NO_BUMP) {
		if(curr_level > 0) { /* level 0 already asked for */
		    askfor(ep, i++, curr_level, &info);
		}
		log_add(L_INFO,"Preventing bump of %s:%s as directed.",
			dp->host->hostname, dp->name);
	    }
	    else if((info.command & FORCE_BUMP)
		    && curr_level + 1 < DUMP_LEVELS) {
		askfor(ep, i++, curr_level+1, &info);
		log_add(L_INFO,"Bumping of %s:%s at level %d as directed.",
			dp->host->hostname, dp->name, curr_level+1);
	    }
	    else if(curr_level == 0) {
		askfor(ep, i++, 1, &info);
	    }
	    else {
		askfor(ep, i++, curr_level, &info);
		/*
		 * If last time we dumped less than the threshold, then this
		 * time we will too, OR the extra size will be charged to both
		 * cur_level and cur_level + 1, so we will never bump.  Also,
		 * if we haven't been at this level 2 days, or the dump failed
		 * last night, we can't bump.
		 */
		if((info.inf[curr_level].size == 0 || /* no data, try it anyway */
		    (((info.inf[curr_level].size > bump_thresh(curr_level)))
		     && ep->level_days >= conf_bumpdays))
		   && curr_level + 1 < DUMP_LEVELS) {
		    askfor(ep, i++, curr_level+1, &info);
		}
	    } 
	}
    }

    while(i < MAX_LEVELS) 	/* mark end of estimates */
	askfor(ep, i++, -1, &info);

    /* debug output */

    fprintf(stderr, "setup_estimate: %s:%s: command %d, options:",
	    dp->host->hostname, dp->name, info.command);
    if(dp->strategy == DS_NOFULL) fputs(" no-full", stderr);
    if(dp->strategy == DS_INCRONLY) fputs(" incr-only", stderr);
    if(dp->skip_full) fputs(" skip-full", stderr);
    if(dp->skip_incr) fputs(" skip-incr", stderr);
    fprintf(stderr, "\n    last_level %d next_level0 %d level_days %d\n",
	    ep->last_level, ep->next_level0, ep->level_days);
    fprintf(stderr, "    getting estimates %d (%ld) %d (%ld) %d (%ld)\n",
	    ep->level[0], ep->est_size[0],
	    ep->level[1], ep->est_size[1],
	    ep->level[2], ep->est_size[2]);

    assert(ep->level[0] != -1);
    enqueue_disk(&startq, dp);
}

static int when_overwrite(label)
char *label;
{
    tape_t *tp;

    if((tp = lookup_tapelabel(label)) == NULL)
	return 1;	/* "shouldn't happen", but trigger warning message */
    else if(!reusable_tape(tp))
	return 1024;
    else if(lookup_nb_tape() > conf_tapecycle)
	return (lookup_nb_tape() - tp->position) / conf_runtapes;
    else
	return (conf_tapecycle - tp->position) / conf_runtapes;
}

/* Return the estimated size for a particular dump */
static long est_size(dp, level)
disk_t *dp;
int level;
{
    int i;

    for(i = 0; i < MAX_LEVELS; i++) {
	if(level == est(dp)->level[i])
	    return est(dp)->est_size[i];
    }
    return -1;
}

/* Return the estimated on-tape size of a particular dump */
static long est_tape_size(dp, level)
disk_t *dp;
int level;
{
    long size;
    double ratio;

    size = est_size(dp, level);

    if(size == -1) return size;

    if(dp->compress == COMP_NONE)
	return size;

    if(level == 0) ratio = est(dp)->fullcomp;
    else ratio = est(dp)->incrcomp;

    /*
     * make sure over-inflated compression ratios don't throw off the
     * estimates, this is mostly for when you have a small dump getting
     * compressed which takes up alot more disk/tape space relatively due
     * to the overhead of the compression.  This is specifically for
     * Digital Unix vdump.  This patch is courtesy of Rudolf Gabler
     * (RUG@USM.Uni-Muenchen.DE)
     */

    if(ratio > 1.1) ratio = 1.1;

    size *= ratio;

    /*
     * Ratio can be very small in some error situations, so make sure
     * size goes back greater than zero.  It may not be right, but
     * indicates we did get an estimate.
     */
    if(size <= 0) {
	size = 1;
    }

    return size;
}


/* what was the level of the last successful dump to tape? */
static int last_level(info)
info_t *info;
{
    int min_pos, min_level, i;
    time_t lev0_date, last_date;
    tape_t *tp;

    if(info->last_level != -1)
	return info->last_level;

    /* to keep compatibility with old infofile */
    min_pos = 1000000000;
    min_level = -1;
    lev0_date = EPOCH;
    last_date = EPOCH;
    for(i = 0; i < 9; i++) {
	if(conf_reserve < 100) {
	    if(i == 0) lev0_date = info->inf[0].date;
	    else if(info->inf[i].date < lev0_date) continue;
	    if(info->inf[i].date > last_date) {
		last_date = info->inf[i].date;
		min_level = i;
	    }
	}
	else {
	    if((tp = lookup_tapelabel(info->inf[i].label)) == NULL) continue;
	    /* cull any entries from previous cycles */
	    if(i == 0) lev0_date = info->inf[0].date;
	    else if(info->inf[i].date < lev0_date) continue;

	    if(tp->position < min_pos) {
		min_pos = tp->position;
		min_level = i;
	    }
	}
    }
    info->last_level = i;
    return min_level;
}

/* when is next level 0 due? 0 = today, 1 = tomorrow, etc*/
static int
next_level0(dp, info)
     disk_t *dp;
     info_t *info;
{
    if(dp->strategy == DS_NOFULL || dp->strategy == DS_INCRONLY)
	return 1;		/* fake it */
    else if (dp->strategy == DS_NOINC)
	return 0;
    else if(info->inf[0].date < (time_t)0)
	return -days_diff(EPOCH, today);	/* new disk */
    else
	return dp->dumpcycle - days_diff(info->inf[0].date, today);
}

/* how many runs at current level? */
static int runs_at(info, lev)
info_t *info;
int lev;
{
    tape_t *cur_tape, *old_tape;
    int last, nb_runs;

    last = last_level(info);
    if(lev != last) return 0;
    if(lev == 0) return 1;

    if(info->consecutive_runs != -1)
	return info->consecutive_runs;

    /* to keep compatibility with old infofile */
    cur_tape = lookup_tapelabel(info->inf[lev].label);
    old_tape = lookup_tapelabel(info->inf[lev-1].label);
    if(cur_tape == NULL || old_tape == NULL) return 0;

    nb_runs = (old_tape->position - cur_tape->position) / conf_runtapes;
    info->consecutive_runs = nb_runs;

    return nb_runs;
}


static long bump_thresh(level)
int level;
{
    double bump;

    bump = conf_bumpsize;
    while(--level) bump = bump * conf_bumpmult;

    return (long)bump;
}



/*
 * ========================================================================
 * GET REMOTE DUMP SIZE ESTIMATES
 *
 */

static void getsize P((host_t *hostp));
static disk_t *lookup_hostdisk P((host_t *hp, char *str));
static void handle_result P((proto_t *p, pkt_t *pkt));


static void get_estimates P((void))
{
    host_t *hostp;
    disk_t *dp;
    struct servent *amandad;
    int something_started;

    if((amandad = getservbyname(AMANDA_SERVICE_NAME, "udp")) == NULL)
	amanda_port = AMANDA_SERVICE_DEFAULT;
    else
	amanda_port = ntohs(amandad->s_port);

#ifdef KRB4_SECURITY
    if((amandad = getservbyname(KAMANDA_SERVICE_NAME, "udp")) == NULL)
	kamanda_port = KAMANDA_SERVICE_DEFAULT;
    else
	kamanda_port = ntohs(amandad->s_port);
#endif

    something_started = 1;
    while(something_started) {
	something_started = 0;
	for(dp = startq.head; dp != NULL; dp = dp->next) {
	    hostp = dp->host;
	    if(hostp->up == HOST_READY) {
		something_started = 1;
		getsize(hostp);
		check_protocol();
		/*
		 * dp is no longer on startq, so dp->next is not valid
		 * and we have to start all over.
		 */
		break;
	    }
	}
    }
    run_protocol();

    while(!empty(waitq)) {
	disk_t *dp = dequeue_disk(&waitq);
	est(dp)->errstr = "hmm, disk was stranded on waitq";
	enqueue_disk(&failq, dp);
    }
}

static void getsize(hostp)
host_t *hostp;
{
    disklist_t *destqp;
    disk_t *dp;
    char *req = NULL, *errstr = NULL;
    int i, estimates, rc, timeout, disk_state, req_len;
    char number[NUM_STR_SIZE];

    assert(hostp->disks != NULL);

    if(hostp->up != HOST_READY) {
	return;
    }

    /*
     * The first time through here we send a "noop" request.  This will
     * return the feature list from the client if it supports that.
     * If it does not, handle_result() will set the feature list to an
     * empty structure.  In either case, we do the disks on the second
     * (and subsequent) pass(es).
     */

    if(hostp->features != NULL) { /* sendsize service */
	int has_features = am_has_feature(hostp->features,
					  fe_req_options_features);
	int has_hostname = am_has_feature(hostp->features,
					  fe_req_options_hostname);
	int has_maxdumps = am_has_feature(hostp->features,
					  fe_req_options_maxdumps);

	ap_snprintf(number, sizeof(number), "%d", hostp->maxdumps);
	req = vstralloc("SERVICE ", "sendsize", "\n",
			"OPTIONS ",
			has_features ? "features=" : "",
			has_features ? our_feature_string : "",
			has_features ? ";" : "",
			has_maxdumps ? "maxdumps=" : "",
			has_maxdumps ? number : "",
			has_maxdumps ? ";" : "",
			has_hostname ? "hostname=" : "",
			has_hostname ? hostp->hostname : "",
			has_hostname ? ";" : "",
			"\n",
			NULL);
	req_len = strlen(req);
	req_len += 128;                             /* room for SECURITY ... */
	estimates = 0;
	for(dp = hostp->disks; dp != NULL; dp = dp->hostnext) {
	    char *s = NULL;
	    int s_len = 0;
    
	    if(dp->todo == 0) continue;
    
	    if(est(dp)->state != DISK_READY) {
		continue;
	    }
	    est(dp)->got_estimate = 0;
	    if(est(dp)->level[0] == -1) {
		est(dp)->state = DISK_DONE;
		continue;   /* ignore this disk */
	    }
    
	    for(i = 0; i < MAX_LEVELS; i++) {
		char *l;
		char *exclude1 = "";
		char *exclude2 = "";
		char *excludefree = NULL;
		char spindle[NUM_STR_SIZE];
		char level[NUM_STR_SIZE];
		int lev = est(dp)->level[i];
    
		if(lev == -1) break;
    
		ap_snprintf(level, sizeof(level), "%d", lev);
		ap_snprintf(spindle, sizeof(spindle), "%d", dp->spindle);
		if(am_has_feature(hostp->features, fe_sendsize_req_options)) {
		    exclude1 = " OPTIONS |";
		    exclude2 = optionstr(dp, hostp->features, NULL);
		    excludefree = exclude2;
		}
		else {
		    if(dp->exclude_file && dp->exclude_file->nb_element == 1) {
			exclude1 = " exclude-file=";
			exclude2 = dp->exclude_file->first->name;
		    }
		    else if(dp->exclude_list
			    && dp->exclude_list->nb_element == 1) {
			exclude1 = " exclude-list=";
			exclude2 = dp->exclude_list->first->name;
		    }
		}
		if(dp->device) {
		    l = vstralloc(dp->program, " ",
				  dp->name, " ",
				  dp->device, " ",
				  level, " ",
				  est(dp)->dumpdate[i], " ", spindle,
				  exclude1,
				  exclude2,
				  "\n",
				  NULL);
		}
		else {
		    l = vstralloc(dp->program, " ", dp->name, " ", level, " ",
				  est(dp)->dumpdate[i], " ", spindle,
				  exclude1,
				  exclude2,
				  "\n",
				  NULL);
		}
		amfree(excludefree);
		strappend(s, l);
		s_len += strlen(l);
		amfree(l);
	    }
	    /*
	     * Allow 2X for err response.
	     */
	    if(req_len + s_len > MAX_DGRAM / 2) {
		amfree(s);
		break;
	    }
	    estimates += i;
	    strappend(req, s);
	    req_len += s_len;
	    amfree(s);
	    est(dp)->state = DISK_ACTIVE;
	    remove_disk(&startq, dp);
	}
    
	if(estimates == 0) {
	    amfree(req);
	    hostp->up = HOST_DONE;
	    return;
	}

	if (conf_etimeout < 0) {
	    timeout = - conf_etimeout;
	} else {
	    timeout = estimates * conf_etimeout;
	}
    } else { /* noop service */
	req = vstralloc("SERVICE ", "noop", "\n",
			"OPTIONS ",
			"features=", our_feature_string, ";",
			"\n",
			NULL);
	/*
	 * We use ctimeout for the "noop" request because it should be
	 * very fast and etimeout has other side effects.
	 */
	timeout = getconf_int(CNF_CTIMEOUT);
    }

#ifdef KRB4_SECURITY
    if(hostp->disks->auth == AUTH_KRB4)
	rc = make_krb_request(hostp->hostname, kamanda_port, req,
			      hostp, timeout, handle_result);
    else
#endif
	rc = make_request(hostp->hostname, amanda_port, req,
			  hostp, timeout, handle_result);

    req = NULL;					/* do not own this any more */

    if(rc) {
	errstr = vstralloc("could not resolve hostname \"",
			   hostp->hostname,
			   "\"",
			   NULL);
	destqp = &failq;
	hostp->up = HOST_DONE;
	disk_state = DISK_DONE;
    }
    else {
	errstr = NULL;
	destqp = &waitq;
	hostp->up = HOST_ACTIVE;
	disk_state = DISK_ACTIVE;
    }

    for(dp = hostp->disks; dp != NULL; dp = dp->hostnext) {
	if(dp->todo && est(dp)->state == DISK_ACTIVE) {
	    est(dp)->state = disk_state;
	    est(dp)->errstr = errstr;
	    errstr = NULL;
	    enqueue_disk(destqp, dp);
	}
    }
    amfree(errstr);
}

static disk_t *lookup_hostdisk(hp, str)
host_t *hp;
char *str;
{
    disk_t *dp;

    for(dp = hp->disks; dp != NULL; dp = dp->hostnext)
	if(strcmp(str, dp->name) == 0) return dp;

    return NULL;
}


static void handle_result(p, pkt)
proto_t *p;
pkt_t *pkt;
{
    int level, i;
    long size;
    disk_t *dp;
    host_t *hostp;
    char *msgdisk=NULL, *msgdisk_undo=NULL, msgdisk_undo_ch = '\0';
    char *errbuf = NULL;
    char *line;
    char *fp;
    char *s;
    char *t;
    int ch;
    int tch;

    hostp = (host_t *) p->datap;
    hostp->up = HOST_READY;

    if(p->state == S_FAILED && pkt == NULL) {
	if(p->prevstate == S_REPWAIT) {
	    errbuf = vstralloc("Estimate timeout from ", hostp->hostname,
			       NULL);
	}
	else {
	    errbuf = vstralloc("Request to ", hostp->hostname, " timed out.",
			       NULL);
	}
	goto error_return;
    }

#if 0
    fprintf(stderr, "got %sresponse from %s:\n----\n%s----\n\n",
	    (p->state == S_FAILED) ? "NAK " : "", hostp->hostname, pkt->body);
#endif

#ifdef KRB4_SECURITY
    if(hostp->disks->auth == AUTH_KRB4 &&
       !check_mutual_authenticator(host2key(hostp->hostname), pkt, p)) {
	errbuf = vstralloc(hostp->hostname,
			   "[mutual-authentication failed]",
			   NULL);
	goto error_return;
    }
#endif

    msgdisk_undo = NULL;
    s = pkt->body;
    ch = *s++;
    while(ch) {
	line = s - 1;
	skip_line(s, ch);
	if (s[-2] == '\n') {
	    s[-2] = '\0';
	}

#define sc "OPTIONS "
	if(strncmp(line, sc, sizeof(sc)-1) == 0) {
#undef sc

#define sc "features="
	    t = strstr(line, sc);
	    if(t != NULL && (isspace((int)t[-1]) || t[-1] == ';')) {
		t += sizeof(sc)-1;
#undef sc
		am_release_feature_set(hostp->features);
		if((hostp->features = am_string_to_feature(t)) == NULL) {
		    errbuf = vstralloc(hostp->hostname,
				       ": bad features value: ",
				       line,
				       "\n",
				       NULL);
		    goto error_return;
		}
	    }

	    continue;
	}

#define sc "ERROR "
	if(strncmp(line, sc, sizeof(sc)-1) == 0) {
	    t = line + sizeof(sc)-1;
	    tch = t[-1];
#undef sc

	    fp = t - 1;
	    skip_whitespace(t, tch);
	    if (tch == '\n') {
		t[-1] = '\0';
	    }
	    /*
	     * If the "error" is that the "noop" service is unknown, it
	     * just means the client is "old" (does not support the servie).
	     * We can ignore this.
	     */
	    if(hostp->features == NULL
	       && p->state == S_FAILED
	       && (strcmp(t - 1, "unknown service: noop") == 0
	           || strcmp(t - 1, "noop: invalid service") == 0)) {
		continue;
	    } else {
		errbuf = vstralloc(hostp->hostname,
			           (p->state == S_FAILED) ? "NAK " : "",
			           ": ",
			           fp,
			           NULL);
	        goto error_return;
	    }
	}

	msgdisk = t = line;
	tch = *t++;
	skip_non_whitespace(t, tch);
	msgdisk_undo = t - 1;
	msgdisk_undo_ch = *msgdisk_undo;
	*msgdisk_undo = '\0';

	skip_whitespace(t, tch);
	if (sscanf(t - 1, "%d SIZE %ld", &level, &size) != 2) {
	    goto bad_msg;
	}

	dp = lookup_hostdisk(hostp, msgdisk);

	*msgdisk_undo = msgdisk_undo_ch;	/* for error message */
	msgdisk_undo = NULL;

	if(dp == NULL) {
	    log_add(L_ERROR, "%s: invalid reply from sendsize: `%s'\n",
		    hostp->hostname, line);
	} else {
	    for(i = 0; i < MAX_LEVELS; i++) {
		if(est(dp)->level[i] == level) {
		    est(dp)->est_size[i] = size;
		    break;
		}
	    }
	    if(i == MAX_LEVELS) {
		goto bad_msg;			/* this est wasn't requested */
	    }
	    est(dp)->got_estimate++;
	}
    }

    if(hostp->up == HOST_READY && hostp->features == NULL) {
	/*
	 * The client does not support the features list, so give it an
	 * empty one.
	 */
	dbprintf(("%s: no feature set from host %s\n",
		  debug_prefix_time(NULL), hostp->hostname));
	hostp->features = am_set_default_feature_set();
    }

    /* XXX what about disks that only got some estimates...  do we care? */
    /* XXX amanda 2.1 treated that case as a bad msg */

    for(dp = hostp->disks; dp != NULL; dp = dp->hostnext) {
	if(dp->todo == 0) continue;
	if(est(dp)->state != DISK_ACTIVE) continue;
	est(dp)->state = DISK_DONE;
	if(est(dp)->level[0] == -1) continue;   /* ignore this disk */
	remove_disk(&waitq, dp);
	if(est(dp)->got_estimate) {
	    fprintf(stderr,"%s: time %s: got result for host %s disk %s:",
		    get_pname(), walltime_str(curclock()),
		    dp->host->hostname, dp->name);
	    fprintf(stderr," %d -> %ldK, %d -> %ldK, %d -> %ldK\n",
		    est(dp)->level[0], est(dp)->est_size[0],
		    est(dp)->level[1], est(dp)->est_size[1],
		    est(dp)->level[2], est(dp)->est_size[2]);

	    if((est(dp)->level[0] != -1 && est(dp)->est_size[0] > 0) ||
	       (est(dp)->level[1] != -1 && est(dp)->est_size[1] > 0) ||
	       (est(dp)->level[2] != -1 && est(dp)->est_size[2] > 0)) {

		if(est(dp)->level[2] != -1 && est(dp)->est_size[2] < 0) {
		    log_add(L_WARNING,
			    "disk %s:%s, estimate of level %d failed: %d.",
			    dp->host->hostname, dp->name,
			    est(dp)->level[2], est(dp)->est_size[2]);
		    est(dp)->level[2] = -1;
		}
		if(est(dp)->level[1] != -1 && est(dp)->est_size[1] < 0) {
		    log_add(L_WARNING,
			    "disk %s:%s, estimate of level %d failed: %d.",
			    dp->host->hostname, dp->name,
			    est(dp)->level[1], est(dp)->est_size[1]);
		    est(dp)->level[1] = -1;
		}
		if(est(dp)->level[0] != -1 && est(dp)->est_size[0] < 0) {
		    log_add(L_WARNING,
			    "disk %s:%s, estimate of level %d failed: %d.",
			    dp->host->hostname, dp->name,
			    est(dp)->level[0], est(dp)->est_size[0]);
		    est(dp)->level[0] = -1;
		}

		enqueue_disk(&estq, dp);
	    }
	    else {
		enqueue_disk(&failq, dp);
		est(dp)->errstr = vstralloc("disk ", dp->name,
					    ", all estimate failed", NULL);
	    }
	}
	else {
	    enqueue_disk(&failq, dp);

	    fprintf(stderr, "error result for host %s disk %s: missing estimate\n",
		dp->host->hostname, dp->name);

	    est(dp)->errstr = vstralloc("missing result for ", dp->name,
					" in ", dp->host->hostname, " response",
					NULL);
	}
    }
    getsize(hostp);
    return;

 bad_msg:

    if(msgdisk_undo) {
	*msgdisk_undo = msgdisk_undo_ch;
	msgdisk_undo = NULL;
    }
    fprintf(stderr,"got a bad message, stopped at:\n");
    fprintf(stderr,"----\n%s\n----\n\n", line);
    errbuf = stralloc2("badly formatted response from ", hostp->hostname);
    /* fall through to ... */

 error_return:

    if(msgdisk_undo) {
	*msgdisk_undo = msgdisk_undo_ch;
	msgdisk_undo = NULL;
    }
    i = 0;
    for(dp = hostp->disks; dp != NULL; dp = dp->hostnext) {
	if(est(dp)->state == DISK_ACTIVE) {
	    est(dp)->state = DISK_DONE;
	    remove_disk(&waitq, dp);
	    enqueue_disk(&failq, dp);
	    i++;

	    est(dp)->errstr = stralloc(errbuf);
	    fprintf(stderr, "%s: time %s: error result for host %s disk %s: %s\n",
	        get_pname(), walltime_str(curclock()),
		dp->host->hostname, dp->name, errbuf);
	}
    }
    if(i == 0) {
	/*
	 * If there were no disks involved, make sure the error gets
	 * reported.
	 */
	log_add(L_ERROR, "%s", errbuf);
    }
    hostp->up = HOST_DONE;
    amfree(errbuf);
}




/*
 * ========================================================================
 * ANALYSE ESTIMATES
 *
 */

static int schedule_order P((disk_t *a, disk_t *b));	  /* subroutines */
static int pick_inclevel P((disk_t *dp));

static void analyze_estimate(dp)
disk_t *dp;
{
    est_t *ep;
    info_t info;
    int have_info = 0;

    ep = est(dp);

    fprintf(stderr, "pondering %s:%s... ",
	    dp->host->hostname, dp->name);
    fprintf(stderr, "next_level0 %d last_level %d ",
	    ep->next_level0, ep->last_level);

    if(get_info(dp->host->hostname, dp->name, &info) == 0) {
	have_info = 1;
    }

    ep->degr_level = -1;
    ep->degr_size = -1;

    if(ep->next_level0 <= 0
       || (have_info && ep->last_level == 0 && (info.command & FORCE_NO_BUMP))) {
	if(ep->next_level0 <= 0) {
	    fprintf(stderr,"(due for level 0) ");
	}
	ep->dump_level = 0;
	ep->dump_size = est_tape_size(dp, 0);
	if(ep->dump_size <= 0) {
	    fprintf(stderr,
		    "(no estimate for level 0, picking an incr level)\n");
	    ep->dump_level = pick_inclevel(dp);
	    ep->dump_size = est_tape_size(dp, ep->dump_level);

	    if(ep->dump_size == -1) {
		ep->dump_level = ep->dump_level + 1;
		ep->dump_size = est_tape_size(dp, ep->dump_level);
	    }
	}
	else {
	    total_lev0 += (double) ep->dump_size;
	    if(ep->last_level == -1 || dp->skip_incr) {
		fprintf(stderr,"(%s disk, can't switch to degraded mode)\n",
			dp->skip_incr? "skip-incr":"new");
		ep->degr_level = -1;
		ep->degr_size = -1;
	    }
	    else {
		/* fill in degraded mode info */
		fprintf(stderr,"(picking inclevel for degraded mode)");
		ep->degr_level = pick_inclevel(dp);
		ep->degr_size = est_tape_size(dp, ep->degr_level);
		if(ep->degr_size == -1) {
		    ep->degr_level = ep->degr_level + 1;
		    ep->degr_size = est_tape_size(dp, ep->degr_level);
		}
		if(ep->degr_size == -1) {
		    fprintf(stderr,"(no inc estimate)");
		    ep->degr_level = -1;
		}
		fprintf(stderr,"\n");
	    }
	}
    }
    else {
	fprintf(stderr,"(not due for a full dump, picking an incr level)\n");
	/* XXX - if this returns -1 may be we should force a total? */
	ep->dump_level = pick_inclevel(dp);
	ep->dump_size = est_tape_size(dp, ep->dump_level);

	if(ep->dump_size == -1) {
	    ep->dump_level = ep->last_level;
	    ep->dump_size = est_tape_size(dp, ep->dump_level);
	}
	if(ep->dump_size == -1) {
	    ep->dump_level = ep->last_level + 1;
	    ep->dump_size = est_tape_size(dp, ep->dump_level);
	}
	if(ep->dump_size == -1) {
	    ep->dump_level = 0;
	    ep->dump_size = est_tape_size(dp, ep->dump_level);
	}
    }

    fprintf(stderr,"  curr level %d size %ld ", ep->dump_level, ep->dump_size);

    insert_disk(&schedq, dp, schedule_order);

    total_size += tt_blocksize_kb + ep->dump_size + tape_mark;

    /* update the balanced size */
    if(!(dp->skip_full || dp->strategy == DS_NOFULL || 
	 dp->strategy == DS_INCRONLY)) {
	long lev0size;

	lev0size = est_tape_size(dp, 0);
	if(lev0size == -1) lev0size = ep->last_lev0size;

	balanced_size += lev0size / runs_per_cycle;
    }

    fprintf(stderr,"total size %ld total_lev0 %1.0f balanced-lev0size %1.0f\n",
	    total_size, total_lev0, balanced_size);
}

static void handle_failed(dp)
disk_t *dp;
{
    char *errstr;

/*
 * From George Scott <George.Scott@cc.monash.edu.au>:
 * --------
 * If a machine is down when the planner is run it guesses from historical
 * data what the size of tonights dump is likely to be and schedules a
 * dump anyway.  The dumper then usually discovers that that machine is
 * still down and ends up with a half full tape.  Unfortunately the
 * planner had to delay another dump because it thought that the tape was
 * full.  The fix here is for the planner to ignore unavailable machines
 * rather than ignore the fact that they are unavailable.
 * --------
 */

#ifdef old_behavior
    if(est(dp)->last_level != -1) {
	log_add(L_WARNING,
	        "Could not get estimate for %s:%s, using historical data.",
	        dp->host->hostname, dp->name);
	analyze_estimate(dp);
	return;
    }
#endif

    errstr = est(dp)->errstr? est(dp)->errstr : "hmm, no error indicator!";

    fprintf(stderr, "%s: FAILED %s %s %s 0 [%s]\n",
	get_pname(), dp->host->hostname, dp->name, datestamp, errstr);

    log_add(L_FAIL, "%s %s %s 0 [%s]", dp->host->hostname, dp->name, datestamp, errstr);

    /* XXX - memory leak with *dp */
}


static int schedule_order(a, b)
disk_t *a, *b;
/*
 * insert-sort by decreasing priority, then
 * by decreasing size within priority levels.
 */
{
    int diff;
    long ldiff;

    diff = est(b)->dump_priority - est(a)->dump_priority;
    if(diff != 0) return diff;

    ldiff = est(b)->dump_size - est(a)->dump_size;
    if(ldiff < 0) return -1; /* XXX - there has to be a better way to dothis */
    if(ldiff > 0) return 1;
    return 0;
}


static int pick_inclevel(dp)
disk_t *dp;
{
    int base_level, bump_level;
    long base_size, bump_size;
    long thresh;

    base_level = est(dp)->last_level;

    /* if last night was level 0, do level 1 tonight, no ifs or buts */
    if(base_level == 0) {
	fprintf(stderr,"   picklev: last night 0, so tonight level 1\n");
	return 1;
    }

    /* if no-full option set, always do level 1 */
    if(dp->strategy == DS_NOFULL) {
	fprintf(stderr,"   picklev: no-full set, so always level 1\n");
	return 1;
    }

    base_size = est_size(dp, base_level);

    /* if we didn't get an estimate, we can't do an inc */
    if(base_size == -1) {
	base_size = est_size(dp, base_level+1);
	if(base_size > 0) /* FORCE_BUMP */
	    return base_level+1;
	fprintf(stderr,"   picklev: no estimate for level %d, so no incs\n", base_level);
	return base_level;
    }

    thresh = bump_thresh(base_level);

    fprintf(stderr,
	    "   pick: size %ld level %d days %d (thresh %ldK, %d days)\n",
	    base_size, base_level, est(dp)->level_days,
	    thresh, conf_bumpdays);

    if(base_level == 9
       || est(dp)->level_days < conf_bumpdays
       || base_size <= thresh)
	    return base_level;

    bump_level = base_level + 1;
    bump_size = est_size(dp, bump_level);

    if(bump_size == -1) return base_level;

    fprintf(stderr, "   pick: next size %ld... ", bump_size);

    if(base_size - bump_size < thresh) {
	fprintf(stderr, "not bumped\n");
	return base_level;
    }

    fprintf(stderr, "BUMPED\n");
    log_add(L_INFO, "Incremental of %s:%s bumped to level %d.",
	    dp->host->hostname, dp->name, bump_level);

    return bump_level;
}




/*
** ========================================================================
** ADJUST SCHEDULE
**
** We have two strategies here:
**
** 1. Delay dumps
**
** If we are trying to fit too much on the tape something has to go.  We
** try to delay totals until tomorrow by converting them into incrementals
** and, if that is not effective enough, dropping incrementals altogether.
** While we are searching for the guilty dump (the one that is really
** causing the schedule to be oversize) we have probably trampled on a lot of
** innocent dumps, so we maintain a "before image" list and use this to
** put back what we can.
**
** 2. Promote dumps.
**
** We try to keep the amount of tape used by total dumps the same each night.
** If there is some spare tape in this run we have a look to see if any of
** tonights incrementals could be promoted to totals and leave us with a
** more balanced cycle.
*/

static void delay_one_dump P((disk_t *dp, int delete, ...));

static void delay_dumps P((void))
/* delay any dumps that will not fit */
{
    disk_t *dp, *ndp, *preserve;
    bi_t *bi, *nbi;
    long new_total;	/* New total_size */
    char est_kb[20];     /* Text formatted dump size */
    int nb_forced_level_0;
    info_t info;

    biq.head = biq.tail = NULL;

    /*
    ** 1. Delay dumps that are way oversize.
    **
    ** Dumps larger that the size of the tapes we are using are just plain
    ** not going to fit no matter how many other dumps we drop.  Delay
    ** oversize totals until tomorrow (by which time my owner will have
    ** resolved the problem!) and drop incrementals altogether.  Naturally
    ** a large total might be delayed into a large incremental so these
    ** need to be checked for separately.
    */

    for(dp = schedq.head; dp != NULL; dp = ndp) {
	ndp = dp->next; /* remove_disk zaps this */

	if (est(dp)->dump_size <= tape->length) {
	    continue;
	}

        /* Format dumpsize for messages */
	ap_snprintf(est_kb, 20, "%ld KB,", est(dp)->dump_size);

	if(est(dp)->dump_level == 0) {
	    if(est(dp)->last_level == -1 || dp->skip_incr) {
		delay_one_dump(dp, 1,
			       "dump larger than tape,",
			       est_kb,
			       "but cannot incremental dump",
			       dp->skip_incr ? "skip-incr": "new",
			       "disk",
			       NULL);
	    }
	    else {
		delay_one_dump(dp, 0,
			       "dump larger than tape,",
			       est_kb,
			       "full dump delayed",
			       NULL);
	    }
	}
	else {
	    delay_one_dump(dp, 1,
			   "dump larger than tape,",
			   est_kb,
			   "skipping incremental",
			   NULL);
	}
    }

    /*
    ** 2. Delay total dumps.
    **
    ** Delay total dumps until tomorrow (or the day after!).  We start with
    ** the lowest priority (most dispensable) and work forwards.  We take
    ** care not to delay *all* the dumps since this could lead to a stale
    ** mate [for any one disk there are only three ways tomorrows dump will
    ** be smaller than todays: 1. we do a level 0 today so tomorows dump
    ** will be a level 1; 2. the disk gets more data so that it is bumped
    ** tomorrow (this can be a slow process); and, 3. the disk looses some
    ** data (when does that ever happen?)].
    */

    nb_forced_level_0 = 0;
    preserve = NULL;
    for(dp = schedq.head; dp != NULL && preserve == NULL; dp = dp->next)
	if(est(dp)->dump_level == 0)
	    preserve = dp;

    /* 2.a. Do not delay forced full */
    for(dp = schedq.tail;
		dp != NULL && total_size > tape_length;
		dp = ndp) {
	ndp = dp->prev;

	if(est(dp)->dump_level != 0) continue;

	get_info(dp->host->hostname, dp->name, &info);
	if(info.command & FORCE_FULL) {
	    nb_forced_level_0 += 1;
	    preserve = dp;
	    continue;
	}

	if(dp != preserve) {

            /* Format dumpsize for messages */
	    ap_snprintf(est_kb, 20, "%ld KB,", est(dp)->dump_size);

	    if(est(dp)->last_level == -1 || dp->skip_incr) {
		delay_one_dump(dp, 1,
			       "dumps too big,",
			       est_kb,
			       "but cannot incremental dump",
			       dp->skip_incr ? "skip-incr": "new",
			       "disk",
			       NULL);
	    }
	    else {
		delay_one_dump(dp, 0,
			       "dumps too big,",
			       est_kb,
			       "full dump delayed",
			       NULL);
	    }
	}
    }

    /* 2.b. Delay forced full if needed */
    if(nb_forced_level_0 > 0 && total_size > tape_length) {
	for(dp = schedq.tail;
		dp != NULL && total_size > tape_length;
		dp = ndp) {
	    ndp = dp->prev;

	    if(est(dp)->dump_level == 0 && dp != preserve) {

		/* Format dumpsize for messages */
		ap_snprintf(est_kb, 20, "%ld KB,", est(dp)->dump_size);

		if(est(dp)->last_level == -1 || dp->skip_incr) {
		    delay_one_dump(dp, 1,
				   "dumps too big,",
				   est_kb,
				   "but cannot incremental dump",
				   dp->skip_incr ? "skip-incr": "new",
				   "disk",
				   NULL);
		}
		else {
		    delay_one_dump(dp, 0,
				   "dumps too big,",
				   est_kb,
				   "full dump delayed",
				   NULL);
		}
	    }
	}
    }

    /*
    ** 3. Delay incremental dumps.
    **
    ** Delay incremental dumps until tomorrow.  This is a last ditch attempt
    ** at making things fit.  Again, we start with the lowest priority (most
    ** dispensable) and work forwards.
    */

    for(dp = schedq.tail;
	    dp != NULL && total_size > tape_length;
	    dp = ndp) {
	ndp = dp->prev;

	if(est(dp)->dump_level != 0) {

            /* Format dumpsize for messages */
	    ap_snprintf(est_kb, 20, "%ld KB,", est(dp)->dump_size);

	    delay_one_dump(dp, 1,
			   "dumps way too big,",
			   est_kb,
			   "must skip incremental dumps",
			   NULL);
	}
    }

    /*
    ** 4. Reinstate delayed dumps.
    **
    ** We might not have needed to stomp on all of the dumps we have just
    ** delayed above.  Try to reinstate them all starting with the last one
    ** and working forwards.  It is unlikely that the last one will fit back
    ** in but why complicate the code?
    */

    for(bi = biq.tail; bi != NULL; bi = nbi) {
	nbi = bi->prev;
	dp = bi->dp;

	if(bi->deleted)
	    new_total = total_size + tt_blocksize_kb + bi->size + tape_mark;
	else
	    new_total = total_size - est(dp)->dump_size + bi->size;

	if(new_total <= tape_length && bi->size < tape->length) {
	    /* reinstate it */
	    total_size = new_total;
	    if(bi->deleted) {
		if(bi->level == 0) {
		    total_lev0 += (double) bi->size;
		}
		insert_disk(&schedq, dp, schedule_order);
	    }
	    else {
		est(dp)->dump_level = bi->level;
		est(dp)->dump_size = bi->size;
	    }

	    /* Keep it clean */
	    if(bi->next == NULL)
		biq.tail = bi->prev;
	    else
		(bi->next)->prev = bi->prev;
	    if(bi->prev == NULL)
		biq.head = bi->next;
	    else
		(bi->prev)->next = bi->next;
	    amfree(bi->errstr);
	    amfree(bi);
	}
    }

    /*
    ** 5. Output messages about what we have done.
    **
    ** We can't output messages while we are delaying dumps because we might
    ** reinstate them later.  We remember all the messages and output them
    ** now.
    */

    for(bi = biq.head; bi != NULL; bi = nbi) {
	nbi = bi->next;

	if(bi->deleted) {
	    fprintf(stderr, "%s: FAILED %s\n", get_pname(), bi->errstr);
	    log_add(L_FAIL, "%s", bi->errstr);
	}
	else {
	    dp = bi->dp;
	    fprintf(stderr, "  delay: %s now at level %d\n",
		bi->errstr, est(dp)->dump_level);
	    log_add(L_INFO, "%s", bi->errstr);
	}

	/* Clean up - dont be too fancy! */
	amfree(bi->errstr);
	amfree(bi);
    }

    fprintf(stderr, "  delay: Total size now %ld.\n", total_size);

    return;
}


/*
 * Remove a dump or modify it from full to incremental.
 * Keep track of it on the bi q in case we can add it back later.
 */
arglist_function1(static void delay_one_dump,
		  disk_t *, dp,
		  int, delete)
{
    bi_t *bi;
    va_list argp;
    char level_str[NUM_STR_SIZE];
    char *sep;
    char *next;

    arglist_start(argp, delete);

    total_size -= tt_blocksize_kb + est(dp)->dump_size + tape_mark;
    if(est(dp)->dump_level == 0) {
	total_lev0 -= (double) est(dp)->dump_size;
    }

    bi = alloc(sizeof(bi_t));
    bi->next = NULL;
    bi->prev = biq.tail;
    if(biq.tail == NULL)
	biq.head = bi;
    else
	biq.tail->next = bi;
    biq.tail = bi;

    bi->deleted = delete;
    bi->dp = dp;
    bi->level = est(dp)->dump_level;
    bi->size = est(dp)->dump_size;

    ap_snprintf(level_str, sizeof(level_str), "%d", est(dp)->dump_level);
    bi->errstr = vstralloc(dp->host->hostname,
			   " ", dp->name,
			   " ", datestamp ? datestamp : "?",
			   " ", level_str,
			   NULL);
    sep = " [";
    while ((next = arglist_val(argp, char *)) != NULL) {
	bi->errstr = newvstralloc(bi->errstr, bi->errstr, sep, next, NULL);
	sep = " ";
    }
    strappend(bi->errstr, "]");
    arglist_end(argp);

    if (delete) {
	remove_disk(&schedq, dp);
    } else {
	est(dp)->dump_level = est(dp)->degr_level;
	est(dp)->dump_size = est(dp)->degr_size;
	total_size += tt_blocksize_kb + est(dp)->dump_size + tape_mark;
    }

    return;
}


static int promote_highest_priority_incremental P((void))
{
    disk_t *dp, *dp1, *dp_promote;
    long new_size, new_total, new_lev0;
    int check_days;
    int nb_today, nb_same_day, nb_today2;
    int nb_disk_today, nb_disk_same_day;

    /*
     * return 1 if did so; must update total_size correctly; must not
     * cause total_size to exceed tape_length
     */

    dp_promote = NULL;
    for(dp = schedq.head; dp != NULL; dp = dp->next) {

	est(dp)->promote = -1000;

	if(est_size(dp,0) <= 0)
	    continue;

	if(est(dp)->next_level0 <= 0)
	    continue;

	if(est(dp)->next_level0 > dp->maxpromoteday)
	    continue;

	new_size = est_tape_size(dp, 0);
	new_total = total_size - est(dp)->dump_size + new_size;
	new_lev0 = total_lev0 + new_size;

	nb_today = 0;
	nb_same_day = 0;
	nb_disk_today = 0;
	nb_disk_same_day = 0;
        for(dp1 = schedq.head; dp1 != NULL; dp1 = dp1->next) {
	    if(est(dp1)->dump_level == 0)
		nb_disk_today++;
	    else if(est(dp1)->next_level0 == est(dp)->next_level0)
		nb_disk_same_day++;
	    if(strcmp(dp->host->hostname, dp1->host->hostname) == 0) {
		if(est(dp1)->dump_level == 0)
		    nb_today++;
		else if(est(dp1)->next_level0 == est(dp)->next_level0)
		    nb_same_day++;
	    }
	}

	/* do not promote if overflow tape */
	if(new_total > tape_length) continue;

	/* do not promote if overflow balanced size and something today */
	/* promote if nothing today */
	if(new_lev0 > balanced_size+balance_threshold && nb_disk_today > 0)
	    continue;

	/* do not promote if only one disk due that day and nothing today */
	if(nb_disk_same_day == 1 && nb_disk_today == 0) continue;

	nb_today2 = nb_today*nb_today;
	if(nb_today == 0 && nb_same_day > 1) nb_same_day++;

	if(nb_same_day >= nb_today2) {
	    est(dp)->promote = ((nb_same_day - nb_today2)*(nb_same_day - nb_today2)) + 
			       conf_dumpcycle - est(dp)->next_level0;
	}
	else {
	    est(dp)->promote = -nb_today2 +
			       conf_dumpcycle - est(dp)->next_level0;
	}

        if(!dp_promote || est(dp_promote)->promote < est(dp)->promote) {
	    dp_promote = dp;
	    fprintf(stderr,"   try %s:%s %d %d %d = %d\n",
		    dp->host->hostname, dp->name, nb_same_day, nb_today, est(dp)->next_level0, est(dp)->promote);
	}
        else {
	    fprintf(stderr,"no try %s:%s %d %d %d = %d\n",
		    dp->host->hostname, dp->name, nb_same_day, nb_today, est(dp)->next_level0, est(dp)->promote);
	}
    }

    if(dp_promote) {
	dp = dp_promote;

	new_size = est_tape_size(dp, 0);
	new_total = total_size - est(dp)->dump_size + new_size;
	new_lev0 = total_lev0 + new_size;

	total_size = new_total;
	total_lev0 = new_lev0;
	check_days = est(dp)->next_level0;
	est(dp)->degr_level = est(dp)->dump_level;
	est(dp)->degr_size = est(dp)->dump_size;
	est(dp)->dump_level = 0;
	est(dp)->dump_size = new_size;
	est(dp)->next_level0 = 0;

	fprintf(stderr,
	      "   promote: moving %s:%s up, total_lev0 %1.0f, total_size %ld\n",
		dp->host->hostname, dp->name,
		total_lev0, total_size);

	log_add(L_INFO,
		"Full dump of %s:%s promoted from %d day%s ahead.",
		dp->host->hostname, dp->name,
		check_days, (check_days == 1) ? "" : "s");
	return 1;
    }
    return 0;
}


static int promote_hills P((void))
{
    disk_t *dp;
    struct balance_stats {
	int disks;
	long size;
    } *sp = NULL;
    int days;
    int hill_days = 0;
    long hill_size;
    long new_size;
    long new_total;
    int my_dumpcycle;

    /* If we are already doing a level 0 don't bother */
    if(total_lev0 > 0)
	return 0;

    /* Do the guts of an "amadmin balance" */
    my_dumpcycle = conf_dumpcycle;
    if(my_dumpcycle > 10000) my_dumpcycle = 10000;

    sp = (struct balance_stats *)
	alloc(sizeof(struct balance_stats) * my_dumpcycle);

    for(days = 0; days < my_dumpcycle; days++)
	sp[days].disks = sp[days].size = 0;

    for(dp = schedq.head; dp != NULL; dp = dp->next) {
	days = est(dp)->next_level0;   /* This is > 0 by definition */
	if(days<my_dumpcycle && !dp->skip_full && dp->strategy != DS_NOFULL &&
	   dp->strategy != DS_INCRONLY) {
	    sp[days].disks++;
	    sp[days].size += est(dp)->last_lev0size;
	}
    }

    /* Search for a suitable big hill and cut it down */
    while(1) {
	/* Find the tallest hill */
	hill_size = 0;
	for(days = 0; days < my_dumpcycle; days++) {
	    if(sp[days].disks > 1 && sp[days].size > hill_size) {
		hill_size = sp[days].size;
		hill_days = days;
	    }
	}

	if(hill_size <= 0) break;	/* no suitable hills */

	/* Find all the dumps in that hill and try and remove one */
	for(dp = schedq.head; dp != NULL; dp = dp->next) {
	    if(est(dp)->next_level0 != hill_days ||
	       est(dp)->next_level0 > dp->maxpromoteday ||
	       dp->skip_full ||
	       dp->strategy == DS_NOFULL ||
	       dp->strategy == DS_INCRONLY)
		continue;
	    new_size = est_tape_size(dp, 0);
	    new_total = total_size - est(dp)->dump_size + new_size;
	    if(new_total > tape_length)
		continue;
	    /* We found a disk we can promote */
	    total_size = new_total;
	    total_lev0 += new_size;
	    est(dp)->degr_level = est(dp)->dump_level;
	    est(dp)->degr_size = est(dp)->dump_size;
	    est(dp)->dump_level = 0;
	    est(dp)->next_level0 = 0;
	    est(dp)->dump_size = new_size;

	    fprintf(stderr,
		    "   promote: moving %s:%s up, total_lev0 %1.0f, total_size %ld\n",
		    dp->host->hostname, dp->name,
		    total_lev0, total_size);

	    log_add(L_INFO,
		    "Full dump of %s:%s specially promoted from %d day%s ahead.",
		    dp->host->hostname, dp->name,
		    hill_days, (hill_days == 1) ? "" : "s");

	    amfree(sp);
	    return 1;
	}
	/* All the disks in that hill were unsuitable. */
	sp[hill_days].disks = 0;	/* Don't get tricked again */
    }

    amfree(sp);
    return 0;
}

/*
 * ========================================================================
 * OUTPUT SCHEDULE
 *
 * XXX - memory leak - we shouldn't just throw away *dp
 */
static void output_scheduleline(dp)
    disk_t *dp;
{
    est_t *ep;
    long dump_time = 0, degr_time = 0;
    char *schedline = NULL, *degr_str = NULL;
    char dump_priority_str[NUM_STR_SIZE];
    char dump_level_str[NUM_STR_SIZE];
    char dump_size_str[NUM_STR_SIZE];
    char dump_time_str[NUM_STR_SIZE];
    char degr_level_str[NUM_STR_SIZE];
    char degr_size_str[NUM_STR_SIZE];
    char degr_time_str[NUM_STR_SIZE];
    char *dump_date, *degr_date;
    char *features;
    int i;

    ep = est(dp);

    if(ep->dump_size == -1) {
	/* no estimate, fail the disk */
	fprintf(stderr,
		"%s: FAILED %s %s %s %d [no estimate]\n",
		get_pname(),
		dp->host->hostname, dp->name, datestamp, ep->dump_level);
	log_add(L_FAIL, "%s %s %s %d [no estimate]",
	        dp->host->hostname, dp->name, datestamp, ep->dump_level);
	return;
    }

    dump_date = degr_date = (char *)0;
    for(i = 0; i < MAX_LEVELS; i++) {
	if(ep->dump_level == ep->level[i])
	    dump_date = ep->dumpdate[i];
	if(ep->degr_level == ep->level[i])
	    degr_date = ep->dumpdate[i];
    }

#define fix_rate(rate) (rate < 1.0 ? DEFAULT_DUMPRATE : rate)

    if(ep->dump_level == 0) {
	dump_time = ep->dump_size / fix_rate(ep->fullrate);

	if(ep->degr_size != -1) {
	    degr_time = ep->degr_size / fix_rate(ep->incrrate);
	}
    }
    else {
	dump_time = ep->dump_size / fix_rate(ep->incrrate);
    }

    if(ep->dump_level == 0 && ep->degr_size != -1) {
	ap_snprintf(degr_level_str, sizeof(degr_level_str),
		    "%d", ep->degr_level);
	ap_snprintf(degr_size_str, sizeof(degr_size_str),
		    "%ld", ep->degr_size);
	ap_snprintf(degr_time_str, sizeof(degr_time_str),
		    "%ld", degr_time);
	degr_str = vstralloc(" ", degr_level_str,
			     " ", degr_date,
			     " ", degr_size_str,
			     " ", degr_time_str,
			     NULL);
    }
    ap_snprintf(dump_priority_str, sizeof(dump_priority_str),
		"%d", ep->dump_priority);
    ap_snprintf(dump_level_str, sizeof(dump_level_str),
		"%d", ep->dump_level);
    ap_snprintf(dump_size_str, sizeof(dump_size_str),
		"%ld", ep->dump_size);
    ap_snprintf(dump_time_str, sizeof(dump_time_str),
		"%ld", dump_time);
    features = am_feature_to_string(dp->host->features);
    schedline = vstralloc("DUMP ",dp->host->hostname,
			  " ", features,
			  " ", dp->name,
			  " ", datestamp,
			  " ", dump_priority_str,
			  " ", dump_level_str,
			  " ", dump_date,
			  " ", dump_size_str,
			  " ", dump_time_str,
			  degr_str ? degr_str : "",
			  "\n", NULL);

    fputs(schedline, stdout);
    fputs(schedline, stderr);
    amfree(features);
    amfree(schedline);
    amfree(degr_str);
}
