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
 * $Id: driver.c,v 1.70 1999/02/16 03:18:40 martinea Exp $
 *
 * controlling process for the Amanda backup system
 */

/*
 * XXX possibly modify tape queue to be cognizant of how much room is left on
 *     tape.  Probably not effective though, should do this in planner.
 */

#include "amanda.h"
#include "clock.h"
#include "conffile.h"
#include "diskfile.h"
#include "holding.h"
#include "infofile.h"
#include "logfile.h"
#include "statfs.h"
#include "token.h"
#include "version.h"
#include "driverio.h"

disklist_t waitq, runq, stoppedq, tapeq;
int pending_aborts, inside_dump_to_tape;
int use_lffo;
disk_t *taper_disk;
int big_dumpers;
int degraded_mode;
unsigned long reserved_space;
unsigned long total_disksize;
char *dumper_program;

int driver_main P((int argc, char **argv));
int client_constrained P((disk_t *dp));
int sort_by_priority_reversed P((disk_t *a, disk_t *b));
int sort_by_time P((disk_t *a, disk_t *b));
int sort_by_size_reversed P((disk_t *a, disk_t *b));
int start_some_dumps P((disklist_t *rq));
void dump_schedule P((disklist_t *qp, char *str));
void start_degraded_mode P((disklist_t *queuep));
void handle_taper_result P((void));
dumper_t *idle_dumper P((void));
int some_dumps_in_progress P((void));
int num_busy_dumpers P((void));
dumper_t *lookup_dumper P((int fd));
char *construct_datestamp P((void));
void handle_dumper_result P((int fd));
disklist_t read_schedule P((disklist_t *waitqp));
int free_kps P((interface_t *ip));
void interface_state P((char *time_str));
void allocate_bandwidth P((interface_t *ip, int kps));
void deallocate_bandwidth P((interface_t *ip, int kps));
unsigned long free_space P((void));
holdingdisk_t *find_diskspace P((unsigned long size, int *cur_idle));
char *diskname2filename P((char *dname));
void assign_holdingdisk P((holdingdisk_t *holdp, disk_t *diskp));
void adjust_diskspace P((disk_t *diskp, tok_t tok));
void delete_diskspace P((disk_t *diskp));
void holdingdisk_state P((char *time_str));
int dump_to_tape P((disk_t *dp));
int queue_length P((disklist_t q));
void short_dump_state P((void));
void dump_state P((char *str));
int main P((int argc, char **argv));

#define LITTLE_DUMPERS 3

static int idle_reason;
char *datestamp;

#define max(a, b)     ((a) > (b)? (a) : (b))
#define min(a, b)     ((a) < (b)? (a) : (b))
char *idle_strings[] = {
#define NOT_IDLE		0
    "not-idle",
#define IDLE_START_WAIT		1
    "start-wait",
#define IDLE_NO_DUMPERS		2
    "no-dumpers",
#define IDLE_NO_HOLD		3
    "no-hold",
#define IDLE_CLIENT_CONSTRAINED	4
    "client-constrained",
#define IDLE_NO_DISKSPACE	5
    "no-diskspace",
#define IDLE_TOO_LARGE		6
    "file-too-large",
#define IDLE_NO_BANDWIDTH	7
    "no-bandwidth",
#define IDLE_TAPER_WAIT		8
    "taper-wait",
};

#define SLEEP_MAX		(24*3600)
struct timeval sleep_time = { SLEEP_MAX, 0 };
/* enabled if any disks are in start-wait: */
int any_delayed_disk = 0;

