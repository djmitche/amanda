/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991,1994 University of Maryland
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
 * $Id: planner.c,v 1.47 1997/12/30 05:25:22 jrj Exp $
 *
 * backup schedule planner for the Amanda backup system.
 */
#include "amanda.h"
#include "conffile.h"
#include "diskfile.h"
#include "tapefile.h"
#include "infofile.h"
#include "logfile.h"
#include "clock.h"
#include "dgram.h"
#include "protocol.h"
#include "version.h"

char *pname = "planner";

#define MAX_LEVELS		    3	/* max# of estimates per filesys */
#define ONE_TIMEOUT		  240	/* # seconds to wait for one est */

#define RUNS_REDZONE		    5	/* should be in conf file? */

#define PROMOTE_THRESHOLD	 0.05	/* if <5% unbalanced, don't promote */
#define DEFAULT_DUMPRATE	 30.0	/* K/s */

/* configuration file stuff */
char *conf_diskfile;
char *conf_tapelist;
char *conf_infofile;
char *conf_tapetype;
int conf_runtapes;
int conf_dumpcycle;
int conf_tapecycle;
int conf_bumpdays;
int conf_bumpsize;
double conf_bumpmult;

typedef struct est_s {
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
    double fullrate, incrrate;
    double fullcomp, incrcomp;
    char *errstr;
    int level[MAX_LEVELS];
    char *dumpdate[MAX_LEVELS];
    long est_size[MAX_LEVELS];
} est_t;

#define est(dp)	((est_t *)(dp)->up)

disklist_t startq, waitq, estq, failq, schedq;
long total_size, initial_size;
double total_lev0, balanced_size, balance_threshold;
unsigned long tape_length, tape_mark;
int result_port, result_socket, amanda_port;
int total_waiting, max_disks;
char *loginid = NULL;

#ifdef KRB4_SECURITY
int kamanda_port;
#endif

tapetype_t *tape;
int runs_per_cycle;
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

/*
 * ========================================================================
 * MAIN PROGRAM
 *
 */

static char *construct_datestamp P((void));  /* subroutines */
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
    int moved_one;
    char *datestamp = NULL, **vp;
    struct passwd *pwptr;

    erroutput_type = (ERR_AMANDALOG|ERR_INTERACTIVE);
    startclock();

    fprintf(stderr, "%s: pid %ld executable %s version %s\n",
	    pname, (long) getpid(), argv[0], version());
    for(vp = version_info; *vp != NULL; vp++)
	fprintf(stderr, "%s: %s", pname, *vp);

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

    if(dgram_bind(msg, &result_port) == -1)
	error("could not bind result datagram port: %s", strerror(errno));

    if(geteuid() == 0) {
	/* set both real and effective uid's to real uid, likewise for gid */
	setgid(getgid());
	setuid(getuid());
    }

    /*
     * From this point on we are running under our real uid, so we don't
     * have to worry about opening security holes below.  Find out who we
     * are running as.
     */

    if((pwptr = getpwuid(getuid())) == NULL)
	error("can't get login name for my uid %ld", (long)getuid());
    loginid = newstralloc(loginid, pwptr->pw_name);

    /*
     * 2. Read in Configuration Information
     *
     * All the Amanda configuration files are loaded before we begin.
     */

    fprintf(stderr,"READING CONF FILES...\n");

    if(read_conffile(CONFFILE_NAME))
	error("could not find \"%s\" in this directory.\n", CONFFILE_NAME);

    conf_diskfile = getconf_str(CNF_DISKFILE);
    conf_tapelist = getconf_str(CNF_TAPELIST);
    conf_infofile = getconf_str(CNF_INFOFILE);
    conf_tapetype = getconf_str(CNF_TAPETYPE);
    conf_runtapes = getconf_int(CNF_RUNTAPES);
    conf_dumpcycle = getconf_int(CNF_DUMPCYCLE);
    conf_tapecycle = getconf_int(CNF_TAPECYCLE);
    conf_bumpdays = getconf_int(CNF_BUMPDAYS);
    conf_bumpsize = getconf_int(CNF_BUMPSIZE);
    conf_bumpmult = getconf_real(CNF_BUMPMULT);

    afree(datestamp);
    datestamp = construct_datestamp();
    log(L_START, "date %s", datestamp);

    if((origqp = read_diskfile(conf_diskfile)) == NULL)
	error("could not load \"%s\"\n", conf_diskfile);

    if(read_tapelist(conf_tapelist))
	error("could not load \"%s\"\n", conf_tapelist);

    if(open_infofile(conf_infofile))
	error("could not open info db \"%s\"\n", conf_infofile);


    /* some initializations */

    runs_per_cycle = guess_runs_from_tapelist();

    tape = lookup_tapetype(conf_tapetype);
    tape_length = tape->length * conf_runtapes;
    tape_mark   = tape->filemark;

    proto_init(msg->socket, today, 1000); /* XXX handles should eq nhosts */

#ifdef KRB4_SECURITY
    kerberos_service_init();