int main(main_argc, main_argv)
     int main_argc;
     char **main_argv;
{
    disklist_t *origqp;
    disk_t *diskp;
    fd_set selectset;
    int fd, dsk;
    dumper_t *dumper;
    char *newdir = NULL;
    generic_fs_stats_t fs;
    holdingdisk_t *hdp;
    unsigned long malloc_hist_1, malloc_size_1;
    unsigned long malloc_hist_2, malloc_size_2;
    unsigned long reserve = 100;
    tok_t tok;
    int result_argc;
    char *result_argv[MAX_ARGS+1];
    char *taper_program;

    for(fd = 3; fd < FD_SETSIZE; fd++) {
	/*
	 * Make sure nobody spoofs us with a lot of extra open files
	 * that would cause an open we do to get a very high file
	 * descriptor, which in turn might be used as an index into
	 * an array (e.g. an fd_set).
	 */
	close(fd);
    }

    set_pname("driver");

    malloc_size_1 = malloc_inuse(&malloc_hist_1);

    erroutput_type = (ERR_AMANDALOG|ERR_INTERACTIVE);
    set_logerror(logerror);

    startclock();
    FD_ZERO(&readset);

    printf("%s: pid %ld executable %s version %s\n",
	   get_pname(), (long) getpid(), main_argv[0], version());

    if(read_conffile(CONFFILE_NAME))
	error("could not read amanda config file\n");

    amfree(datestamp);
    datestamp = construct_datestamp();
    log_add(L_START,"date %s", datestamp);

    taper_program = vstralloc(libexecdir, "/", "taper", versionsuffix(), NULL);
    dumper_program = vstralloc(libexecdir, "/", "dumper", versionsuffix(),
			       NULL);

    /* taper takes a while to get going, so start it up right away */

    startup_tape_process(taper_program);
    taper_cmd(START_TAPER, datestamp, NULL, 0, NULL);

    /* start initializing: read in databases */

    if((origqp = read_diskfile(getconf_str(CNF_DISKFILE))) == NULL)
	error("could not read disklist file\n");

    /* set up any configuration-dependent variables */

    inparallel	= getconf_int(CNF_INPARALLEL);
    big_dumpers	= inparallel - LITTLE_DUMPERS;
    use_lffo	= 1;

    reserve = getconf_int(CNF_RESERVE);
    if (reserve < 0 || reserve > 100) {
	log_add(L_WARNING, "WARNING: reserve must be between 0 and 100!");
	reserve = 100;
    }

    total_disksize = 0;
    for(hdp = getconf_holdingdisks(), dsk = 0; hdp != NULL; hdp = hdp->next, dsk++) {
	struct stat stat_hdp;
	hdp->up = (void *)alloc(sizeof(holdalloc_t));
	holdalloc(hdp)->allocated_dumpers = 0;
	holdalloc(hdp)->allocated_space = 0L;

	if(get_fs_stats(hdp->diskdir, &fs) == -1
	   || access(hdp->diskdir, W_OK) == -1) {
	    log_add(L_WARNING, "WARNING: ignoring holding disk %s: %s\n",
		    hdp->diskdir, strerror(errno));
	    hdp->disksize = 0L;
	    continue;
	}

	if(fs.avail != -1) {
	    if(hdp->disksize > 0) {
		if(hdp->disksize > fs.avail) {
		    log_add(L_WARNING,
			    "WARNING: %s: %ld KB requested, but only %ld KB available.",
			    hdp->diskdir, hdp->disksize, fs.avail);
			    hdp->disksize = fs.avail;
		}
	    }
	    else if(fs.avail + hdp->disksize < 0) {
		log_add(L_WARNING,
			"WARNING: %s: not %ld KB free.",
			hdp->diskdir, -hdp->disksize);
		hdp->disksize = 0L;
		continue;
	    }
	    else
		hdp->disksize += fs.avail;
	}

	printf("driver: adding holding disk %d dir %s size %ld\n",
	       dsk, hdp->diskdir, hdp->disksize);

	newdir = newvstralloc(newdir,
			      hdp->diskdir, "/", datestamp,
			      NULL);
	if (stat(newdir, &stat_hdp) == -1) {
	    if (mkdir(newdir, 0770) == -1) {
		log_add(L_WARNING, "WARNING: could not create %s: %s",
			newdir, strerror(errno));
		hdp->disksize = 0L;
	    }
	}
	else {
	    if (!S_ISDIR((stat_hdp.st_mode))) {
		log_add(L_WARNING, "WARNING: %s is not a directory",
			newdir);
		hdp->disksize = 0L;
	    }
	    else if (access(newdir,W_OK) == -1) {
		log_add(L_WARNING, "WARNING: directory %s is not writable",
			newdir);
	    }
	}
	total_disksize += hdp->disksize;
    }

    reserved_space = total_disksize * (reserve / 100.0);

    printf("reserving %ld out of %ld for degraded-mode dumps\n",
		reserved_space, free_space());

    amfree(newdir);

    if(inparallel > MAX_DUMPERS) inparallel = MAX_DUMPERS;

    /* fire up the dumpers now while we are waiting */

    startup_dump_processes(dumper_program);

    /*
     * Read schedule from stdin.  Usually, this is a pipe from planner,
     * so the effect is that we wait here for the planner to
     * finish, but meanwhile the taper is rewinding the tape, reading
     * the label, checking it, writing a new label and all that jazz
     * in parallel with the planner.
     */

    waitq = *origqp;
    runq = read_schedule(&waitq);

    stoppedq.head = stoppedq.tail = NULL;
    tapeq.head = tapeq.tail = NULL;

    log_add(L_STATS, "startup time %s", walltime_str(curclock()));

    printf("driver: start time %s inparallel %d bandwidth %d diskspace %lu",
	   walltime_str(curclock()), inparallel, free_kps((interface_t *)0),
	   free_space());
    printf(" dir %s datestamp %s driver: drain-ends tapeq %s big-dumpers %d\n",
	   "OBSOLETE", datestamp,  use_lffo? "LFFO" : "FIFO", big_dumpers);
    fflush(stdout);

    /* ok, planner is done, now lets see if the tape is ready */

    tok = getresult(taper, 1, &result_argc, result_argv, MAX_ARGS+1);

    if(tok != TAPER_OK) {
	/* no tape, go into degraded mode: dump to holding disk */
	start_degraded_mode(&runq);
	FD_CLR(taper,&readset);
    }

    while(start_some_dumps(&runq) || some_dumps_in_progress() ||
	  any_delayed_disk) {

	short_dump_state();

	/* wait for results */

	memcpy(&selectset, &readset, sizeof(fd_set));
	if(select(maxfd+1, (SELECT_ARG_TYPE *)(&selectset),
		  NULL, NULL, &sleep_time) == -1)
	    error("select: %s", strerror(errno));

	/* handle any results that have come in */

	for(fd = 0; fd <= maxfd; fd++) if(FD_ISSET(fd, &selectset)) {
	    if(fd == taper) handle_taper_result();
	    else handle_dumper_result(fd);
	}

    }

    /* handle any remaining dumps by dumping directly to tape, if possible */

    while(!empty(runq)) {
	diskp = dequeue_disk(&runq);
	if(!degraded_mode) {
	    int rc = dump_to_tape(diskp);
	    if(rc == 1)
		log_add(L_INFO,
			"%s %s %d [dump to tape failed, will try again]",
		        diskp->host->hostname,
			diskp->name,
			sched(diskp)->level);
	    else if(rc == 2)
		log_add(L_FAIL, "%s %s %d [dump to tape failed]",
		        diskp->host->hostname,
			diskp->name,
			sched(diskp)->level);
	}
	else
	    log_add(L_FAIL, "%s %s %d [%s]",
		    diskp->host->hostname, diskp->name, sched(diskp)->level,
		diskp->no_hold ?
		    "can't dump no-hold disk in degraded mode" :
		    "no more holding disk space");
    }

    short_dump_state();				/* for amstatus */

    printf("driver: QUITTING time %s telling children to quit\n",
           walltime_str(curclock()));
    fflush(stdout);

    for(dumper = dmptable; dumper < dmptable + inparallel; dumper++) {
	dumper_cmd(dumper, QUIT, NULL);
	amfree(dumper->name);
    }

    if(taper)
	taper_cmd(QUIT, NULL, NULL, 0, NULL);

    /* wait for all to die */

    while(wait(NULL) != -1);

    if(!degraded_mode) {
	for(hdp = getconf_holdingdisks(); hdp != NULL; hdp = hdp->next) {
	    newdir = newvstralloc(newdir,
				  hdp->diskdir, "/", datestamp,
				  NULL);
	    if(rmdir(newdir) != 0)
		log_add(L_WARNING, "Could not rmdir %s: %s",
		        newdir, strerror(errno));
	    amfree(hdp->up);
	}
    }
    amfree(newdir);

    printf("driver: FINISHED time %s\n", walltime_str(curclock()));
    fflush(stdout);
    log_add(L_FINISH,"date %s time %s", datestamp, walltime_str(curclock()));
    amfree(datestamp);

    amfree(dumper_program);
    amfree(taper_program);

    malloc_size_2 = malloc_inuse(&malloc_hist_2);

    if(malloc_size_1 != malloc_size_2) {
	malloc_list(fileno(stderr), malloc_hist_1, malloc_hist_2);
    }

    return 0;
}

int client_constrained(dp)
disk_t *dp;
{
    disk_t *dp2;

    /* first, check if host is too busy */

    if(dp->host->inprogress >= dp->host->maxdumps) {
	return 1;
    }

    /* next, check conflict with other dumps on same spindle */

    if(dp->spindle == -1) {	/* but spindle -1 never conflicts by def. */
	return 0;
    }

    for(dp2 = dp->host->disks; dp2 != NULL; dp2 = dp2->hostnext)
	if(dp2->inprogress && dp2->spindle == dp->spindle) {
	    return 1;
	}

    return 0;
}