#endif

    fprintf(stderr, "startup took %s secs\n", walltime_str(curclock()));

    /*
     * 3. Calculate Preliminary Dump Levels
     *
     * Before we can get estimates from the remote slave hosts, we make a
     * first attempt at guessing what dump levels we will be dumping at
     * based on the curinfo database.
     */

    fprintf(stderr,"\nSETTING UP FOR ESTIMATES...\n");
    startclock();

    startq.head = startq.tail = NULL;
    while(!empty(*origqp)) setup_estimate(dequeue_disk(origqp));

    fprintf(stderr, "setting up estimates took %s secs\n",
	    walltime_str(curclock()));


    /*
     * 4. Get Dump Size Estimates from Remote Client Hosts
     *
     * Each host is queried (in parallel) for dump size information on all
     * of its disks, and the results gathered as they come in.
     */

    /* go out and get the dump estimates */

    fprintf(stderr,"\nGETTING ESTIMATES...\n");
    startclock();

    estq.head = estq.tail = NULL;
    failq.head = failq.tail = NULL;

    get_estimates();

    fprintf(stderr, "getting estimates took %s secs\n",
	    walltime_str(curclock()));

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

    startclock();

			/* an empty tape still has a label and an endmark */
    total_size = (TAPE_BLOCK_SIZE + tape_mark) * 2;
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

    fprintf(stderr, "analysis took %s secs\n", walltime_str(curclock()));


    /*
     * 8. Output Schedule
     *
     * The schedule goes to stdout, presumably to driver.  A copy is written
     * on stderr for the debug file.
     */

    fprintf(stderr,"\nGENERATING SCHEDULE:\n--------\n");
    while(!empty(schedq)) output_scheduleline(dequeue_disk(&schedq));
    fprintf(stderr, "--------\n");

    close_infofile();
    log(L_FINISH, "date %s", datestamp);

    return 0;
}

static char *construct_datestamp()
{
    struct tm *tm;
    char datestamp[3*NUM_STR_SIZE];

    today = time((time_t *)NULL);
    tm = localtime(&today);
    ap_snprintf(datestamp, sizeof(datestamp),
		"%04d%02d%02d", tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday);
    return stralloc(datestamp);
}



/*
 * ========================================================================
 * SETUP FOR ESTIMATES
 *
 */

static int last_level P((info_t *ip));		  /* subroutines */
static long est_size P((disk_t *dp, int level));
static long est_tape_size P((disk_t *dp, int level));
static int next_level0 P((disk_t *dp, info_t *ip));
static int runs_at P((info_t *ip, int lev));
static long bump_thresh P((int level));
static int when_overwrite P((char *label));

static void askfor(ep, seq, lev, inf)
est_t *ep;	/* esimate data block */
int seq;	/* sequence number of request */
int lev;	/* dump level being requested */
info_t *inf;	/* info block for disk */
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

    ep->dumpdate[seq] = stralloc(get_dumpdate(inf,lev));

    stat = &inf->inf[lev];
    if(stat->date == EPOCH) ep->est_size[seq] = -1;
    else ep->est_size[seq] = stat->size;

    return;
}