#define is_bigdumper(d) (((d)-dmptable) >= (inparallel-big_dumpers))

int start_some_dumps(rq)
disklist_t *rq;
{
    int total, cur_idle;
    disk_t *diskp, *big_degraded_diskp;
    dumper_t *dumper;
    holdingdisk_t *holdp, *big_degraded_holdp;
    time_t now = time(NULL);

    if(rq->head == NULL) {
	idle_reason = 0;
	return 0;
    }

    total = 0;
    idle_reason = IDLE_NO_DUMPERS;
    sleep_time.tv_sec = SLEEP_MAX;
    sleep_time.tv_usec = 0;
    any_delayed_disk = 0;

    /*
     * A potential problem with starting from the bottom of the dump time
     * distribution is that a slave host will have both one of the shortest
     * and one of the longest disks, so starting its shortest disk first will
     * tie up the host and eliminate its longest disk from consideration the
     * first pass through.  This could cause a big delay in starting that long
     * disk, which could drag out the whole night's dumps.
     *
     * While starting from the top of the dump time distribution solves the
     * above problem, this turns out to be a bad idea, because the big dumps
     * will almost certainly pack the holding disk completely, leaving no
     * room for even one small dump to start.  This ends up shutting out the
     * small-end dumpers completely (they stay idle).
     *
     * The introduction of multiple simultaneous dumps to one host alleviates
     * the biggest&smallest dumps problem: both can be started at the
     * beginning.
     */
    for(dumper = dmptable; dumper < dmptable+inparallel; dumper++) {
	if(dumper->busy || dumper->down) continue;
	/* found an idle dumper, now find a disk for it */
	if(is_bigdumper(dumper)) diskp = rq->tail;
	else diskp = rq->head;
	big_degraded_diskp = NULL;

	if(idle_reason == IDLE_NO_DUMPERS)
	    idle_reason = NOT_IDLE;

	cur_idle = NOT_IDLE;

	while(diskp) {
	    assert(diskp->host != NULL && sched(diskp) != NULL);

	    if(diskp->host->start_t > now) {
		cur_idle = max(cur_idle, IDLE_START_WAIT);
		sleep_time.tv_sec = min(diskp->host->start_t - now, 
					sleep_time.tv_sec);
		any_delayed_disk = 1;
	    }
	    else if(diskp->start_t > now) {
		cur_idle = max(cur_idle, IDLE_START_WAIT);
		sleep_time.tv_sec = min(diskp->start_t - now, 
					sleep_time.tv_sec);
		any_delayed_disk = 1;
	    }
	    else if(sched(diskp)->est_kps > free_kps(diskp->host->netif))
		cur_idle = max(cur_idle, IDLE_NO_BANDWIDTH);
	    else if((holdp = find_diskspace(sched(diskp)->est_size,&cur_idle)) == NULL)
		cur_idle = max(cur_idle, IDLE_NO_DISKSPACE);
	    else if(diskp->no_hold)
		cur_idle = max(cur_idle, IDLE_NO_HOLD);
	    else if(client_constrained(diskp))
		cur_idle = max(cur_idle, IDLE_CLIENT_CONSTRAINED);
	    else {

		/* disk fits, dump it */
		if(is_bigdumper(dumper) && degraded_mode) {
		   if(!big_degraded_diskp || 
		      sched(diskp)->priority > big_degraded_diskp->priority) {
			big_degraded_diskp = diskp;
			big_degraded_holdp = holdp;
		    }
		}
		else {
		    cur_idle = NOT_IDLE;
		    break;
		}
	    }
	    if(is_bigdumper(dumper)) diskp = diskp->prev;
	    else diskp = diskp->next;
	}

	if(is_bigdumper(dumper) && degraded_mode) {
	    diskp = big_degraded_diskp;
	    holdp = big_degraded_holdp;
	    if(big_degraded_diskp) cur_idle = NOT_IDLE;
	}
	if(diskp && cur_idle == NOT_IDLE) {
	    allocate_bandwidth(diskp->host->netif, sched(diskp)->est_kps);
	    assign_holdingdisk(holdp, diskp);
	    diskp->host->inprogress += 1;	/* host is now busy */
	    diskp->inprogress = 1;
	    sched(diskp)->dumper = dumper;
	    sched(diskp)->holdp = holdp;
	    sched(diskp)->timestamp = time((time_t *)0);

	    dumper->busy = 1;		/* dumper is now busy */
	    dumper->dp = diskp;		/* link disk to dumper */
	    total++;
	    remove_disk(rq, diskp);		/* take it off the run queue */
	    dumper_cmd(dumper, FILE_DUMP, diskp);
	    diskp->host->start_t = time(NULL) + 15;
	}
	idle_reason = max(idle_reason, cur_idle);
    }
    return total;
}

int sort_by_priority_reversed(a, b)
disk_t *a, *b;
{
    if(sched(b)->priority - sched(a)->priority != 0)
	return sched(b)->priority - sched(a)->priority;
    else
	return sort_by_time(a, b);
}

int sort_by_time(a, b)
disk_t *a, *b;
{
    long diff;

    if ((diff = sched(a)->est_time - sched(b)->est_time) < 0) {
	return -1;
    } else if (diff > 0) {
	return 1;
    } else {
	return 0;
    }
}

int sort_by_size_reversed(a, b)
disk_t *a, *b;
{
    long diff;

    if ((diff = sched(a)->est_size - sched(b)->est_size) < 0) {
	return -1;
    } else if (diff > 0) {
	return 1;
    } else {
	return 0;
    }
}

void dump_schedule(qp, str)
disklist_t *qp;
char *str;
{
    disk_t *dp;

    printf("dump of driver schedule %s:\n--------\n", str);

    for(dp = qp->head; dp != NULL; dp = dp->next) {
	printf("  %-10.10s %.16s lv %d t %5ld s %8lu p %d\n",
	       dp->host->hostname, dp->name, sched(dp)->level,
	       sched(dp)->est_time, sched(dp)->est_size, sched(dp)->priority);
    }
    printf("--------\n");
}


void start_degraded_mode(queuep)
disklist_t *queuep;
{
    disk_t *dp;
    disklist_t newq;
    unsigned long est_full_size;

    newq.head = newq.tail = 0;

    dump_schedule(queuep, "before start degraded mode");

    est_full_size = 0;
    while(!empty(*queuep)) {
	dp = dequeue_disk(queuep);

	if(sched(dp)->level != 0)
	    /* go ahead and do the disk as-is */
	    insert_disk(&newq, dp, sort_by_priority_reversed);
	else {
	    if (reserved_space + est_full_size + sched(dp)->est_size
		<= total_disksize) {
		insert_disk(&newq, dp, sort_by_priority_reversed);
		est_full_size += sched(dp)->est_size;
	    }
	    else if(sched(dp)->degr_level != -1) {
		sched(dp)->level = sched(dp)->degr_level;
		sched(dp)->dumpdate = sched(dp)->degr_dumpdate;
		sched(dp)->est_size = sched(dp)->degr_size;
		sched(dp)->est_time = sched(dp)->degr_time;
		sched(dp)->est_kps  = sched(dp)->degr_kps;
		insert_disk(&newq, dp, sort_by_priority_reversed);
	    }
	    else {
		log_add(L_FAIL, "%s %s %d [can't switch to incremental dump]",
		        dp->host->hostname, dp->name, sched(dp)->level);
	    }
	}
    }

    *queuep = newq;
    degraded_mode = 1;

    dump_schedule(queuep, "after start degraded mode");
}


void handle_taper_result()
{
    disk_t *dp;
    int filenum;
    tok_t tok;
    int result_argc;
    char *result_argv[MAX_ARGS+1];

    tok = getresult(taper, 1, &result_argc, result_argv, MAX_ARGS+1);

    switch(tok) {

    case DONE:	/* DONE <handle> <label> <tape file> <err mess> */
	if(result_argc != 5) {
	    error("error: [taper DONE result_argc != 5: %d", result_argc);
	}

	dp = serial2disk(result_argv[2]);
	free_serial(result_argv[2]);

	filenum = atoi(result_argv[4]);
	update_info_taper(dp, result_argv[3], filenum);

	delete_diskspace(dp);

	printf("driver: finished-cmd time %s taper wrote %s:%s\n",
	       walltime_str(curclock()), dp->host->hostname, dp->name);
	fflush(stdout);

	amfree(sched(dp)->dumpdate);
	amfree(dp->up);

	if(empty(tapeq)) {
	    taper_busy = 0;
	    taper_disk = NULL;
	}
	else {
	    dp = dequeue_disk(&tapeq);
	    taper_disk = dp;
	    taper_cmd(FILE_WRITE, dp, sched(dp)->destname, sched(dp)->level, datestamp);
	}
	/*
	 * we need to restart some stopped dumps; without a good
	 * way to determine which ones to start, let them all compete
	 * for the remaining disk space.  Remember, having stopped
	 * processes indicates a failure in our estimation of sizes.
	 * Rather than have a complicated workaround to deal with
	 * stopped dumper processes more efficiently, we should
	 * work on getting better estimates to avoid the situation
	 * to begin with.
	 */
	while(!empty(stoppedq)) {
	    dp = dequeue_disk(&stoppedq);
	    dumper_cmd(sched(dp)->dumper, CONTINUE, NULL);
	}
	break;

    case TRYAGAIN:  /* TRY-AGAIN <handle> <err mess> */
	if (result_argc < 2) {
	    error("error [taper TRYAGAIN result_argc < 2: %d]", result_argc);
	}
	dp = serial2disk(result_argv[2]);
	free_serial(result_argv[2]);
	printf("driver: taper-tryagain time %s disk %s:%s\n",
	       walltime_str(curclock()), dp->host->hostname, dp->name);
	fflush(stdout);

	/* re-insert into taper queue */

	if(sched(dp)->attempted) {
	    log_add(L_FAIL, "%s %s %d [too many taper retries]",
		    dp->host->hostname, dp->name, sched(dp)->level);
	    /* XXX should I do this? */
	    delete_diskspace(dp);
	}
	else {
	    sched(dp)->attempted++;
	    if(use_lffo)insert_disk(&tapeq, dp, sort_by_size_reversed);
	    else enqueue_disk(&tapeq, dp);
	}

	/* run next thing from queue */

	dp = dequeue_disk(&tapeq);
	taper_disk = dp;
	taper_cmd(FILE_WRITE, dp, sched(dp)->destname, sched(dp)->level, datestamp);
	break;

    case TAPE_ERROR: /* TAPE-ERROR <handle> <err mess> */
	dp = serial2disk(result_argv[2]);
	free_serial(result_argv[2]);
	printf("driver: finished-cmd time %s taper wrote %s:%s\n",
	       walltime_str(curclock()), dp->host->hostname, dp->name);
	fflush(stdout);
	/* Note: fall through code... */

    case BOGUS:
	/*
	 * Since we've gotten a tape error, we can't send anything more
	 * to the taper.  Go into degraded mode to try to get everthing
	 * onto disk.  Later, these dumps can be flushed to a new tape.
	 * The tape queue is zapped so that it appears empty in future
	 * checks.
	 */
	log_add(L_WARNING, "going into degraded mode because of tape error.");
	start_degraded_mode(&runq);
	tapeq.head = tapeq.tail = NULL;
	taper_busy = 0;
	taper_disk = NULL;
	FD_CLR(taper,&readset);
	if(tok != TAPE_ERROR) aclose(taper);
	break;
    default:
	error("driver received unexpected token (%d) from taper", tok);
    }
}


dumper_t *idle_dumper()
{
    dumper_t *dumper;

    for(dumper = dmptable; dumper < dmptable+inparallel; dumper++)
	if(!dumper->busy && !dumper->down) return dumper;

    return NULL;
}

int some_dumps_in_progress()
{
    dumper_t *dumper;

    for(dumper = dmptable; dumper < dmptable+inparallel; dumper++)
	if(dumper->busy) return 1;

    return taper_busy;
}

int num_busy_dumpers()
{
    dumper_t *dumper;
    int n;

    n = 0;
    for(dumper = dmptable; dumper < dmptable+inparallel; dumper++)
	if(dumper->busy) n += 1;

    return n;
}

dumper_t *lookup_dumper(fd)
int fd;
{
    dumper_t *dumper;

    for(dumper = dmptable; dumper < dmptable+inparallel; dumper++)
	if(dumper->outfd == fd) return dumper;

    return NULL;
}

char *construct_datestamp()
{
    struct tm *tm;
    time_t timevar;
    char datestamp[3*NUM_STR_SIZE];

    timevar = time((time_t *)NULL);
    tm = localtime(&timevar);
    ap_snprintf(datestamp, sizeof(datestamp),
		"%04d%02d%02d", tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday);
    return stralloc(datestamp);
}