static void setup_estimate(dp)
disk_t *dp;
{
    est_t *ep;
    info_t inf;
    int i;

    assert(dp && dp->host);
    fprintf(stderr, "setting up estimates for %s:%s\n", dp->host->hostname, dp->name);

    /* get current information about disk */

    if(get_info(dp->host->hostname, dp->name, &inf)) {
	/* no record for this disk, make a note of it */
	log(L_INFO, "Adding new disk %s:%s.", dp->host->hostname, dp->name);
    }

    /* setup working data struct for disk */

    ep = alloc(sizeof(est_t));
    dp->up = (void *) ep;
    ep->dump_size = -1;
    ep->dump_priority = dp->priority;
    ep->errstr = 0;

    /* calculated fields */

    if(inf.command == PLANNER_FORCE) {
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
	    log(L_ERROR,
		"Cannot force full dump of %s:%s with no-full option.",
		dp->host->hostname, dp->name);

	    /* clear force command */
	    if(inf.command == PLANNER_FORCE)
		inf.command = NO_COMMAND;
	    if(put_info(dp->host->hostname, dp->name, &inf))
		error("could not put info record for %s:%s: %s",
		      dp->host->hostname, dp->name, strerror(errno));
	    ep->last_level = last_level(&inf);
	    ep->next_level0 = next_level0(dp, &inf);
	}
	else {
	    ep->last_level = -1;
	    ep->next_level0 = -conf_dumpcycle;
	    log(L_INFO, "Forcing full dump of %s:%s as directed.",
		dp->host->hostname, dp->name);
	}
    }
    else {
	ep->last_level = last_level(&inf);
	ep->next_level0 = next_level0(dp, &inf);
    }

    /* adjust priority levels */

    if(ep->next_level0 < 0) {
	fprintf(stderr,"%s:%s overdue %d days for level 0\n",
		dp->host->hostname, dp->name, - ep->next_level0);
	ep->dump_priority -= ep->next_level0;
	/* warn if dump will be overwritten */
	if(ep->last_level > -1) {
	    int overwrite_runs = when_overwrite(inf.inf[0].label);
	    if(overwrite_runs == 0) {
		log(L_WARNING,
		 "Last full dump of %s:%s on tape %s overwritten on this run.",
		    dp->host->hostname, dp->name, inf.inf[0].label);
	    }
	    else if(overwrite_runs < RUNS_REDZONE) {
		log(L_WARNING,
		 "Last full dump of %s:%s on tape %s overwritten in %d run%s.",
		    dp->host->hostname, dp->name, inf.inf[0].label,
		    overwrite_runs, overwrite_runs == 1? "" : "s");
	    }
	}
    }
    else if(inf.command == PLANNER_FORCE)
	ep->dump_priority += 1;
    /* else XXX bump up the priority of incrementals that failed last night */

    /* handle external level 0 dumps */

    if(dp->skip_full) {
	if(ep->next_level0 <= 0) {
	    /* update the date field */
	    if(inf.inf[0].date == EPOCH || inf.command == PLANNER_FORCE)
		inf.inf[0].date = today;
	    else
		inf.inf[0].date += conf_dumpcycle * SECS_PER_DAY;
	    if(inf.command == PLANNER_FORCE)
		inf.command = NO_COMMAND;
	    if(put_info(dp->host->hostname, dp->name, &inf))
		error("could not put info record for %s:%s: %s",
		      dp->host->hostname, dp->name, strerror(errno));
	    ep->next_level0 += conf_dumpcycle;
	    ep->last_level = 0;
	}

	if(days_diff(inf.inf[0].date, today) == 0) {
	    log(L_INFO, "Skipping full dump of %s:%s today.",
		dp->host->hostname, dp->name);
	    fprintf(stderr,"%s:%s lev 0 skipped due to skip-full flag\n",
		    dp->host->hostname, dp->name);
	    /* don't enqueue the disk */
	    askfor(ep, 0, -1, &inf);
	    askfor(ep, 1, -1, &inf);
	    askfor(ep, 2, -1, &inf);
	    fprintf(stderr, "planner: SKIPPED %s %s 0 [skip-full]\n",
		    dp->host->hostname, dp->name);
	    log(L_SUCCESS, "%s %s 0 [skipped: skip-full]",
		dp->host->hostname, dp->name);
	    return;
	}

	if(ep->next_level0 == 1) {
	    log(L_WARNING, "Skipping full dump of %s:%s tommorrow.",
		dp->host->hostname, dp->name);
	}

    }

    /* handle "skip-incr" type archives */

    if(dp->skip_incr && ep->next_level0 > 0) {
	fprintf(stderr,"%s:%s lev 1 skipped due to skip-incr flag\n",
		dp->host->hostname, dp->name);
	/* don't enqueue the disk */
	askfor(ep, 0, -1, &inf);
	askfor(ep, 1, -1, &inf);
	askfor(ep, 2, -1, &inf);

	fprintf(stderr, "planner: SKIPPED %s %s 1 [skip-incr]\n",
		dp->host->hostname, dp->name);

	log(L_SUCCESS, "%s %s 1 [skipped: skip-incr]",
	    dp->host->hostname, dp->name);
	return;
    }

    if(ep->last_level == -1 && ep->next_level0 > 0 && dp->strategy != DS_NOFULL) {
	log(L_WARNING,
	    "%s:%s mismatch: no tapelist record, but curinfo next_level0: %d.",
	    dp->host->hostname, dp->name, ep->next_level0);
	ep->next_level0 = 0;
    }

    if(ep->last_level == 0) ep->level_days = 0;
    else ep->level_days = runs_at(&inf, ep->last_level);
    ep->last_lev0size = inf.inf[0].csize;

    ep->fullrate = perf_average(inf.full.rate, 0.0);
    ep->incrrate = perf_average(inf.incr.rate, 0.0);

    ep->fullcomp = perf_average(inf.full.comp, dp->comprate[0]);
    ep->incrcomp = perf_average(inf.incr.comp, dp->comprate[1]);

    /* determine which estimates to get */

    i = 0;

    if(!(dp->skip_full || dp->strategy == DS_NOFULL))
	askfor(ep, i++, 0, &inf);

    if(!dp->skip_incr) {
	if(ep->last_level == -1) {		/* a new disk */
	    if(dp->strategy == DS_NOFULL) {
		askfor(ep, i++, 1, &inf);
	    } else {
		assert(!dp->skip_full);		/* should be handled above */
	    }
	} else {				/* not new, pick normally */
	    int curr_level;

	    curr_level = ep->last_level;
	    if(curr_level == 0)
		askfor(ep, i++, 1, &inf);
	    else {
		askfor(ep, i++, curr_level, &inf);
		/*
		 * If last time we dumped less than the threshold, then this
		 * time we will too, OR the extra size will be charged to both
		 * cur_level and cur_level + 1, so we will never bump.  Also,
		 * if we haven't been at this level 2 days, or the dump failed
		 * last night, we can't bump.
		 */
		if((inf.inf[curr_level].size == 0 || /* no data, try it anyway */
		   ((inf.inf[curr_level].size > bump_thresh(curr_level))) &&
		   ep->level_days >= conf_bumpdays)) {
		    askfor(ep, i++, curr_level+1, &inf);
		}
	    } 
	}
    }

    while(i < MAX_LEVELS) 	/* mark end of estimates */
	askfor(ep, i++, -1, &inf);

    /* debug output */

    fprintf(stderr, "setup_estimate: %s:%s: command %d, options:",
	    dp->host->hostname, dp->name, inf.command);
    if(dp->strategy == DS_NOFULL) fputs(" no-full", stderr);
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

    return (long)(size * ratio);
}