void handle_dumper_result(fd)
int fd;
{
    dumper_t *dumper;
    disk_t *dp, *sdp;
    long origsize;
    long dumpsize;
    long dumptime;
    tok_t tok;
    int result_argc;
    char *result_argv[MAX_ARGS+1];

    dumper = lookup_dumper(fd);
    dp = dumper->dp;
    assert(dp && sched(dp));

    tok = getresult(fd, 1, &result_argc, result_argv, MAX_ARGS+1);

    if(tok != BOGUS) {
	sdp = serial2disk(result_argv[2]); /* result_argv[2] always contains the serial number */
	assert(sdp == dp);
    }

    switch(tok) {

    case DONE: /* DONE <handle> <origsize> <dumpsize> <dumptime> <err str> */
	if(result_argc != 6) {
	    error("error [dumper DONE result_argc != 6: %d]", result_argc);
	}

	free_serial(result_argv[2]);

	origsize = (long)atof(result_argv[3]);
	dumpsize = (long)atof(result_argv[4]);
	dumptime = (long)atof(result_argv[5]);
	update_info_dumper(dp, origsize, dumpsize, dumptime);

	rename_tmp_holding(sched(dp)->destname, 1);
	deallocate_bandwidth(dp->host->netif, sched(dp)->est_kps);
	adjust_diskspace(dp, DONE);
	dumper->busy = 0;
	dp->host->inprogress -= 1;
	dp->inprogress = 0;
	sched(dp)->attempted = 0;
	printf("driver: finished-cmd time %s %s dumped %s:%s\n",
	       walltime_str(curclock()), dumper->name,
	       dp->host->hostname, dp->name);
	fflush(stdout);

	if(taper_busy) {
	    if(use_lffo)insert_disk(&tapeq, dp, sort_by_size_reversed);
	    else enqueue_disk(&tapeq, dp);
	}
	else if(!degraded_mode) {
	    taper_disk = dp;
	    taper_busy = 1;
	    taper_cmd(FILE_WRITE, dp, sched(dp)->destname, sched(dp)->level,
		      datestamp);
	}
	dp = NULL;
	break;

    case TRYAGAIN: /* TRY-AGAIN <handle> <err str> */
    case FATAL_TRYAGAIN:
	free_serial(result_argv[2]);

	rename_tmp_holding(sched(dp)->destname, 0);
	deallocate_bandwidth(dp->host->netif, sched(dp)->est_kps);
	adjust_diskspace(dp, DONE);
	delete_diskspace(dp);
	dumper->busy = 0;
	dp->host->inprogress -= 1;
	dp->inprogress = 0;

	if(sched(dp)->attempted) {
	    log_add(L_FAIL, "%s %s %d [could not connect to %s]",
		    dp->host->hostname, dp->name,
		    sched(dp)->level, dp->host->hostname);
	}
	else {
	    sched(dp)->attempted++;
	    enqueue_disk(&runq, dp);
	}

	if(tok == FATAL_TRYAGAIN) {
	    /* dumper is confused, start another */
	    log_add(L_WARNING, "%s (pid %ld) confused, restarting it.",
		    dumper->name, (long)dumper->pid);
	    FD_CLR(fd,&readset);
	    aclose(fd);
	    startup_dump_process(dumper, dumper_program);
	}
	/* sleep in case the dumper failed because of a temporary network
	   problem, as NIS or NFS... */
	sleep(15);
	break;

    case FAILED: /* FAILED <handle> <errstr> */
	free_serial(result_argv[2]);

	rename_tmp_holding(sched(dp)->destname, 0);
	deallocate_bandwidth(dp->host->netif, sched(dp)->est_kps);
	adjust_diskspace(dp, DONE);
	delete_diskspace(dp);
	dumper->busy = 0;
	dp->host->inprogress -= 1;
	dp->inprogress = 1;

	/* no need to log this, dumper will do it */
	/* sleep in case the dumper failed because of a temporary network
	   problem, as NIS or NFS... */
	sleep(15);
	break;

    case NO_ROOM: /* NO-ROOM <handle> */
	if(!taper_busy && empty(tapeq) && pending_aborts == 0) {
	    /* no disk space due to be freed */
	    dumper_cmd(dumper, ABORT, NULL);
	    pending_aborts++;
	    /*
	     * if this is the only outstanding dump, it must be too big for
	     * the holding disk, so force it to go directly to tape on the
	     * next attempt.
	     */
	    if(num_busy_dumpers() <= 1)
		dp->no_hold = 1;
	}
	else {
	    adjust_diskspace(dp, NO_ROOM);
	    enqueue_disk(&stoppedq, dp);
	}
	break;

    case ABORT_FINISHED: /* ABORT-FINISHED <handle> */
	assert(pending_aborts);
	free_serial(result_argv[2]);
	rename_tmp_holding(sched(dp)->destname, 0);
	deallocate_bandwidth(dp->host->netif, sched(dp)->est_kps);
	adjust_diskspace(dp, DONE);
	delete_diskspace(dp);
	sched(dp)->attempted++;
	enqueue_disk(&runq, dp);	/* we'll try again later */
	dumper->busy = 0;
	dp->host->inprogress -= 1;
	dp->inprogress = 0;
	dp = NULL;
	pending_aborts--;
	while(!empty(stoppedq)) {
	    disk_t *dp2;
	    dp2 = dequeue_disk(&stoppedq);
	    dumper_cmd(sched(dp2)->dumper, CONTINUE, NULL);
	}
	break;

    case BOGUS:
	/* either EOF or garbage from dumper.  Turn it off */
	log_add(L_WARNING, "%s pid %ld is messed up, ignoring it.\n",
	        dumper->name, (long)dumper->pid);
	FD_CLR(fd,&readset);
	aclose(fd);
	dumper->busy = 0;
	dumper->down = 1;	/* mark it down so it isn't used again */
	if(dp) {
	    /* if it was dumping something, zap it and try again */
	    rename_tmp_holding(sched(dp)->destname, 0);
	    deallocate_bandwidth(dp->host->netif, sched(dp)->est_kps);
	    adjust_diskspace(dp, DONE);
	    delete_diskspace(dp);
	    dp->host->inprogress -= 1;
	    dp->inprogress = 0;
	    if(sched(dp)->attempted) {
		log_add(L_FAIL, "%s %s %d [%s died]",
		        dp->host->hostname, dp->name,
		        sched(dp)->level, dumper->name);
	    }
	    else {
		log_add(L_WARNING, "%s died while dumping %s:%s lev %d.",
		        dumper->name, dp->host->hostname, dp->name,
		        sched(dp)->level);
		sched(dp)->attempted++;
		enqueue_disk(&runq, dp);
	    }
	    dp = NULL;
	}
	break;

    default:
	assert(0);
    }

    return;
}

disklist_t read_schedule(waitqp)
disklist_t *waitqp;
{
    sched_t *sp;
    disk_t *dp;
    disklist_t rq;
    int level, line, priority;
    char *dumpdate, *degr_dumpdate;
    int degr_level;
    long time, degr_time;
    unsigned long size, degr_size;
    char *hostname, *diskname, *inpline = NULL;
    char *s;
    int ch;


    rq.head = rq.tail = NULL;

    /* read schedule from stdin */

    for(line = 0; (inpline = agets(stdin)) != NULL; free(inpline)) {
	line++;

	s = inpline;
	ch = *s++;

	skip_whitespace(s, ch);			/* find the host name */
	if(ch == '\0') {
	    error("schedule line %d: syntax error", line);
	    continue;
	}
	hostname = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';

	skip_whitespace(s, ch);			/* find the disk name */
	if(ch == '\0') {
	    error("schedule line %d: syntax error", line);
	    continue;
	}
	diskname = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';

	skip_whitespace(s, ch);			/* find the priority number */
	if(ch == '\0' || sscanf(s - 1, "%d", &priority) != 1) {
	    error("schedule line %d: syntax error", line);
	    continue;
	}
	skip_integer(s, ch);

	skip_whitespace(s, ch);			/* find the level number */
	if(ch == '\0' || sscanf(s - 1, "%d", &level) != 1) {
	    error("schedule line %d: syntax error", line);
	    continue;
	}
	skip_integer(s, ch);

	skip_whitespace(s, ch);			/* find the dump date */
	if(ch == '\0') {
	    error("schedule line %d: syntax error", line);
	    continue;
	}
	dumpdate = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';

	skip_whitespace(s, ch);			/* find the size number */
	if(ch == '\0' || sscanf(s - 1, "%lu", &size) != 1) {
	    error("schedule line %d: syntax error", line);
	    continue;
	}
	skip_integer(s, ch);

	skip_whitespace(s, ch);			/* find the time number */
	if(ch == '\0' || sscanf(s - 1, "%ld", &time) != 1) {
	    error("schedule line %d: syntax error", line);
	    continue;
	}
	skip_integer(s, ch);

	degr_dumpdate = NULL;			/* flag if degr fields found */
	skip_whitespace(s, ch);			/* find the degr level number */
	if(ch != '\0') {
	    if(sscanf(s - 1, "%d", &degr_level) != 1) {
		error("schedule line %d: syntax error", line);
		continue;
	    }
	    skip_integer(s, ch);

	    skip_whitespace(s, ch);		/* find the degr dump date */
	    if(ch == '\0') {
		error("schedule line %d: syntax error", line);
		continue;
	    }
	    degr_dumpdate = s - 1;
	    skip_non_whitespace(s, ch);
	    s[-1] = '\0';

	    skip_whitespace(s, ch);		/* find the degr size number */
	    if(ch == '\0'  || sscanf(s - 1, "%lu", &degr_size) != 1) {
		error("schedule line %d: syntax error", line);
		continue;
	    }
	    skip_integer(s, ch);

	    skip_whitespace(s, ch);		/* find the degr time number */
	    if(ch == '\0' || sscanf(s - 1, "%lu", &degr_time) != 1) {
		error("schedule line %d: syntax error", line);
		continue;
	    }
	    skip_integer(s, ch);
	}

	dp = lookup_disk(hostname, diskname);
	if(dp == NULL) {
	    log_add(L_WARNING,
		    "schedule line %d: %s:%s not in disklist, ignored",
		    line, hostname, diskname);
	    continue;
	}

	sp = (sched_t *) alloc(sizeof(sched_t));
	sp->level    = level;
	sp->dumpdate = stralloc(dumpdate);
	sp->est_size = TAPE_BLOCK_SIZE + size; /* include header */
	sp->est_time = time;
	sp->priority = priority;

	if(degr_dumpdate) {
	    sp->degr_level = degr_level;
	    sp->degr_dumpdate = stralloc(degr_dumpdate);
	    sp->degr_size = TAPE_BLOCK_SIZE + degr_size;
	    sp->degr_time = degr_time;
	} else {
	    sp->degr_level = -1;
	}

	if(time <= 0)
	    sp->est_kps = 10;
	else
	    sp->est_kps = size/time;

	if(sp->degr_level != -1) {
	    if(degr_time <= 0)
		sp->degr_kps = 10;
	    else
		sp->degr_kps = degr_size/degr_time;
	}

	sp->attempted = 0;
	sp->act_size = 0;
	sp->holdp = NULL;
	sp->dumper = NULL;
	sp->timestamp = (time_t)0;

	dp->up = (char *) sp;
	remove_disk(waitqp, dp);
	insert_disk(&rq, dp, sort_by_time);
    }
    amfree(inpline);
    if(line == 0)
	log_add(L_WARNING, "WARNING: got empty schedule from planner");

    return rq;
}

int free_kps(ip)
interface_t *ip;
{
    int res;

    if (ip == (interface_t *)0) {
	interface_t *p;
	int maxusage=0;
	int curusage=0;
	for(p = lookup_interface(NULL); p != NULL; p = p->next) {
	    maxusage += p->maxusage;
	    curusage += p->curusage;
	}
	res = maxusage - curusage;
    }
    else {
	/* XXX - kludge - if we are currently using nothing
	**       on this interface then lie and say he can
	**       have as much as he likes.
	*/
	if (ip->curusage == 0) res = 10000;
	else res = ip->maxusage - ip->curusage;
    }

    return res;
}

void interface_state(time_str)
char *time_str;
{
    interface_t *ip;

    printf("driver: interface-state time %s", time_str);

    for(ip = lookup_interface(NULL); ip != NULL; ip = ip->next) {
	printf(" if %s: free %d", ip->name, free_kps(ip));
    }
    printf("\n");
}

void allocate_bandwidth(ip, kps)
interface_t *ip;
int kps;
{
    ip->curusage += kps;
}

void deallocate_bandwidth(ip, kps)
interface_t *ip;
int kps;
{
    assert(kps <= ip->curusage);
    ip->curusage -= kps;
}

/* ------------ */
unsigned long free_space()
{
    holdingdisk_t *hdp;
    unsigned long total_free;
    long diff;

    total_free = 0L;
    for(hdp = getconf_holdingdisks(); hdp != NULL; hdp = hdp->next) {
	diff = hdp->disksize - holdalloc(hdp)->allocated_space;
	if(diff > 0)
	    total_free += diff;
    }
    return total_free;
}