/* what was the level of the last successful dump to tape? */
static int last_level(ip)
info_t *ip;
{
    int min_pos, min_level, i;
    time_t lev0_date;
    tape_t *tp;

    min_pos = 1000000000;
    min_level = -1;
    lev0_date = EPOCH;
    for(i = 0; i < 9; i++) {
	if((tp = lookup_tapelabel(ip->inf[i].label)) == NULL) continue;
	/* cull any entries from previous cycles */
	if(i == 0) lev0_date = ip->inf[0].date;
	else if(ip->inf[i].date < lev0_date) continue;

	if(tp->position < min_pos) {
	    min_pos = tp->position;
	    min_level = i;
	}
    }
    return min_level;
}

/* when is next level 0 due? 0 = today, 1 = tommorrow, etc*/
static int next_level0(dp, ip)
disk_t *dp;
info_t *ip;
{
    if(dp->strategy == DS_NOFULL)
	return 1;		/* fake it */
    else if(ip->inf[0].date < (time_t)0)
	return -days_diff(EPOCH, today);	/* new disk */
    else
	return dp->dumpcycle - days_diff(ip->inf[0].date, today);
}

/* how many runs at current level? */
static int runs_at(ip, lev)
info_t *ip;
int lev;
{
    tape_t *cur_tape, *old_tape;
    int last;

    last = last_level(ip);
    if(lev != last) return 0;
    if(lev == 0) return 1;

    cur_tape = lookup_tapelabel(ip->inf[lev].label);
    old_tape = lookup_tapelabel(ip->inf[lev-1].label);
    if(cur_tape == NULL || old_tape == NULL) return 0;

    return (old_tape->position - cur_tape->position) / conf_runtapes;
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
    struct servent *amandad;

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

    while(!empty(startq)) {
	getsize(startq.head->host);
	check_protocol();
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
    char *req, *errstr;
    int i, disks, rc;
    char number[NUM_STR_SIZE];

    assert(hostp->disks != NULL);

    ap_snprintf(number, sizeof(number), "%d", hostp->maxdumps);
    req = vstralloc("SERVICE sendsize\n",
		    "OPTIONS "
		    "maxdumps=", number, ";",
		    "hostname=", hostp->hostname, ";",
		    "\n",
		    NULL);
    disks = 0;
    for(dp = hostp->disks; dp != NULL; dp = dp->hostnext) {
	est(dp)->got_estimate = 0;
	if(est(dp)->level[0] == -1) continue;	/* ignore this disk */

	remove_disk(&startq, dp);

	for(i = 0; i < MAX_LEVELS; i++) {
	    char *t;
	    char *exclude1 = "";
	    char *exclude2 = "";
	    char platter[NUM_STR_SIZE];
	    char level[NUM_STR_SIZE];
	    int lev = est(dp)->level[i];

	    if(lev == -1) break;

	    ap_snprintf(level, sizeof(level), "%d", lev);
	    ap_snprintf(platter, sizeof(platter), "%d", dp->platter);
	    if(dp->exclude) {
		exclude1 = dp->exclude_list ? " exclude-list" : " exclude-file";
		exclude2 = dp->exclude;
	    }
	    t = vstralloc(req,
			  dp->program, " ", dp->name, " ", level, " ",
			  est(dp)->dumpdate[i], " ", platter,
			  exclude1,
			  exclude2,
			  "\n",
			  NULL);
	    afree(req);
	    req = t;
	    disks++;
	}
    }
    if(disks > max_disks) max_disks = disks;

#ifdef KRB4_SECURITY
    if(hostp->disks->auth == AUTH_KRB4)
	rc = make_krb_request(hostp->hostname, kamanda_port, req,
			      hostp, disks*ONE_TIMEOUT, handle_result);
    else
#endif
	rc = make_request(hostp->hostname, amanda_port, req,
			  hostp, disks*ONE_TIMEOUT, handle_result);

    req = NULL;					/* do not own this any more */

    if(rc) {
	errstr = vstralloc("could not resolve hostname \"",
			   hostp->hostname,
			   "\"",
			   ": ", strerror(errno),
			   NULL);
	destqp = &failq;
    }
    else {
	errstr = NULL;
	destqp = &waitq;
    }

    for(dp = hostp->disks; dp != NULL; dp = dp->hostnext) {
	est(dp)->errstr = errstr;
	errstr = NULL;
	enqueue_disk(destqp, dp);
    }
    afree(errstr);
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
    int rc, level, i;
    long size;
    disk_t *dp;
    host_t *hostp;
    char *resp;
    char *msgdisk, *msgdisk_undo, msgdisk_undo_ch;
    char *remoterr, *errbuf = NULL;
    char *s, *fp;
    int ch;

    hostp = (host_t *) p->datap;

    if(p->state == S_FAILED) {
	if(pkt == NULL) {
	    errbuf = vstralloc("Request to ", hostp->hostname, " timed out.",
			       NULL);
#define sc "ERROR"
	} else if(strncmp(pkt->body, sc, sizeof(sc)-1) == 0) {
	    s = pkt->body + sizeof(sc)-1;
	    ch = *s++;
#undef sc
	    skip_whitespace(s, ch);
	    if(ch == '\0') goto NAK_parse_failed;
	    remoterr = s - 1;
	    if((s = strchr(remoterr, '\n')) != NULL) {
		if(s == remoterr) goto NAK_parse_failed;
		*s = '\0';
	    }
	    errbuf = vstralloc(hostp->hostname, " NAK: ", remoterr, NULL);
	    if(s) *s = '\n';
	} else {
	    goto NAK_parse_failed;
	}
	goto error_return;
    }

#ifdef KRB4_SECURITY
    if(hostp->disks->auth == AUTH_KRB4 &&
       !check_mutual_authenticator(host2key(hostp->hostname), pkt, p)) {
	errbuf = vstralloc(hostp->hostname,
			   "[mutual-authentication failed]",
			   NULL);
	goto error_return;
    }
#endif

    s = pkt->body;
    ch = *s++;

#define sc "ERROR"
    if(strncmp(s - 1, sc, sizeof(sc)-1) == 0) {
	/* this is an error response packet */
	s += sizeof(sc)-1;
	ch = s[-1];
#undef sc

	skip_whitespace(s, ch);
	if(ch == '\0') goto bogus_error_packet;
	remoterr = s - 1;
	skip_line(s, ch);
	s[-1] = '\0';
	errbuf = vstralloc(hostp->hostname, ": ", remoterr, NULL);
	goto error_return;
    }

#define sc "OPTIONS"
    if(strncmp(s - 1, sc, sizeof(sc)-1) == 0) {
#undef sc
	/* got options back from other side, ignore them */
	skip_line(s, ch);
    }

    msgdisk_undo = NULL;
    while(ch) {
	resp = s - 1;
	skip_whitespace(s, ch);
	if (ch == '\0') goto bad_msg;
	msgdisk = s - 1;
	skip_non_whitespace(s, ch);
	msgdisk_undo = s - 1;
	msgdisk_undo_ch = *msgdisk_undo;
	s[-1] = '\0';

	skip_whitespace(s, ch);
	if (ch == '\0' || sscanf(s - 1, "%d SIZE %ld", &level, &size) != 2) {
	    goto bad_msg;
	}
	skip_line(s, ch);

	dp = lookup_hostdisk(hostp, msgdisk);

	*msgdisk_undo = msgdisk_undo_ch;	/* for error message */
	msgdisk_undo = NULL;

	if(dp == NULL) {
	    char tmp;

	    tmp = s[-1];
	    s[-1] = '\0';			/* for error message */
	    log(L_ERROR, "%s: invalid reply from sendsize: `%s'\n",
		hostp->hostname, resp);
	    s[-1] = tmp;
	} else {
	    for(i = 0; i < MAX_LEVELS; i++)
		if(est(dp)->level[i] == level) {
		    est(dp)->est_size[i] = size;
		    break;
		}
	    if(i == MAX_LEVELS) goto bad_msg;   /* this est wasn't requested */
	    est(dp)->got_estimate++;
	}
    }

    /* XXX what about disks that only got some estimates...  do we care? */
    /* XXX amanda 2.1 treated that case as a bad msg */

    for(dp = hostp->disks; dp != NULL; dp = dp->hostnext) {
	remove_disk(&waitq, dp);
	if(est(dp)->got_estimate) {
	    if(est(dp)->est_size[0] >= 0) {
		enqueue_disk(&estq, dp);
	    } else {
		enqueue_disk(&failq, dp);

		est(dp)->errstr = vstralloc("disk ", dp->name,
					    " offline on ", dp->host->hostname,
					    "?",
					    NULL);
	    }

	    fprintf(stderr,"got result for host %s disk %s:",
		    dp->host->hostname, dp->name);
	    fprintf(stderr," %d -> %ldK, %d -> %ldK, %d -> %ldK\n",
		    est(dp)->level[0], est(dp)->est_size[0],
		    est(dp)->level[1], est(dp)->est_size[1],
		    est(dp)->level[2], est(dp)->est_size[2]);
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
    return;

 NAK_parse_failed:

    if(msgdisk_undo) {
	*msgdisk_undo = msgdisk_undo_ch;
	msgdisk_undo = NULL;
    }
    errbuf = vstralloc(hostp->hostname, " NAK: ", "[NAK parse failed]", NULL);
    fprintf(stderr, "got strange nak from %s:\n----\n%s----\n\n",
	    hostp->hostname, pkt->body);
    goto error_return;

 bogus_error_packet:

    errbuf = vstralloc(hostp->hostname, ": [bogus error packet]", NULL);
    goto error_return;

 bad_msg:

    if(msgdisk_undo) {
	*msgdisk_undo = msgdisk_undo_ch;
	msgdisk_undo = NULL;
    }
    fprintf(stderr,"got a bad message, stopped at:\n");
    fprintf(stderr,"----\n%s----\n\n", resp);
    errbuf = vstralloc("badly formatted response from ", hostp->hostname, NULL);
    goto error_return;

 error_return:

    if(msgdisk_undo) {
	*msgdisk_undo = msgdisk_undo_ch;
	msgdisk_undo = NULL;
    }
    for(dp = hostp->disks; dp != NULL; dp = dp->hostnext) {
	remove_disk(&waitq, dp);
	enqueue_disk(&failq, dp);

	est(dp)->errstr = stralloc(errbuf);
	fprintf(stderr, "error result for host %s disk %s: %s\n",
	    dp->host->hostname, dp->name, errbuf);
    }
    afree(errbuf);
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

    ep = est(dp);

    fprintf(stderr, "pondering %s:%s... ",
	    dp->host->hostname, dp->name);
    fprintf(stderr, "next_level0 %d last_level %d ",
	    ep->next_level0, ep->last_level);

    if(ep->next_level0 <= 0) {
	fprintf(stderr,"(due for level 0) ");
	ep->dump_level = 0;
	ep->dump_size = est_tape_size(dp, 0);
	total_lev0 += (double) ep->dump_size;
	if(ep->last_level == -1 || dp->skip_incr) {
	    fprintf(stderr,"(%s disk, can't switch to degraded mode)\n",
		    dp->skip_incr? "skip-incr":"new");
	    ep->degr_level = -1;
	    ep->degr_size = -1;
	}
	else {
	    /* fill in degraded mode info */
	    fprintf(stderr,"(picking inclevel for degraded mode)\n");
	    ep->degr_level = pick_inclevel(dp);
	    ep->degr_size = est_tape_size(dp, ep->degr_level);
	}
    }
    else {
	fprintf(stderr,"(not due for a full dump, picking an incr level)\n");
	/* XXX - if this returns -1 may be we should force a total? */
	ep->dump_level = pick_inclevel(dp);
	ep->dump_size = est_tape_size(dp, ep->dump_level);
    }

    fprintf(stderr,"  curr level %d size %ld ", ep->dump_level, ep->dump_size);

    insert_disk(&schedq, dp, schedule_order);

    total_size += TAPE_BLOCK_SIZE + ep->dump_size + tape_mark;

    /* update the balanced size */
    if(!(dp->skip_full || dp->strategy == DS_NOFULL)) {
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
	log(L_WARNING,
	    "Could not get estimate for %s:%s, using historical data.",
	    dp->host->hostname, dp->name);
	analyze_estimate(dp);
	return;
    }
#endif

    errstr = est(dp)->errstr? est(dp)->errstr : "hmm, no error indicator!";

    fprintf(stderr, "planner: FAILED %s %s 0 [%s]\n",
	dp->host->hostname, dp->name, errstr);

    log(L_FAIL, "%s %s 0 [%s]",
	dp->host->hostname, dp->name, errstr);

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
	fprintf(stderr,"   picklev: no estimate for level %d, so no incs\n", base_level);
	return -1;
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
    log(L_INFO, "Incremental of %s:%s bumped to level %d.",
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

static void delay_remove_dump P((disk_t *dp, char *errstr));
static void delay_modify_dump P((disk_t *dp, char *errstr));

static void delay_dumps P((void))
/* delay any dumps that will not fit */
{
    disk_t *dp, *ndp, *preserve;
    char *errbuf = NULL;
    bi_t *bi, *nbi;
    long new_total;	/* New total_size */

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

	if(est(dp)->dump_level == 0 && est(dp)->dump_size > tape->length) {
	    if(est(dp)->last_level == -1 || dp->skip_incr) {
		errbuf = vstralloc(dp->host->hostname,
				   " ", dp->name,
				   " ", "0",
				   " [dump larger than tape,",
				   " but cannot incremental dump",
				   " ", dp->skip_incr ? "skip-incr": "new",
				   " disk]",
				   NULL);
		delay_remove_dump(dp, errbuf);
	    }
	    else {
		errbuf = vstralloc("Dump larger than tape:",
				   " full dump of ", dp->host->hostname,
				   ":", dp->name, " delayed.",
				   NULL);
		delay_modify_dump(dp, errbuf);
	    }
	}

	if(est(dp)->dump_level != 0 && est(dp)->dump_size > tape->length) {
	    char level_str[NUM_STR_SIZE];

	    ap_snprintf(level_str, sizeof(level_str),
			"%d", est(dp)->dump_level);
	    errbuf = vstralloc(dp->host->hostname,
			       " ", dp->name,
			       " ", level_str,
			       " ", "[dump larger than tape,"
			       " ", "skipping incremental]",
			       NULL);
	    delay_remove_dump(dp, errbuf);
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

    preserve = NULL;
    for(dp = schedq.head; dp != NULL && preserve == NULL; dp = dp->next)
	if(est(dp)->dump_level == 0)
	    preserve = dp;

    for(dp = schedq.tail;
		dp != NULL && total_size > tape_length;
		dp = ndp) {
	ndp = dp->prev;

	if(est(dp)->dump_level == 0 && dp != preserve) {
	    if(est(dp)->last_level == -1 || dp->skip_incr) {
		errbuf = vstralloc(dp->host->hostname,
				   " ", dp->name,
				   " ", "0",
				   " [dumps too bug,",
				   " but cannot incremental dump",
				   " ", dp->skip_incr ? "skip-incr": "new",
				   " disk]",
				   NULL);
		delay_remove_dump(dp, errbuf);
	    }
	    else {
		errbuf = vstralloc("Dump too big for tape:",
				   " full dump of ", dp->host->hostname,
				   ":", dp->name, " delayed.",
				   NULL);
		delay_modify_dump(dp, errbuf);
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
	    char level_str[NUM_STR_SIZE];

	    ap_snprintf(level_str, sizeof(level_str),
			"%d", est(dp)->dump_level);
	    errbuf = vstralloc(dp->host->hostname,
			       " ", dp->name,
			       " ", level_str,
			       " ", "[dumps way too big,"
			       " ", "must skipp incremental dumps]",
			       NULL);
	    delay_remove_dump(dp, errbuf);
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
	    new_total = total_size + TAPE_BLOCK_SIZE + est(dp)->dump_size + tape_mark;
	else
	    new_total = total_size - est(dp)->dump_size + bi->size;

	if(new_total <= tape_length) { /* reinstate it */
	    if(bi->deleted) {
		total_size = new_total;
		total_lev0 += (double) est(dp)->dump_size;
		insert_disk(&schedq, dp, schedule_order);
	    }
	    else {
		total_size = new_total;
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
	    afree(bi->errstr);
	    afree(bi);
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
	    fprintf(stderr, "planner: FAILED %s\n", bi->errstr);
	    log(L_FAIL, "%s", bi->errstr);
	}
	else {
	    dp = bi->dp;
	    fprintf(stderr, "  delay: %s  Now at level %d.\n",
		bi->errstr, est(dp)->dump_level);
	    log(L_INFO, "%s", bi->errstr);
	}

	/* Clean up - dont be too fancy! */
	afree(bi->errstr);
	afree(bi);
    }

    afree(errbuf);

    fprintf(stderr, "  delay: Total size now %ld.\n", total_size);

    return;
}


static void delay_remove_dump(dp, errstr)
disk_t *dp;
char *errstr;
/* Remove a dump - keep track on the bi q */
{
    bi_t *bi;

    total_size -= TAPE_BLOCK_SIZE + est(dp)->dump_size + tape_mark;
    if(est(dp)->dump_level == 0)
	total_lev0 -= (double) est(dp)->dump_size;

    bi = alloc(sizeof(bi_t));
    bi->next = NULL;
    bi->prev = biq.tail;
    if(biq.tail == NULL)
	biq.head = bi;
    else
	biq.tail->next = bi;
    biq.tail = bi;

    bi->deleted = 1;
    bi->dp = dp;
    bi->errstr = stralloc(errstr);

    remove_disk(&schedq, dp);

    return;
}


static void delay_modify_dump(dp, errstr)
disk_t *dp;
char *errstr;
/* Modify a dump from total to incr - keep track on the bi q */
{
    bi_t *bi;

    total_size -= est(dp)->dump_size;
    total_lev0 -= (double) est(dp)->dump_size;

    bi = alloc(sizeof(bi_t));
    bi->next = NULL;
    bi->prev = biq.tail;
    if (biq.tail == NULL)
	biq.head = bi;
    else
	biq.tail->next = bi;
    biq.tail = bi;

    bi->deleted = 0;
    bi->dp = dp;
    bi->level = est(dp)->dump_level;
    bi->size = est(dp)->dump_size;
    bi->errstr = stralloc(errstr);

    est(dp)->dump_level = est(dp)->degr_level;
    est(dp)->dump_size = est(dp)->degr_size;

    total_size += est(dp)->dump_size;

    return;
}


static int promote_highest_priority_incremental P((void))
{
    disk_t *dp;
    long new_size, new_total, new_lev0;
    int check_days, check_limit;

    /*
     * return 1 if did so; must update total_size correctly; must not
     * cause total_size to exceed tape_length
     */
    check_limit = conf_dumpcycle - 1;
    fprintf(stderr, "   promote: checking up to %d days ahead\n",
	    check_limit - 1);

    for(check_days = 1; check_days < check_limit; check_days++) {
	fprintf(stderr, "   promote: checking %d days now\n", check_days);

	for(dp = schedq.head; dp != NULL; dp = dp->next) {
	    if(dp->skip_full || dp->strategy == DS_NOFULL) {
		fprintf(stderr,
	"    promote: can't move %s:%s: no full dumps allowed.\n",
			dp->host->hostname, dp->name);
		continue;
	    }

	    if(est(dp)->next_level0 != check_days)
		continue; /* totals continue here too */

	    new_size = est_tape_size(dp, 0);
	    new_total = total_size - est(dp)->dump_size + new_size;
	    new_lev0 = total_lev0 + new_size;

	    if(new_total > tape_length
	       || new_lev0 > balanced_size + balance_threshold) {

		fprintf(stderr,
	"  promote: %s:%s too big: new size %ld total %ld, bal size %1.0f thresh %1.0f\n",
			dp->host->hostname, dp->name, new_size,
			new_lev0, balanced_size, balance_threshold);
		continue;
	    }

	    total_size = new_total;
	    total_lev0 = new_lev0;
	    est(dp)->degr_level = est(dp)->dump_level;
	    est(dp)->degr_size = est(dp)->dump_size;
	    est(dp)->dump_level = 0;
	    est(dp)->dump_size = new_size;
	    est(dp)->next_level0 = 0;

	    fprintf(stderr,
	"   promote: moving %s:%s up, total_lev0 %1.0f, total_size %ld\n",
		    dp->host->hostname, dp->name,
		    total_lev0, total_size);

	    log(L_INFO,
		"Full dump of %s:%s promoted from %d days ahead.",
		dp->host->hostname, dp->name, check_days);

	    return 1;
	}
    }

    return 0;
}


static int promote_hills P((void))
{
    disk_t *dp;
    struct balance_stats {
	int disks;
	long size;
    } *sp;
    int tapecycle;
    int days;
    int hill_days;
    long hill_size;
    long new_size;
    long new_total;

    /* If we are already doing a level 0 don't bother */
    if(total_lev0 > 0)
	return 0;

    /* Do the guts of an "amadmin balance" */
    tapecycle = conf_tapecycle;

    sp = (struct balance_stats *)
	alloc(sizeof(struct balance_stats) * tapecycle);

    for(days = 0; days < tapecycle; days++)
	sp[days].disks = sp[days].size = 0;

    for(dp = schedq.head; dp != NULL; dp = dp->next) {
	days = est(dp)->next_level0;   /* This is > 0 by definition */
	if(days<tapecycle && !dp->skip_full && dp->strategy != DS_NOFULL) {
	    sp[days].disks++;
	    sp[days].size += est(dp)->last_lev0size;
	}
    }

    /* Search for a suitable big hill and cut it down */
    while(1) {
	/* Find the tallest hill */
	hill_size = 0;
	for(days = 0; days < tapecycle; days++) {
	    if(sp[days].disks > 1 && sp[days].size > hill_size) {
		hill_size = sp[days].size;
		hill_days = days;
	    }
	}

	if(hill_size <= 0) break;	/* no suitable hills */

	/* Find all the dumps in that hill and try and remove one */
	for(dp = schedq.head; dp != NULL; dp = dp->next) {
	    if(est(dp)->next_level0 != hill_days ||
	       dp->skip_full ||
	       dp->strategy == DS_NOFULL)
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

	    log(L_INFO,
		"Full dump of %s:%s specially promoted from %d days ahead.",
		dp->host->hostname, dp->name, hill_days);

	    afree(sp);
	    return 1;
	}
	/* All the disks in that hill were unsuitable. */
	sp[hill_days].disks = 0;	/* Don't get tricked again */
    }

    afree(sp);
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
    long dump_time, degr_time;
    char *schedline = NULL, *degr_str = NULL;
    char dump_priority_str[NUM_STR_SIZE];
    char dump_level_str[NUM_STR_SIZE];
    char dump_size_str[NUM_STR_SIZE];
    char dump_time_str[NUM_STR_SIZE];
    char degr_level_str[NUM_STR_SIZE];
    char degr_size_str[NUM_STR_SIZE];
    char degr_time_str[NUM_STR_SIZE];
    char *dump_date, *degr_date;
    int i;

    ep = est(dp);

    if(ep->dump_size == -1) {
	/* no estimate, fail the disk */
	fprintf(stderr,
		"planner: FAILED %s %s %d [no estimate or historical data]\n",
		dp->host->hostname, dp->name, ep->dump_level);
	log(L_FAIL, "%s %s %d [no estimate or historical data]",
	    dp->host->hostname, dp->name, ep->dump_level);
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

	if(ep->degr_level != -1) {
	    degr_time = ep->degr_size / fix_rate(ep->incrrate);
	}
    }
    else {
	dump_time = ep->dump_size / fix_rate(ep->incrrate);
    }

    if(ep->dump_level == 0 && ep->degr_level != -1) {
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
    schedline = vstralloc(dp->host->hostname,
			  " ", dp->name,
			  " ", dump_priority_str,
			  " ", dump_level_str,
			  " ", dump_date,
			  " ", dump_size_str,
			  " ", dump_time_str,
			  degr_str ? degr_str : "",
			  "\n", NULL);

    fputs(schedline, stdout);
    fputs(schedline, stderr);
    afree(schedline);
    afree(degr_str);
}