holdingdisk_t *find_diskspace(size, cur_idle)
unsigned long size;
int *cur_idle;
    /* find holding disk with enough space + minimal # of dumpers */
{
    holdingdisk_t *minp, *hdp;

    minp = NULL;
    for(hdp = getconf_holdingdisks(); hdp != NULL; hdp = hdp->next) {
	if(hdp->chunksize < 0 && size > -hdp->chunksize) {
	    *cur_idle = max(*cur_idle, IDLE_TOO_LARGE);
	}
	/* We add 10 MB per active dumper to give a bit of protection
	 * against under-estimated dump sizes.  */
	else if(holdalloc(hdp)->allocated_space + size +
	   ((holdalloc(hdp)->allocated_dumpers + 1) * 10*1024)
	   <= hdp->disksize) {
	    if(!minp || (holdalloc(minp)->allocated_dumpers >
			 holdalloc(hdp)->allocated_dumpers))
		minp = hdp;
	}
    }
#ifdef HOLD_DEBUG
    printf("find %lu K space: selected %s size %ld allocated %ld dumpers %d\n",
	   size,
	   minp? minp->diskdir : "NO FIT",
	   minp? minp->disksize : (long)-1,
	   minp? holdalloc(minp)->allocated_space : (long)-1,
	   minp? holdalloc(minp)->allocated_dumpers : -1);
#endif
    return minp;
}


void assign_holdingdisk(holdp, diskp)
holdingdisk_t *holdp;
disk_t *diskp;
{
    char *sfn;

#ifdef HOLD_DEBUG
    printf("assigning disk %s:%s size %lu to disk %s\n",
	   diskp->host->hostname, diskp->name,
	   sched(diskp)->est_size, holdp->diskdir);
#endif

    sched(diskp)->holdp = holdp;
    sched(diskp)->act_size = sched(diskp)->est_size;

    sfn = sanitise_filename(diskp->name);
    ap_snprintf(sched(diskp)->destname, sizeof(sched(diskp)->destname),
		"%s/%s/%s.%s.%d",
		holdp->diskdir, datestamp, diskp->host->hostname,
		sfn, sched(diskp)->level);
    amfree(sfn);

    holdalloc(holdp)->allocated_space += sched(diskp)->act_size;
    holdalloc(holdp)->allocated_dumpers += 1;
}

void adjust_diskspace(diskp, tok)
disk_t *diskp;
tok_t tok;
{
    holdingdisk_t *holdp;
    unsigned long kbytes;
    long diff;
    long size;

    size = size_holding_files(sched(diskp)->destname);

    holdp = sched(diskp)->holdp;

#ifdef HOLD_DEBUG
    printf("adjust: before: hdisk %s alloc space %ld dumpers %d\n",
	   holdp->diskdir, holdalloc(holdp)->allocated_space,
	   holdalloc(holdp)->allocated_dumpers);
#endif

    kbytes = size;
    diff = kbytes - sched(diskp)->act_size;
    switch(tok) {
    case DONE:
	/* the dump is done, adjust to actual size and decrease dumpers */

#ifdef HOLD_DEBUG
	printf("adjust: disk %s:%s done, act %lu prev est %lu diff %ld\n",
	       diskp->host->hostname, diskp->name, kbytes,
	       sched(diskp)->act_size, diff);
#endif

	sched(diskp)->act_size = kbytes;
	holdalloc(holdp)->allocated_space += diff;
	holdalloc(holdp)->allocated_dumpers -= 1;
	break;
    case NO_ROOM:
	/* dump still active, but adjust size up iff already > estimate */

#ifdef HOLD_DEBUG
	printf("adjust: disk %s:%s no_room, act %lu prev est %lu diff %ld\n",
	       diskp->host->hostname, diskp->name, kbytes,
	       sched(diskp)->act_size, diff);
#endif
	if(diff > 0) {
	    sched(diskp)->act_size = kbytes;
	    holdalloc(holdp)->allocated_space += diff;
	}
	break;
    default:
	assert(0);
    }
#ifdef HOLD_DEBUG
    printf("adjust: after: hdisk %s alloc space %ld dumpers %d\n",
	   holdp->diskdir, holdalloc(holdp)->allocated_space,
	   holdalloc(holdp)->allocated_dumpers);
#endif
}

void delete_diskspace(diskp)
disk_t *diskp;
{
#ifdef HOLD_DEBUG
    printf("delete: file %s size %lu hdisk %s\n",
	   sched(diskp)->destname, sched(diskp)->act_size,
	   sched(diskp)->holdp->diskdir);
#endif
    holdalloc(sched(diskp)->holdp)->allocated_space -= sched(diskp)->act_size;
    unlink_holding_files(sched(diskp)->destname);
    sched(diskp)->holdp = NULL;
    sched(diskp)->act_size = 0;
    sched(diskp)->destname[0] = '\0';
}

void holdingdisk_state(time_str)
char *time_str;
{
    holdingdisk_t *hdp;
    int dsk;
    long diff;

    printf("driver: hdisk-state time %s", time_str);

    for(hdp = getconf_holdingdisks(), dsk = 0; hdp != NULL; hdp = hdp->next, dsk++) {
	diff = hdp->disksize - holdalloc(hdp)->allocated_space;
	printf(" hdisk %d: free %ld dumpers %d", dsk, diff,
	       holdalloc(hdp)->allocated_dumpers);
    }
    printf("\n");
}

static void update_failed_dump_to_tape(dp)
disk_t *dp;
{
    time_t save_timestamp = sched(dp)->timestamp;
    /* setting timestamp to 0 removes the current level from the
     * database, so that we ensure that it will not be bumped to the
     * next level on the next run.  If we didn't do this, dumpdates or
     * gnutar-lists might have been updated already, and a bumped
     * incremental might be created.  */
    sched(dp)->timestamp = 0;
    update_info_dumper(dp, -1, -1, -1);
    sched(dp)->timestamp = save_timestamp;
}

/* ------------------- */
int dump_to_tape(dp)
disk_t *dp;
{
    dumper_t *dumper;
    int failed = 0;
    int filenum;
    long origsize;
    long dumpsize;
    long dumptime;
    tok_t tok;
    int result_argc;
    char *result_argv[MAX_ARGS+1];

    inside_dump_to_tape = 1;	/* for simulator */

    printf("driver: dumping %s:%s directly to tape\n",
	   dp->host->hostname, dp->name);
    fflush(stdout);

    /* pick a dumper and fail if there are no idle dumpers */

    dumper = idle_dumper();
    if (!dumper) {
	printf("driver: no idle dumpers for %s:%s.\n", 
		dp->host->hostname, dp->name);
	fflush(stdout);
	log_add(L_WARNING, "no idle dumpers for %s:%s.\n",
	        dp->host->hostname, dp->name);
	inside_dump_to_tape = 0;
	return 2;	/* fatal problem */
    }

    /* tell the taper to read from a port number of its choice */

    taper_cmd(PORT_WRITE, dp, NULL, sched(dp)->level, datestamp);
    tok = getresult(taper, 1, &result_argc, result_argv, MAX_ARGS+1);
    if(tok != PORT) {
	printf("driver: did not get PORT from taper for %s:%s\n",
		dp->host->hostname, dp->name);
	fflush(stdout);
	inside_dump_to_tape = 0;
	return 2;	/* fatal problem */
    }
    /* copy port number */
    strncpy(sched(dp)->destname, result_argv[2], sizeof(sched(dp)->destname)-1);
    sched(dp)->destname[sizeof(sched(dp)->destname)-1] = '\0';

    /* tell the dumper to dump to a port */

    dumper_cmd(dumper, PORT_DUMP, dp);
    dp->host->start_t = time(NULL) + 15;

    /* update statistics & print state */

    taper_busy = dumper->busy = 1;
    dp->host->inprogress += 1;
    dp->inprogress = 1;
    sched(dp)->timestamp = time((time_t *)0);
    allocate_bandwidth(dp->host->netif, sched(dp)->est_kps);
    idle_reason = 0;

    short_dump_state();

    /* wait for result from dumper */

    tok = getresult(dumper->outfd, 1, &result_argc, result_argv, MAX_ARGS+1);

    if(tok != BOGUS)
	free_serial(result_argv[2]);

    switch(tok) {
    case BOGUS:
	/* either eof or garbage from dumper */
	log_add(L_WARNING, "%s pid %ld is messed up, ignoring it.\n",
	        dumper->name, (long)dumper->pid);
	dumper->down = 1;	/* mark it down so it isn't used again */
	failed = 1;	/* dump failed, must still finish up with taper */
	break;

    case DONE: /* DONE <handle> <origsize> <dumpsize> <dumptime> <err str> */
	/* everything went fine */
	origsize = (long)atof(result_argv[3]);
	dumpsize = (long)atof(result_argv[4]);
	dumptime = (long)atof(result_argv[5]);
	break;

    case NO_ROOM: /* NO-ROOM <handle> */
	dumper_cmd(dumper, ABORT, dp);
	tok = getresult(dumper->outfd, 1, &result_argc, result_argv, MAX_ARGS+1);
	if(tok != BOGUS)
	    free_serial(result_argv[2]);
	assert(tok == ABORT_FINISHED);

    case TRYAGAIN: /* TRY-AGAIN <handle> <err str> */
    default:
	/* dump failed, but we must still finish up with taper */
	failed = 1;	/* problem with dump, possibly nonfatal */
    }

    /*
     * Note that at this point, even if the dump above failed, it may
     * not be a fatal failure if taper below says we can try again.
     * E.g. a dumper failure above may actually be the result of a
     * tape overflow, which in turn causes dump to see "broken pipe",
     * "no space on device", etc., since taper closed the port first.
     */

    tok = getresult(taper, 1, &result_argc, result_argv, MAX_ARGS+1);

    switch(tok) {
    case DONE: /* DONE <handle> <label> <tape file> <err mess> */
	if(result_argc != 5) {
	    error("error [dump to tape DONE result_argc != 5: %d]", result_argc);
	}

	free_serial(result_argv[2]);

	if(failed) break;	/* dump didn't work */

	/* every thing went fine */
	update_info_dumper(dp, origsize, dumpsize, dumptime);
	filenum = atoi(result_argv[4]);
	update_info_taper(dp, result_argv[3], filenum);

	break;

    case TRYAGAIN: /* TRY-AGAIN <handle> <err mess> */
	update_failed_dump_to_tape(dp);
	free_serial(result_argv[2]);
	enqueue_disk(&runq, dp);
	break;


    case TAPE_ERROR: /* TAPE-ERROR <handle> <err mess> */
	update_failed_dump_to_tape(dp);
	free_serial(result_argv[2]);
	/* fall through */

    case BOGUS:
    default:
	failed = 2;	/* fatal problem */
    }

    /* reset statistics & return */

    taper_busy = dumper->busy = 0;
    dp->host->inprogress -= 1;
    dp->inprogress = 0;
    deallocate_bandwidth(dp->host->netif, sched(dp)->est_kps);

    inside_dump_to_tape = 0;
    return failed;
}

int queue_length(q)
disklist_t q;
{
    disk_t *p;
    int len;

    for(len = 0, p = q.head; p != NULL; len++, p = p->next);
    return len;
}


void short_dump_state()
{
    int i, nidle;
    char *wall_time;

    wall_time = walltime_str(curclock());

    printf("driver: state time %s ", wall_time);
    printf("free kps: %d space: %lu taper: ",
	   free_kps((interface_t *)0), free_space());
    if(degraded_mode) printf("DOWN");
    else if(!taper_busy) printf("idle");
    else printf("writing");
    nidle = 0;
    for(i = 0; i < inparallel; i++) if(!dmptable[i].busy) nidle++;
    printf(" idle-dumpers: %d", nidle);
    printf(" qlen tapeq: %d", queue_length(tapeq));
    printf(" runq: %d", queue_length(runq));
    printf(" stoppedq: %d", queue_length(stoppedq));
    printf(" wakeup: %d", (int)sleep_time.tv_sec);
    printf(" driver-idle: %s\n", idle_strings[idle_reason]);
    interface_state(wall_time);
    holdingdisk_state(wall_time);
    fflush(stdout);
}

void dump_state(str)
char *str;
{
    int i;
    disk_t *dp;

    printf("================\n");
    printf("driver state at time %s: %s\n", walltime_str(curclock()), str);
    printf("free kps: %d, space: %lu\n", free_kps((interface_t *)0), free_space());
    if(degraded_mode) printf("taper: DOWN\n");
    else if(!taper_busy) printf("taper: idle\n");
    else printf("taper: writing %s:%s.%d est size %lu\n",
		taper_disk->host->hostname, taper_disk->name,
		sched(taper_disk)->level,
		sched(taper_disk)->est_size);
    for(i = 0; i < inparallel; i++) {
	dp = dmptable[i].dp;
	if(!dmptable[i].busy)
	  printf("%s: idle\n", dmptable[i].name);
	else
	  printf("%s: dumping %s:%s.%d est kps %d size %lu time %ld\n",
		dmptable[i].name, dp->host->hostname, dp->name, sched(dp)->level,
		sched(dp)->est_kps, sched(dp)->est_size, sched(dp)->est_time);
    }
    dump_queue("TAPE", tapeq, 5, stdout);
    dump_queue("STOP", stoppedq, 5, stdout);
    dump_queue("RUN ", runq, 5, stdout);
    printf("================\n");
    fflush(stdout);
}
