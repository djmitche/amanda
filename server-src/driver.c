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
 * $Id: driver.c,v 1.124 2002/04/13 01:51:48 jrjackson Exp $
 *
 * controlling process for the Amanda backup system
 */

/*
 * XXX possibly modify tape queue to be cognizant of how much room is left on
 *     tape.  Probably not effective though, should do this in planner.
 */

#define HOLD_DEBUG

#include "amanda.h"
#include "clock.h"
#include "conffile.h"
#include "diskfile.h"
#include "event.h"
#include "holding.h"
#include "infofile.h"
#include "logfile.h"
#include "statfs.h"
#include "version.h"
#include "driverio.h"
#include "server_util.h"

static disklist_t waitq, runq, tapeq, roomq;
static int pending_aborts;
static int use_lffo;
static disk_t *taper_disk;
static int degraded_mode;
static unsigned long reserved_space;
static unsigned long total_disksize;
static char *dumper_program;
static char *chunker_program;
static int  inparallel;
static int nodump = 0;
static time_t sleep_time;
static int idle_reason;
static char *datestamp;

static void allocate_bandwidth P((interface_t *ip, int kps));
static int assign_holdingdisk P((assignedhd_t **holdp, disk_t *diskp));
static void adjust_diskspace P((disk_t *diskp, cmd_t cmd));
static void delete_diskspace P((disk_t *diskp));
static assignedhd_t **build_diskspace P((char *destname));
static int client_constrained P((disk_t *dp));
static void deallocate_bandwidth P((interface_t *ip, int kps));
static void dump_schedule P((disklist_t *qp, char *str));
static int dump_to_tape P((disk_t *dp));
static assignedhd_t **find_diskspace P((unsigned long size, int *cur_idle,
					assignedhd_t *preferred));
static int free_kps P((interface_t *ip));
static unsigned long free_space P((void));
static void dumper_result P((disk_t *dp));
static void handle_dumper_result P((void *));
static void handle_chunker_result P((void *));
static void handle_idle_wait P((void *));
static void handle_taper_result P((void *));
static void holdingdisk_state P((char *time_str));
static dumper_t *idle_dumper P((void));
static void interface_state P((char *time_str));
static int num_busy_dumpers P((void));
static int queue_length P((disklist_t q));
static disklist_t read_flush P((void));
static disklist_t read_schedule P((disklist_t *waitqp));
static void short_dump_state P((void));
static int sort_by_size_reversed P((disk_t *a, disk_t *b));
static void start_degraded_mode P((disklist_t *queuep));
static void start_some_dumps P((dumper_t *dumper, disklist_t *rq));
static void continue_dumps();
static void taper_queuedisk P((disk_t *));
static void update_failed_dump_to_tape P((disk_t *));
#if 0
static void dump_state P((const char *str));
#endif
int main P((int main_argc, char **main_argv));

static const char *idle_strings[] = {
#define NOT_IDLE		0
    "not-idle",
#define IDLE_NO_DUMPERS		1
    "no-dumpers",
#define IDLE_START_WAIT		2
    "start-wait",
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

char *config_name = NULL;
char *config_dir = NULL;

int
main(main_argc, main_argv)
     int main_argc;
     char **main_argv;
{
    disklist_t origq;
    disk_t *diskp;
    int fd, dsk;
    dumper_t *dumper;
    char *newdir = NULL;
    generic_fs_stats_t fs;
    holdingdisk_t *hdp;
    unsigned long malloc_hist_1, malloc_size_1;
    unsigned long malloc_hist_2, malloc_size_2;
    unsigned long reserve = 100;
    char *conffile;
    char *conf_diskfile;
    cmd_t cmd;
    int result_argc;
    char *result_argv[MAX_ARGS+1];
    char *taper_program;
    amwait_t retstat;

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

    signal(SIGPIPE, SIG_IGN);

    malloc_size_1 = malloc_inuse(&malloc_hist_1);

    erroutput_type = (ERR_AMANDALOG|ERR_INTERACTIVE);
    set_logerror(logerror);

    startclock();

    printf("%s: pid %ld executable %s version %s\n",
	   get_pname(), (long) getpid(), main_argv[0], version());

    if (main_argc > 1) {
	config_name = stralloc(main_argv[1]);
	config_dir = vstralloc(CONFIG_DIR, "/", config_name, "/", NULL);
	if(main_argc > 2) {
	    if(strncmp(main_argv[2], "nodump", 6) == 0) {
		nodump = 1;
	    }
	}

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

    conffile = stralloc2(config_dir, CONFFILE_NAME);
    if(read_conffile(conffile)) {
	error("errors processing config file \"%s\"", conffile);
    }
    amfree(conffile);

    amfree(datestamp);
    datestamp = construct_datestamp(NULL);
    log_add(L_START,"date %s", datestamp);

    taper_program = vstralloc(libexecdir, "/", "taper", versionsuffix(), NULL);
    dumper_program = vstralloc(libexecdir, "/", "dumper", versionsuffix(),
			       NULL);
    chunker_program = vstralloc(libexecdir, "/", "chunker", versionsuffix(),
			       NULL);

    /* taper takes a while to get going, so start it up right away */

    init_driverio();
    startup_tape_process(taper_program);
    taper_cmd(START_TAPER, datestamp, NULL, 0, NULL);

    /* start initializing: read in databases */

    conf_diskfile = getconf_str(CNF_DISKFILE);
    if (*conf_diskfile == '/') {
	conf_diskfile = stralloc(conf_diskfile);
    } else {
	conf_diskfile = stralloc2(config_dir, conf_diskfile);
    }
    if (read_diskfile(conf_diskfile, &origq) < 0)
	error("could not load disklist \"%s\"", conf_diskfile);
    amfree(conf_diskfile);

    /* set up any configuration-dependent variables */

    inparallel	= getconf_int(CNF_INPARALLEL);
    use_lffo	= 1;

    reserve = getconf_int(CNF_RESERVE);

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
	if (mkpdir(newdir, 0770, (uid_t)-1, (gid_t)-1) != 0) {
	    log_add(L_WARNING, "WARNING: could not create parents of %s: %s",
		    newdir, strerror(errno));
	    hdp->disksize = 0L;
	} else if (mkdir(newdir, 0770) != 0 && errno != EEXIST) {
	    log_add(L_WARNING, "WARNING: could not create %s: %s",
		    newdir, strerror(errno));
	    hdp->disksize = 0L;
	} else if (stat(newdir, &stat_hdp) == -1) {
	    log_add(L_WARNING, "WARNING: could not stat %s: %s",
		    newdir, strerror(errno));
	    hdp->disksize = 0L;
	} else {
	    if (!S_ISDIR((stat_hdp.st_mode))) {
		log_add(L_WARNING, "WARNING: %s is not a directory",
			newdir);
		hdp->disksize = 0L;
	    } else if (access(newdir,W_OK) != 0) {
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
    if(!nodump) startup_dump_processes(dumper_program, inparallel);

    /*
     * Read schedule from stdin.  Usually, this is a pipe from planner,
     * so the effect is that we wait here for the planner to
     * finish, but meanwhile the taper is rewinding the tape, reading
     * the label, checking it, writing a new label and all that jazz
     * in parallel with the planner.
     */

    waitq = origq;
    tapeq = read_flush();
    if(!nodump) runq = read_schedule(&waitq);
    else runq.head = NULL;

    roomq.head = roomq.tail = NULL;

    log_add(L_STATS, "startup time %s", walltime_str(curclock()));

    printf("driver: start time %s inparallel %d bandwidth %d diskspace %lu",
	   walltime_str(curclock()), inparallel, free_kps((interface_t *)0),
	   free_space());
    printf(" dir %s datestamp %s driver: drain-ends tapeq %s big-dumpers %s\n",
	   "OBSOLETE", datestamp,  use_lffo? "LFFO" : "FIFO", getconf_str(CNF_DUMPORDER));
    fflush(stdout);

    /* ok, planner is done, now lets see if the tape is ready */

    cmd = getresult(taper, 1, &result_argc, result_argv, MAX_ARGS+1);

    if(cmd != TAPER_OK) {
	/* no tape, go into degraded mode: dump to holding disk */
	start_degraded_mode(&runq);
    }
    else if(empty(tapeq)) {
	taper_busy = 0;
	taper_disk = NULL;
	assert(taper_ev_read == NULL);
    }
    else {
	disk_t *dp = dequeue_disk(&tapeq);
	assert(taper_ev_read == NULL);
	taper_ev_read = event_register(taper, EV_READFD,
	    handle_taper_result, NULL);
	taper_disk = dp;
	taper_busy = 1;
	taper_cmd(FILE_WRITE, dp, sched(dp)->destname, sched(dp)->level,
	    sched(dp)->datestamp);
    }

    /*
     * Assume we'll schedule this dumper, and default to idle no-dumpers.
     */
    for (dumper = dmptable; dumper < dmptable+inparallel; dumper++) {
	start_some_dumps(dumper, &runq);
	event_loop(1);
    }

    short_dump_state();
    event_loop(0);

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
		log_add(L_FAIL, "%s %s %s %d [dump to tape failed]",
		        diskp->host->hostname,
			diskp->name,
			sched(diskp)->datestamp,
			sched(diskp)->level);
	}
	else
	    log_add(L_FAIL, "%s %s %s %d [%s]",
		diskp->host->hostname, diskp->name, sched(diskp)->datestamp,
		sched(diskp)->level,
		diskp->no_hold ?
		    "can't dump no-hold disk in degraded mode" :
		    "no more holding disk space");
    }

    short_dump_state();				/* for amstatus */

    printf("driver: QUITTING time %s telling children to quit\n",
           walltime_str(curclock()));
    fflush(stdout);

    if(!nodump) {
	for(dumper = dmptable; dumper < dmptable + inparallel; dumper++) {
	    dumper_cmd(dumper, QUIT, NULL);
	}
    }

    if(taper >= 0)
	taper_cmd(QUIT, NULL, NULL, 0, NULL);

    /* wait for all to die */

    while(1) {
	char number[NUM_STR_SIZE];
	pid_t pid;
	char *who;
	char *what;
	int code=0;

	if((pid = wait(&retstat)) == -1) {
	    if(errno == EINTR) continue;
	    else break;
	}
	what = NULL;
	if(! WIFEXITED(retstat)) {
	    what = "signal";
	    code = WTERMSIG(retstat);
	} else if(WEXITSTATUS(retstat) != 0) {
	    what = "code";
	    code = WEXITSTATUS(retstat);
	}
	who = NULL;
	for(dumper = dmptable; dumper < dmptable + inparallel; dumper++) {
	    if(pid == dumper->pid) {
		who = stralloc(dumper->name);
		break;
	    }
	}
	if(who == NULL && pid == taper_pid) {
	    who = stralloc("taper");
	}
	if(what != NULL && who == NULL) {
	    snprintf(number, sizeof(number), "%ld", (long)pid);
	    who = stralloc2("unknown pid ", number);
	}
	if(who && what) {
	    log_add(L_WARNING, "%s exited with %s %d\n", who, what, code);
	    printf("driver: %s exited with %s %d\n", who, what, code);
	}
	amfree(who);
    }

    for(dumper = dmptable; dumper < dmptable + inparallel; dumper++) {
	amfree(dumper->name);
    }

    for(hdp = getconf_holdingdisks(); hdp != NULL; hdp = hdp->next) {
	cleanup_holdingdisk(hdp->diskdir, 0);
	amfree(hdp->up);
    }
    amfree(newdir);

    printf("driver: FINISHED time %s\n", walltime_str(curclock()));
    fflush(stdout);
    log_add(L_FINISH,"date %s time %s", datestamp, walltime_str(curclock()));
    amfree(datestamp);

    amfree(dumper_program);
    amfree(taper_program);
    amfree(config_dir);
    amfree(config_name);

    malloc_size_2 = malloc_inuse(&malloc_hist_2);

    if(malloc_size_1 != malloc_size_2) {
	malloc_list(fileno(stderr), malloc_hist_1, malloc_hist_2);
    }

    return 0;
}

static int
client_constrained(dp)
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

static void
start_some_dumps(dumper, rq)
    dumper_t *dumper;
    disklist_t *rq;
{
    int cur_idle;
    disk_t *diskp, *delayed_diskp, *diskp_accept;
    assignedhd_t **holdp=NULL, **holdp_accept;
    const time_t now = time(NULL);
    cmd_t cmd;
    int result_argc;
    char *result_argv[MAX_ARGS+1];
    chunker_t *chunker;
    dumper_t *adumper;
    int busy_dumpers = 0;

    assert(dumper->busy == 0);	/* we better not have been grabbed */

    if (dumper->ev_read != NULL) {
	event_release(dumper->ev_read);
	dumper->ev_read = NULL;
    }
/*
    if (empty(*rq)) {
	idle_reason = NOT_IDLE;
	return;
    }
*/
    idle_reason = IDLE_NO_DUMPERS;
    sleep_time = 0;

    for( adumper = dmptable; adumper < dmptable + inparallel; adumper++) {
	if( adumper->busy )
	    busy_dumpers++;
    }
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

    diskp_accept = NULL;
    holdp_accept = NULL;
    delayed_diskp = NULL;

    cur_idle = NOT_IDLE;

    for(diskp = rq->head; diskp != NULL; diskp = diskp->next) {
	assert(diskp->host != NULL && sched(diskp) != NULL);

	/* round estimate to next multiple of DISK_BLOCK_KB */
	sched(diskp)->est_size = am_round(sched(diskp)->est_size,
					  DISK_BLOCK_KB);

	if (diskp->host->start_t > now) {
	    cur_idle = max(cur_idle, IDLE_START_WAIT);
	    if (delayed_diskp == NULL || sleep_time > diskp->host->start_t) {
		delayed_diskp = diskp;
		sleep_time = diskp->host->start_t;
	    }
	} else if(diskp->start_t > now) {
	    cur_idle = max(cur_idle, IDLE_START_WAIT);
	    if (delayed_diskp == NULL || sleep_time > diskp->start_t) {
		delayed_diskp = diskp;
		sleep_time = diskp->start_t;
	    }
	} else if (diskp->host->netif->curusage > 0 &&
		   sched(diskp)->est_kps > free_kps(diskp->host->netif)) {
	    cur_idle = max(cur_idle, IDLE_NO_BANDWIDTH);
	} else if(sched(diskp)->no_space) {
	    cur_idle = max(cur_idle, IDLE_NO_DISKSPACE);
	} else if ((holdp =
	    find_diskspace(sched(diskp)->est_size,&cur_idle,NULL)) == NULL) {
	    cur_idle = max(cur_idle, IDLE_NO_DISKSPACE);
	} else if (diskp->no_hold) {
	    free_assignedhd(holdp);
	    cur_idle = max(cur_idle, IDLE_NO_HOLD);
	} else if (client_constrained(diskp)) {
	    free_assignedhd(holdp);
	    cur_idle = max(cur_idle, IDLE_CLIENT_CONSTRAINED);
	} else {

	    /* disk fits, dump it */
	    int accept = !diskp_accept;
	    if(!accept) {
		char dumptype;
		char *dumporder = getconf_str(CNF_DUMPORDER);
		if(strlen(dumporder) > (busy_dumpers)) {
		    dumptype = dumporder[busy_dumpers];
		}
		else {
		    if(busy_dumpers < 3)
			dumptype = 's';
		    else
			dumptype = 'S';
		}
		switch(dumptype) {
		  case 's': accept = (sched(diskp)->est_size < sched(diskp_accept)->est_size);
			    break;
		  case 'S': accept = (sched(diskp)->est_size > sched(diskp_accept)->est_size);
			    break;
		  case 't': accept = (sched(diskp)->est_time < sched(diskp_accept)->est_time);
			    break;
		  case 'T': accept = (sched(diskp)->est_time > sched(diskp_accept)->est_time);
			    break;
		  case 'b': accept = (sched(diskp)->est_kps < sched(diskp_accept)->est_kps);
			    break;
		  case 'B': accept = (sched(diskp)->est_kps > sched(diskp_accept)->est_kps);
			    break;
		  default:  log_add(L_WARNING, "Unknown dumporder character \'%c\', using 's'.\n",
				    dumptype);
			    accept = (sched(diskp)->est_size < sched(diskp_accept)->est_size);
			    break;
		}
	    }
	    if(accept) {
		if( !diskp_accept || !degraded_mode || diskp->priority >= diskp_accept->priority) {
		    if(holdp_accept) free_assignedhd(holdp_accept);
		    diskp_accept = diskp;
		    holdp_accept = holdp;
		}
	    }
	}
    }

    diskp = diskp_accept;
    holdp = holdp_accept;

    /*
     * If we have no disk at this point, and there are disks that
     * are delayed, then schedule a time event to call this dumper
     * with the disk with the shortest delay.
     */
    if (diskp == NULL && delayed_diskp != NULL) {
	assert(dumper->ev_wait == NULL);
	assert(sleep_time > now);
	sleep_time -= now;
	dumper->ev_wait = event_register(sleep_time, EV_TIME,
	    handle_idle_wait, dumper);
    } else if (diskp != NULL && cur_idle == NOT_IDLE) {
	sched(diskp)->act_size = 0;
	allocate_bandwidth(diskp->host->netif, sched(diskp)->est_kps);
	sched(diskp)->activehd = assign_holdingdisk(holdp, diskp);
	sched(diskp)->destname = newstralloc(sched(diskp)->destname,
					     sched(diskp)->holdp[0]->destname);
	diskp->host->inprogress++;	/* host is now busy */
	diskp->inprogress = 1;
	sched(diskp)->dumper = dumper;
	sched(diskp)->timestamp = now;

	dumper->ev_read = event_register(dumper->fd, EV_READFD,
	    handle_dumper_result, dumper);
	dumper->busy = 1;		/* dumper is now busy */
	dumper->dp = diskp;		/* link disk to dumper */
	remove_disk(rq, diskp);		/* take it off the run queue */

	sched(diskp)->origsize = -1;
	sched(diskp)->dumpsize = -1;
	sched(diskp)->dumptime = -1;
	sched(diskp)->tapetime = -1;
	chunker = dumper->chunker;
	chunker->result = LAST_TOK;
	dumper->result = LAST_TOK;
	startup_chunk_process(chunker,chunker_program);
	chunker->dumper = dumper;
	chunker_cmd(chunker, PORT_WRITE, diskp);
	cmd = getresult(chunker->fd, 1, &result_argc, result_argv, MAX_ARGS+1);
	if(cmd != PORT) {
	    printf("driver: did not get PORT from %s for %s:%s\n",
		    chunker->name, diskp->host->hostname, diskp->name);
	    fflush(stdout);
	    return ;	/* fatal problem */
	}
	chunker->ev_read = event_register(chunker->fd, EV_READFD,
		handle_chunker_result, chunker);
	dumper->output_port = atoi(result_argv[2]);

	dumper_cmd(dumper, PORT_DUMP, diskp);

	diskp->host->start_t = now + 15;
    } else if (/* cur_idle != NOT_IDLE && */
	(num_busy_dumpers() > 0 || taper_busy)) {
	/*
	 * We are constrained.  Wait until we aren't.
	 * If no dumpers/taper are busy, then we'll never be unconstrained,
	 * so just drop off.
	 */
	assert(dumper->ev_wait == NULL);
	dumper->ev_wait = event_register((event_id_t)handle_idle_wait,
	    EV_WAIT, handle_idle_wait, dumper);
	fprintf(stderr,"EV_WAIT:\n");
    }
    idle_reason = max(idle_reason, cur_idle);
}

/*
 * This gets called when a dumper is delayed for some reason.  It may
 * be because a disk has a delayed start, or amanda is constrained
 * by network or disk limits.
 */
static void
handle_idle_wait(cookie)
    void *cookie;
{
    dumper_t *dumper = cookie;

    short_dump_state();

    assert(dumper != NULL);
    event_release(dumper->ev_wait);
    dumper->ev_wait = NULL;
    start_some_dumps(dumper, &runq);
}

static int
sort_by_size_reversed(a, b)
    disk_t *a, *b;
{
    long diff;

    if ((diff = strcmp(sched(a)->datestamp, sched(b)->datestamp)) < 0) {
	return -1;
    } else if (diff >0) {
	return 1;
    }
    else if ((diff = sched(a)->est_size - sched(b)->est_size) < 0) {
	return -1;
    } else if (diff > 0) {
	return 1;
    } else {
	return 0;
    }
}

static void
dump_schedule(qp, str)
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

static void
start_degraded_mode(queuep)
    disklist_t *queuep;
{
    disk_t *dp;
    disklist_t newq;
    unsigned long est_full_size;

    if (taper_ev_read != NULL) {
	event_release(taper_ev_read);
	taper_ev_read = NULL;
    }

    newq.head = newq.tail = 0;

    dump_schedule(queuep, "before start degraded mode");

    est_full_size = 0;
    while(!empty(*queuep)) {
	dp = dequeue_disk(queuep);

	if(sched(dp)->level != 0)
	    /* go ahead and do the disk as-is */
	    enqueue_disk(&newq, dp);
	else {
	    if (reserved_space + est_full_size + sched(dp)->est_size
		<= total_disksize) {
		enqueue_disk(&newq, dp);
		est_full_size += sched(dp)->est_size;
	    }
	    else if(sched(dp)->degr_level != -1) {
		sched(dp)->level = sched(dp)->degr_level;
		sched(dp)->dumpdate = sched(dp)->degr_dumpdate;
		sched(dp)->est_size = sched(dp)->degr_size;
		sched(dp)->est_time = sched(dp)->degr_time;
		sched(dp)->est_kps  = sched(dp)->degr_kps;
		enqueue_disk(&newq, dp);
	    }
	    else {
		log_add(L_FAIL,"%s %s %s %d [can't switch to incremental dump]",
		        dp->host->hostname, dp->name, sched(dp)->datestamp,
			sched(dp)->level);
	    }
	}
    }

    *queuep = newq;
    degraded_mode = 1;

    dump_schedule(queuep, "after start degraded mode");
}


static void continue_dumps()
{
    disk_t *dp, *ndp;
    assignedhd_t **h;
    int active_dumpers=0, busy_dumpers=0, i;
    dumper_t *dumper;

    /* First we try to grant diskspace to some dumps waiting for it. */
    for( dp = roomq.head; dp; dp = ndp ) {
	ndp = dp->next;
	/* find last holdingdisk used by this dump */
	for( i = 0, h = sched(dp)->holdp; h[i+1]; i++ );
	/* find more space */
	h = find_diskspace( sched(dp)->est_size - sched(dp)->act_size,
			    &active_dumpers, h[i] );
	if( h ) {
	    for(dumper = dmptable; dumper < dmptable + inparallel &&
				   dumper->dp != dp; dumper++);
	    assert( dumper < dmptable + inparallel );
	    sched(dp)->activehd = assign_holdingdisk( h, dp );
	    dumper_cmd( dumper, CONTINUE, dp );
	    amfree(h);
	    remove_disk( &roomq, dp );
	}
    }

    /* So for some disks there is less holding diskspace available than
     * was asked for. Possible reasons are
     * a) diskspace has been allocated for other dumps which are
     *    still running or already being written to tape
     * b) all other dumps have been suspended due to lack of diskspace
     * c) this dump doesn't fit on all the holding disks
     * Case a) is not a problem. We just wait for the diskspace to
     * be freed by moving the current disk to a queue.
     * If case b) occurs, we have a deadlock situation. We select
     * a dump from the queue to be aborted and abort it. It will
     * be retried later dumping to disk.
     * If case c) is detected, the dump is aborted. Next time
     * it will be dumped directly to tape. Actually, case c is a special
     * manifestation of case b) where only one dumper is busy.
     */
    for( dp=NULL, dumper = dmptable; dumper < dmptable + inparallel; dumper++) {
	if( dumper->busy ) {
	    busy_dumpers++;
	    if( !find_disk(&roomq, dumper->dp) ) {
		active_dumpers++;
	    } else if( !dp || 
		       sched(dp)->est_size > sched(dumper->dp)->est_size ) {
		dp = dumper->dp;
	    }
	}
    }
    if( !active_dumpers && busy_dumpers > 0 && !taper_busy && empty(tapeq) &&
	pending_aborts == 0 ) { /* not case a */
	if( busy_dumpers == 1 ) { /* case c */
	    sched(dp)->no_space = 1;
	}
	/* case b */
	/* At this time, dp points to the dump with the smallest est_size.
	 * We abort that dump, hopefully not wasting too much time retrying it.
	 */
	remove_disk( &roomq, dp );
	chunker_cmd( sched(dp)->dumper->chunker, ABORT, NULL );
	dumper_cmd( sched(dp)->dumper, ABORT, NULL );
	pending_aborts++;
    }
}


static void
handle_taper_result(cookie)
    void *cookie;
{
    disk_t *dp;
    int filenum;
    cmd_t cmd;
    int result_argc;
    char *result_argv[MAX_ARGS+1];

    assert(cookie == NULL);

    do {

	short_dump_state();

	cmd = getresult(taper, 1, &result_argc, result_argv, MAX_ARGS+1);

	switch(cmd) {

	case PARTIAL:
	case DONE:	/* DONE <handle> <label> <tape file> <err mess> */
	    if(result_argc != 5) {
		error("error: [taper DONE result_argc != 5: %d", result_argc);
	    }

	    dp = serial2disk(result_argv[2]);
	    free_serial(result_argv[2]);

	    filenum = atoi(result_argv[4]);
	    if(cmd == DONE) {
		update_info_taper(dp, result_argv[3], filenum, sched(dp)->level);
	    }

	    delete_diskspace(dp);

	    printf("driver: finished-cmd time %s taper wrote %s:%s\n",
		   walltime_str(curclock()), dp->host->hostname, dp->name);
	    fflush(stdout);

	    amfree(sched(dp)->destname);
	    amfree(sched(dp)->dumpdate);
	    amfree(dp->up);

	    if(empty(tapeq)) {
		taper_busy = 0;
		taper_disk = NULL;
		if(taper_ev_read != NULL) {
		    event_release(taper_ev_read);
		    taper_ev_read = NULL;
		}
	    }
	    else {
		dp = dequeue_disk(&tapeq);
		taper_disk = dp;
		taper_cmd(FILE_WRITE, dp, sched(dp)->destname, sched(dp)->level,
			  sched(dp)->datestamp);
	    }

	    /* continue with those dumps waiting for diskspace */
	    continue_dumps();
	    break;

	case TRYAGAIN:  /* TRY-AGAIN <handle> <err mess> */
	    if (result_argc < 2) {
		error("error [taper TRYAGAIN result_argc < 2: %d]",
		      result_argc);
	    }
	    dp = serial2disk(result_argv[2]);
	    free_serial(result_argv[2]);
	    printf("driver: taper-tryagain time %s disk %s:%s\n",
		   walltime_str(curclock()), dp->host->hostname, dp->name);
	    fflush(stdout);

	    /* re-insert into taper queue */

	    if(sched(dp)->attempted) {
		log_add(L_FAIL, "%s %s %s %d [too many taper retries]",
	    	    dp->host->hostname, dp->name, sched(dp)->datestamp,
		    sched(dp)->level);
		continue_dumps();
	    }
	    else {
		sched(dp)->attempted++;
		taper_queuedisk(dp);
	    }

	    /* run next thing from queue */

	    if(empty(tapeq)) {
		taper_busy = 0;
		taper_disk = NULL;
		if(taper_ev_read != NULL) {
		    event_release(taper_ev_read);
		    taper_ev_read = NULL;
		}
	    }
	    else {
		dp = dequeue_disk(&tapeq);
		taper_disk = dp;
		taper_cmd(FILE_WRITE, dp, sched(dp)->destname,
			  sched(dp)->level, sched(dp)->datestamp);
	    }
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
	     * checks. If there are dumps waiting for diskspace to be freed,
	     * cancel one.
	     */
	    if(!nodump) {
		log_add(L_WARNING,
			"going into degraded mode because of tape error.");
		start_degraded_mode(&runq);
	    }
	    tapeq.head = tapeq.tail = NULL;
	    taper_busy = 0;
	    taper_disk = NULL;
	    if(taper_ev_read != NULL) {
		event_release(taper_ev_read);
		taper_ev_read = NULL;
	    }
	    if(cmd != TAPE_ERROR) aclose(taper);
	    continue_dumps();
	    break;
	default:
	    error("driver received unexpected token (%s) from taper",
		  cmdstr[cmd]);
	}
	/*
	 * Wakeup any dumpers that are sleeping because of network
	 * or disk constraints.
	 */
	event_wakeup((event_id_t)handle_idle_wait);

    } while(areads_dataready(taper));
}

static dumper_t *
idle_dumper()
{
    dumper_t *dumper;

    for(dumper = dmptable; dumper < dmptable+inparallel; dumper++)
	if(!dumper->busy && !dumper->down) return dumper;

    return NULL;
}

static int
num_busy_dumpers()
{
    dumper_t *dumper;
    int n;

    n = 0;
    for(dumper = dmptable; dumper < dmptable+inparallel; dumper++)
	if(dumper->busy) n += 1;

    return n;
}


static void
dumper_result(dp)
    disk_t *dp;
{
    dumper_t *dumper;
    chunker_t *chunker;
    assignedhd_t **h=NULL;
    int activehd, i, dummy;
    long size;
    int is_partial;

    dumper = sched(dp)->dumper;
    chunker = dumper->chunker;

    h = sched(dp)->holdp;
    activehd = sched(dp)->activehd;

    if(dumper->result == DONE && chunker->result == DONE) {
	update_info_dumper(dp, sched(dp)->origsize,
			   sched(dp)->dumpsize, sched(dp)->dumptime);
    }

    deallocate_bandwidth(dp->host->netif, sched(dp)->est_kps);

    is_partial = dumper->result != DONE || chunker->result != DONE;
    rename_tmp_holding(sched(dp)->destname, !is_partial);

    dummy = 0;
    for( i = 0, h = sched(dp)->holdp; i < activehd; i++ ) {
	dummy += h[i]->used;
    }

    size = size_holding_files(sched(dp)->destname);
    h[activehd]->used = size - dummy;
    holdalloc(h[activehd]->disk)->allocated_dumpers--;
    adjust_diskspace(dp, DONE);

    sched(dp)->attempted += 1;

    if((dumper->result != DONE || chunker->result != DONE) &&
       sched(dp)->attempted <= 1) {
	delete_diskspace(dp);
	enqueue_disk(&runq, dp);
    }
    else if(size > DISK_BLOCK_KB) {
	taper_queuedisk(dp);
    }
    else {
	delete_diskspace(dp);
    }

    dumper->busy = 0;
    dp->host->inprogress -= 1;
    dp->inprogress = 0;

    waitpid(chunker->pid, NULL, 0 );
    aclose(chunker->fd);
    chunker->fd = -1;
    chunker->down = 1;
    
    dp = NULL;
    continue_dumps();
    if(!dumper->down) {
	start_some_dumps(dumper, &runq);
    }

    /*
     * Wakeup any dumpers that are sleeping because of network
     * or disk constraints.
     */
    event_wakeup((event_id_t)handle_idle_wait);
}


static void
handle_dumper_result(cookie)
    void *cookie;
{
    /*static int pending_aborts = 0;*/
    dumper_t *dumper = cookie;
    disk_t *dp, *sdp;
    cmd_t cmd;
    int result_argc;
    char *result_argv[MAX_ARGS+1];

    assert(dumper != NULL);
    dp = dumper->dp;
    assert(dp != NULL && sched(dp) != NULL);

    do {

	short_dump_state();

	cmd = getresult(dumper->fd, 1, &result_argc, result_argv, MAX_ARGS+1);

	if(cmd != BOGUS) {
	    /* result_argv[2] always contains the serial number */
	    sdp = serial2disk(result_argv[2]);
	    assert(sdp == dp);
	}

	switch(cmd) {

	case DONE: /* DONE <handle> <origsize> <dumpsize> <dumptime> <errstr> */
	    if(result_argc != 6) {
		error("error [dumper DONE result_argc != 6: %d]", result_argc);
	    }

	    free_serial(result_argv[2]);

	    sched(dp)->origsize = (long)atof(result_argv[3]);
	    sched(dp)->dumptime = (long)atof(result_argv[5]);

	    printf("driver: finished-cmd time %s %s dumped %s:%s\n",
		   walltime_str(curclock()), dumper->name,
		   dp->host->hostname, dp->name);
	    fflush(stdout);

	    dumper->result = cmd;

	    break;

	case TRYAGAIN: /* TRY-AGAIN <handle> <errstr> */
	    /*
	     * Requeue this disk, and fall through to the FAILED
	     * case for cleanup.
	     */
	    if(sched(dp)->attempted) {
		log_add(L_FAIL, "%s %s %s %d [could not connect to %s]",
	    	    dp->host->hostname, dp->name, sched(dp)->datestamp,
	    	    sched(dp)->level, dp->host->hostname);
	    }
	    /* FALLTHROUGH */
	case FAILED: /* FAILED <handle> <errstr> */
	    free_serial(result_argv[2]);
	    dumper->result = cmd;
	    break;

	case ABORT_FINISHED: /* ABORT-FINISHED <handle> */
	    /*
	     * We sent an ABORT from the NO-ROOM case because this dump
	     * wasn't going to fit onto the holding disk.  We now need to
	     * clean up the remains of this image, and try to finish
	     * other dumps that are waiting on disk space.
	     */
	    assert(pending_aborts);
	    free_serial(result_argv[2]);
	    dumper->result = cmd;
	    break;

	case BOGUS:
	    /* either EOF or garbage from dumper.  Turn it off */
	    log_add(L_WARNING, "%s pid %ld is messed up, ignoring it.\n",
		    dumper->name, (long)dumper->pid);
	    event_release(dumper->ev_read);
	    dumper->ev_read = NULL;
	    aclose(dumper->fd);
	    dumper->busy = 0;
	    dumper->down = 1;	/* mark it down so it isn't used again */
	    if(dp) {
		/* if it was dumping something, zap it and try again */
		if(sched(dp)->attempted) {
	    	log_add(L_FAIL, "%s %s %s %d [%s died]",
	    		dp->host->hostname, dp->name, sched(dp)->datestamp,
	    		sched(dp)->level, dumper->name);
		}
		else {
	    	log_add(L_WARNING, "%s died while dumping %s:%s lev %d.",
	    		dumper->name, dp->host->hostname, dp->name,
	    		sched(dp)->level);
		}
	    }
	    dumper->result = cmd;
	    break;

	default:
	    assert(0);
	}
	/* send the dumper result to the chunker */
	if(dumper->chunker->down == 0 && dumper->chunker->fd != -1) {
	    if(cmd == DONE) {
		chunker_cmd(dumper->chunker, cmd, dp);
	    }
	    else {
		chunker_cmd(dumper->chunker, FAILED, dp);
	    }
	}

	if(dumper->result != LAST_TOK && dumper->chunker->result != LAST_TOK)
	    dumper_result(dp);

    } while(areads_dataready(dumper->fd));
}


static void
handle_chunker_result(cookie)
    void *cookie;
{
    /*static int pending_aborts = 0;*/
    chunker_t *chunker = cookie;
    assignedhd_t **h=NULL;
    dumper_t *dumper;
    disk_t *dp, *sdp;
    cmd_t cmd;
    int result_argc;
    char *result_argv[MAX_ARGS+1];
    int dummy;
    int activehd = -1;


    assert(chunker != NULL);
    dumper = chunker->dumper;
    assert(dumper != NULL);
    dp = dumper->dp;
    assert(dp != NULL);
    assert(sched(dp) != NULL);
    assert(sched(dp)->destname != NULL);
    assert(dp != NULL && sched(dp) != NULL && sched(dp)->destname);

    if(dp && sched(dp) && sched(dp)->holdp) {
	h = sched(dp)->holdp;
	activehd = sched(dp)->activehd;
    }

    do {

	short_dump_state();

	cmd = getresult(chunker->fd, 1, &result_argc, result_argv, MAX_ARGS+1);

	if(cmd != BOGUS) {
	    /* result_argv[2] always contains the serial number */
	    sdp = serial2disk(result_argv[2]);
	    assert(sdp == dp);
	}

	switch(cmd) {

	case PARTIAL: /* PARTIAL <handle> <dumpsize> <errstr> */
	case DONE: /* DONE <handle> <dumpsize> <errstr> */
	    if(result_argc != 4) {
		error("error [chunker %s result_argc != 4: %d]", cmdstr[cmd],
		      result_argc);
	    }
	    free_serial(result_argv[2]);

	    sched(dp)->dumpsize = (long)atof(result_argv[3]);

	    printf("driver: finished-cmd time %s %s chunked %s:%s\n",
		   walltime_str(curclock()), chunker->name,
		   dp->host->hostname, dp->name);
	    fflush(stdout);

	    event_release(chunker->ev_read);

	    chunker->result = cmd;

	    break;

	case TRYAGAIN: /* TRY-AGAIN <handle> <errstr> */
	    assert(0);
	    event_release(chunker->ev_read);

	    break;
	case FAILED: /* FAILED <handle> <errstr> */
	    free_serial(result_argv[2]);

	    event_release(chunker->ev_read);

	    chunker->result = cmd;

	    break;

	case NO_ROOM: /* NO-ROOM <handle> <missing_size> */
	    assert( h && activehd >= 0 );
	    h[activehd]->used -= atoi(result_argv[3]);
	    h[activehd]->reserved -= atoi(result_argv[3]);
	    holdalloc(h[activehd]->disk)->allocated_space -= atoi(result_argv[3]);
	    h[activehd]->disk->disksize -= atoi(result_argv[3]);
	    break;

	case RQ_MORE_DISK: /* RQ-MORE-DISK <handle> */
	    assert( h && activehd >= 0 );
	    holdalloc(h[activehd]->disk)->allocated_dumpers--;
	    h[activehd]->used = h[activehd]->reserved;
	    if( h[++activehd] ) { /* There's still some allocated space left.
				   * Tell the dumper about it. */
		sched(dp)->activehd++;
		chunker_cmd( chunker, CONTINUE, dp );
	    } else { /* !h[++activehd] - must allocate more space */
		sched(dp)->act_size = sched(dp)->est_size; /* not quite true */
		sched(dp)->est_size = sched(dp)->act_size * 21 / 20; /* +5% */
		sched(dp)->est_size = am_round(sched(dp)->est_size, DISK_BLOCK_KB);
		h = find_diskspace( sched(dp)->est_size - sched(dp)->act_size,
				    &dummy,
				    h[activehd-1] );
		if( !h ) {
		    /* No diskspace available. The reason for this will be
		     * determined in continue_dumps(). */
		    enqueue_disk( &roomq, dp );
		    continue_dumps();
		} else {
		    /* OK, allocate space for disk and have chunker continue */
		    sched(dp)->activehd = assign_holdingdisk( h, dp );
		    chunker_cmd( chunker, CONTINUE, dp );
		    amfree(h);
		}
	    }
	    break;

	case ABORT_FINISHED: /* ABORT-FINISHED <handle> */
	    /*
	     * We sent an ABORT from the NO-ROOM case because this dump
	     * wasn't going to fit onto the holding disk.  We now need to
	     * clean up the remains of this image, and try to finish
	     * other dumps that are waiting on disk space.
	     */
	    /*assert(pending_aborts);*/

	    free_serial(result_argv[2]);

	    event_release(chunker->ev_read);

	    chunker->result = cmd;

	    break;

	case BOGUS:
	    /* either EOF or garbage from chunker.  Turn it off */
	    log_add(L_WARNING, "%s pid %ld is messed up, ignoring it.\n",
		    chunker->name, (long)chunker->pid);

	    if(dp) {
		/* if it was dumping something, zap it and try again */
		assert( h && activehd >= 0 );
		if(sched(dp)->attempted) {
		    log_add(L_FAIL, "%s %s %s %d [%s died]",
	    		    dp->host->hostname, dp->name, sched(dp)->datestamp,
	    		    sched(dp)->level, chunker->name);
		}
		else {
	    	    log_add(L_WARNING, "%s died while dumping %s:%s lev %d.",
	    		    chunker->name, dp->host->hostname, dp->name,
	    		    sched(dp)->level);
		}
		dp = NULL;
	    }

	    event_release(chunker->ev_read);

	    chunker->result = cmd;

	    break;

	default:
	    assert(0);
	}

	if(chunker->result != LAST_TOK && chunker->dumper->result != LAST_TOK)
	    dumper_result(dp);

    } while(areads_dataready(chunker->fd));
}


/*
 * Tell the taper to write a disk dump to tape.  If the taper
 * is busy, queue the disk in the tapeq.
 * If we're in degraded mode, do nothing, and leave the dump image
 * in the holding disk.
 */
static void
taper_queuedisk(dp)
    disk_t *dp;
{

    if (taper_busy) {
	if(sched(dp)->attempted)
	    headqueue_disk(&tapeq, dp);
	else if (use_lffo)
	    insert_disk(&tapeq, dp, sort_by_size_reversed);
	else
	    enqueue_disk(&tapeq, dp);
    } else if (!degraded_mode) {
	assert(taper_ev_read == NULL);
	taper_ev_read = event_register(taper, EV_READFD,
	    handle_taper_result, NULL);
	taper_disk = dp;
	taper_busy = 1;
	taper_cmd(FILE_WRITE, dp, sched(dp)->destname, sched(dp)->level,
	    sched(dp)->datestamp);
    }
}

static disklist_t
read_flush()
{
    sched_t *sp;
    disk_t *dp;
    int line;
    dumpfile_t file;
    char *hostname, *diskname, *datestamp;
    int level;
    char *destname;
    disk_t *dp1;
    char *inpline = NULL;
    char *command;
    char *s;
    int ch;
    disklist_t tq;

    tq.head = tq.tail = NULL;

	for(line = 0; (inpline = agets(stdin)) != NULL; free(inpline)) {
	line++;

	s = inpline;
	ch = *s++;

	skip_whitespace(s, ch);                 /* find the command */
	if(ch == '\0') {
	    error("Aflush line %d: syntax error", line);
	    continue;
	}
	command = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';

	if(strcmp(command,"ENDFLUSH") == 0) {
	    break;
	}

	if(strcmp(command,"FLUSH") != 0) {
	    error("Bflush line %d: syntax error", line);
	    continue;
	}

	skip_whitespace(s, ch);			/* find the hostname */
	if(ch == '\0') {
	    error("Cflush line %d: syntax error", line);
	    continue;
	}
	hostname = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';

	skip_whitespace(s, ch);			/* find the diskname */
	if(ch == '\0') {
	    error("Cflush line %d: syntax error", line);
	    continue;
	}
	diskname = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';

	skip_whitespace(s, ch);			/* find the datestamp */
	if(ch == '\0') {
	    error("Cflush line %d: syntax error", line);
	    continue;
	}
	datestamp = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';

	skip_whitespace(s, ch);			/* find the level number */
	if(ch == '\0' || sscanf(s - 1, "%d", &level) != 1) {
	    error("Cflush line %d: syntax error", line);
	    continue;
	}
	skip_integer(s, ch);

	skip_whitespace(s, ch);			/* find the filename */
	if(ch == '\0') {
	    error("Cflush line %d: syntax error", line);
	    continue;
	}
	destname = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';

	get_dumpfile(destname, &file);
	if( file.type != F_DUMPFILE) {
	    if( file.type != F_CONT_DUMPFILE )
		log_add(L_INFO, "%s: ignoring cruft file.", destname);
	    continue;
	}

	if(strcmp(hostname, file.name) != 0 ||
	   strcmp(diskname, file.disk) != 0 ||
	   strcmp(datestamp, file.datestamp) != 0) {
	    log_add(L_INFO, "disk %s:%s not consistent with file %s",
		    hostname, diskname, destname);
	    continue;
	}

	dp = lookup_disk(file.name, file.disk);

	if (dp == NULL) {
	    log_add(L_INFO, "%s: disk %s:%s not in database, skipping it.",
		    destname, file.name, file.disk);
	    continue;
	}

	if(file.dumplevel < 0 || file.dumplevel > 9) {
	    log_add(L_INFO, "%s: ignoring file with bogus dump level %d.",
		    destname, file.dumplevel);
	    continue;
	}

	dp1 = (disk_t *)alloc(sizeof(disk_t));
	*dp1 = *dp;
	dp1->next = dp1->prev = NULL;

	sp = (sched_t *) alloc(sizeof(sched_t));
	sp->destname = stralloc(destname);
	sp->level = file.dumplevel;
	sp->dumpdate = NULL;
	sp->datestamp = stralloc(file.datestamp);
	sp->est_size = 0;
	sp->est_time = 0;
	sp->priority = 0;
	sp->degr_level = -1;
	sp->est_kps = 10;
	sp->attempted = 0;
	sp->act_size = size_holding_files(destname);
	sp->holdp = build_diskspace(destname);
	if(sp->holdp == NULL) continue;
	sp->dumper = NULL;
	sp->timestamp = (time_t)0;

	dp1->up = (char *)sp;

	enqueue_disk(&tq, dp1);
    }
    amfree(inpline);

    return tq;
}

static disklist_t
read_schedule(waitqp)
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
    char *hostname, *diskname, *datestamp, *inpline = NULL;
    char *command;
    char *s;
    int ch;


    rq.head = rq.tail = NULL;

    /* read schedule from stdin */

    for(line = 0; (inpline = agets(stdin)) != NULL; free(inpline)) {
	line++;

	s = inpline;
	ch = *s++;

	skip_whitespace(s, ch);			/* find the command */
	if(ch == '\0') {
	    error("Aschedule line %d: syntax error", line);
	    continue;
	}
	command = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';

	if(strcmp(command,"DUMP") != 0) {
	    error("Bschedule line %d: syntax error", line);
	    continue;
	}

	skip_whitespace(s, ch);			/* find the host name */
	if(ch == '\0') {
	    error("Cschedule line %d: syntax error", line);
	    continue;
	}
	hostname = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';

	skip_whitespace(s, ch);			/* find the disk name */
	if(ch == '\0') {
	    error("Dschedule line %d: syntax error", line);
	    continue;
	}
	diskname = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';

	skip_whitespace(s, ch);			/* find the datestamp */
	if(ch == '\0') {
	    error("Eschedule line %d: syntax error", line);
	    continue;
	}
	datestamp = s - 1;
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
	sp->est_size = DISK_BLOCK_KB + size; /* include header */
	sp->est_time = time;
	sp->priority = priority;
	sp->datestamp = stralloc(datestamp);

	if(degr_dumpdate) {
	    sp->degr_level = degr_level;
	    sp->degr_dumpdate = stralloc(degr_dumpdate);
	    sp->degr_size = DISK_BLOCK_KB + degr_size;
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
	sp->activehd = -1;
	sp->dumper = NULL;
	sp->timestamp = (time_t)0;
	sp->destname = NULL;
	sp->no_space = 0;

	dp->up = (char *) sp;
	remove_disk(waitqp, dp);
	enqueue_disk(&rq, dp);
    }
    amfree(inpline);
    if(line == 0)
	log_add(L_WARNING, "WARNING: got empty schedule from planner");

    return rq;
}

static int
free_kps(ip)
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
	res = ip->maxusage - ip->curusage;
    }

    return res;
}

static void
interface_state(time_str)
    char *time_str;
{
    interface_t *ip;

    printf("driver: interface-state time %s", time_str);

    for(ip = lookup_interface(NULL); ip != NULL; ip = ip->next) {
	printf(" if %s: free %d", ip->name, free_kps(ip));
    }
    printf("\n");
}

static void
allocate_bandwidth(ip, kps)
    interface_t *ip;
    int kps;
{
    ip->curusage += kps;
}

static void
deallocate_bandwidth(ip, kps)
    interface_t *ip;
    int kps;
{
    assert(kps <= ip->curusage);
    ip->curusage -= kps;
}

/* ------------ */
static unsigned long
free_space()
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

static assignedhd_t **
find_diskspace(size, cur_idle, pref)
    unsigned long size;
    int *cur_idle;
    assignedhd_t *pref;
/* We return an array of pointers to assignedhd_t. The array contains at
 * most one entry per holding disk. The list of pointers is terminated by
 * a NULL pointer. Each entry contains a pointer to a holdingdisk and
 * how much diskspace to use on that disk. Later on, assign_holdingdisk
 * will allocate the given amount of space.
 * If there is not enough room on the holdingdisks, NULL is returned.
 */

{
    assignedhd_t **result = NULL;
    holdingdisk_t *minp, *hdp;
    int i=0, num_holdingdisks=0; /* are we allowed to use the global thing? */
    int j, minj;
    char *used;
    long halloc, dalloc, hfree, dfree;

    size = am_round(size, DISK_BLOCK_KB);

#ifdef HOLD_DEBUG
    printf("find diskspace: want %lu K\n", size );
    fflush(stdout);
#endif

    for(hdp = getconf_holdingdisks(); hdp != NULL; hdp = hdp->next) {
	num_holdingdisks++;
    }

    used = alloc(sizeof(char) * num_holdingdisks);/*disks used during this run*/
    memset( used, 0, num_holdingdisks );
    result = alloc( sizeof(assignedhd_t *) * (num_holdingdisks+1) );
    result[0] = NULL;

    while( i < num_holdingdisks && size > 0 ) {
	/* find the holdingdisk with the fewest active dumpers and among
	 * those the one with the biggest free space
	 */
	minp = NULL; minj = -1;
	for(j = 0, hdp = getconf_holdingdisks(); hdp != NULL; hdp = hdp->next, j++ ) {
	    if( pref && pref->disk == hdp && !used[j] &&
		holdalloc(hdp)->allocated_space <= hdp->disksize - DISK_BLOCK_KB) {
		minp = hdp;
		minj = j;
		break;
	    }
	    else if( holdalloc(hdp)->allocated_space <= hdp->disksize - 2*DISK_BLOCK_KB &&
		!used[j] &&
		(!minp ||
		 holdalloc(hdp)->allocated_dumpers < holdalloc(minp)->allocated_dumpers ||
		 (holdalloc(hdp)->allocated_dumpers == holdalloc(minp)->allocated_dumpers &&
		  hdp->disksize-holdalloc(hdp)->allocated_space > minp->disksize-holdalloc(minp)->allocated_space)) ) {
		minp = hdp;
		minj = j;
	    }
	}

	pref = NULL;
	if( !minp ) { break; } /* all holding disks are full */
	used[minj] = 1;

	/* hfree = free space on the disk */
	hfree = minp->disksize - holdalloc(minp)->allocated_space;

	/* dfree = free space for data, remove 1 header for each chunksize */
	dfree = hfree - (((hfree-1)/minp->chunksize)+1) * DISK_BLOCK_KB;

	/* dalloc = space I can allocate for data */
	dalloc = ( dfree < size ) ? dfree : size;

	/* halloc = space to allocate, including 1 header for each chunksize */
	halloc = dalloc + (((dalloc-1)/minp->chunksize)+1) * DISK_BLOCK_KB;

#ifdef HOLD_DEBUG
	fprintf(stdout,"find diskspace: size %ld hf %ld df %ld da %ld ha %ld\n",
		size, hfree, dfree, dalloc, halloc);
	fflush(stdout);
#endif
	size -= dalloc;
	result[i] = alloc(sizeof(assignedhd_t));
	result[i]->disk = minp;
	result[i]->reserved = halloc;
	result[i]->used = 0;
	result[i]->destname = NULL;
	result[i+1] = NULL;
	i++;
    } /* while i < num_holdingdisks && size > 0 */
    amfree(used);

    if( size ) { /* not enough space available */
	printf("find diskspace: not enough diskspace. Left with %lu K\n", size);
	fflush(stdout);
	free_assignedhd(result);
	result = NULL;
    }

#ifdef HOLD_DEBUG
    for( i = 0; result && result[i]; i++ ) {
	printf("find diskspace: selected %s free %ld reserved %ld dumpers %d\n",
		result[i]->disk->diskdir,
		result[i]->disk->disksize - holdalloc(result[i]->disk)->allocated_space,
		result[i]->reserved,
		holdalloc(result[i]->disk)->allocated_dumpers);
    }
    fflush(stdout);
#endif

    return result;
}

static int
assign_holdingdisk(holdp, diskp)
    assignedhd_t **holdp;
    disk_t *diskp;
{
    int i, j, c, l=0;
    unsigned long size;
    char *sfn = sanitise_filename(diskp->name);
    char lvl[64];
    assignedhd_t **new_holdp;

    snprintf( lvl, sizeof(lvl), "%d", sched(diskp)->level );

    size = am_round(sched(diskp)->est_size - sched(diskp)->act_size,
		    DISK_BLOCK_KB);

    for( c = 0; holdp[c]; c++ ); /* count number of disks */

    /* allocate memory for sched(diskp)->holdp */
    for(j = 0; sched(diskp)->holdp && sched(diskp)->holdp[j]; j++) {}
    new_holdp = (assignedhd_t **)alloc(sizeof(assignedhd_t*)*(j+c+1));
    if (sched(diskp)->holdp) {
	memcpy(new_holdp, sched(diskp)->holdp, j * sizeof(*new_holdp));
	amfree(sched(diskp)->holdp);
    }
    sched(diskp)->holdp = new_holdp;
    new_holdp = NULL;

    i = 0;
    if( j > 0 ) { /* This is a request for additional diskspace. See if we can
		   * merge assignedhd_t's */
	l=j;
	if( sched(diskp)->holdp[j-1]->disk == holdp[0]->disk ) { /* Yes! */
	    sched(diskp)->holdp[j-1]->reserved += holdp[0]->reserved;
	    holdalloc(holdp[0]->disk)->allocated_space += holdp[0]->reserved;
	    size = (holdp[0]->reserved>size) ? 0 : size-holdp[0]->reserved;
#ifdef HOLD_DEBUG
	    printf("merging holding disk %s to disk %s:%s, add %lu for reserved %lu, left %lu\n",
		   sched(diskp)->holdp[j-1]->disk->diskdir,
		   diskp->host->hostname, diskp->name,
		   holdp[0]->reserved, sched(diskp)->holdp[j-1]->reserved,
		   size );
	    fflush(stdout);
#endif
	    i++;
	    amfree(holdp[0]);
	    l=j-1;
	}
    }

    /* copy assignedhd_s to sched(diskp), adjust allocated_space */
    for( ; holdp[i]; i++ ) {
	holdp[i]->destname = newvstralloc( holdp[i]->destname,
					   holdp[i]->disk->diskdir, "/",
					   datestamp, "/",
					   diskp->host->hostname, ".",
					   sfn, ".",
					   lvl, NULL );
	sched(diskp)->holdp[j++] = holdp[i];
	holdalloc(holdp[i]->disk)->allocated_space += holdp[i]->reserved;
	size = (holdp[i]->reserved>size) ? 0 : size-holdp[i]->reserved;
#ifdef HOLD_DEBUG
	printf("%d assigning holding disk %s to disk %s:%s, reserved %lu, left %lu\n", i,
		holdp[i]->disk->diskdir, diskp->host->hostname, diskp->name,
		holdp[i]->reserved, size );
	fflush(stdout);
#endif
	holdp[i] = NULL; /* so it doesn't get free()d... */
    }
    sched(diskp)->holdp[j] = NULL;
    amfree(sfn);

    return l;
}

static void
adjust_diskspace(diskp, cmd)
    disk_t *diskp;
    cmd_t cmd;
{
    assignedhd_t **holdp;
    unsigned long total=0;
    long diff;
    int i;

#ifdef HOLD_DEBUG
    printf("adjust: %s:%s %s\n", diskp->host->hostname, diskp->name,
	   sched(diskp)->destname );
    fflush(stdout);
#endif

    holdp = sched(diskp)->holdp;

    assert(holdp);

    for( i = 0; holdp[i]; i++ ) { /* for each allocated disk */
	diff = holdp[i]->used - holdp[i]->reserved;
	total += holdp[i]->used;
	holdalloc(holdp[i]->disk)->allocated_space += diff;

#ifdef HOLD_DEBUG
	printf("adjust: hdisk %s done, reserved %ld used %ld diff %ld alloc %ld dumpers %d\n",
		holdp[i]->disk->name, holdp[i]->reserved, holdp[i]->used, diff,
		holdalloc(holdp[i]->disk)->allocated_space,
		holdalloc(holdp[i]->disk)->allocated_dumpers );
	fflush(stdout);
#endif
	holdp[i]->reserved += diff;
    }

    sched(diskp)->act_size = total;

#ifdef HOLD_DEBUG
    printf("adjust: after: disk %s:%s used %ld\n", diskp->host->hostname,
	   diskp->name, sched(diskp)->act_size );
    fflush(stdout);
#endif

}

static void
delete_diskspace(diskp)
    disk_t *diskp;
{
    assignedhd_t **holdp;
    int i;

    holdp = sched(diskp)->holdp;

    assert(holdp);

    for( i = 0; holdp[i]; i++ ) { /* for each disk */
	/* find all files of this dump on that disk, and subtract their
	 * reserved sizes from the disk's allocated space
	 */
	holdalloc(holdp[i]->disk)->allocated_space -= holdp[i]->used;
    }

    unlink_holding_files(holdp[0]->destname);	/* no need for the entire list,
						 * because unlink_holding_files
						 * will walk through all files
						 * using cont_filename */
    free_assignedhd(sched(diskp)->holdp);
    sched(diskp)->holdp = NULL;
    sched(diskp)->act_size = 0;
}

static assignedhd_t **build_diskspace(destname)
char *destname;
{
    int i, j;
    int fd;
    int buflen;
    char buffer[DISK_BLOCK_BYTES];
    dumpfile_t file;
    assignedhd_t **result;
    holdingdisk_t *hdp;
    int *used;
    int num_holdingdisks=0;
    char dirname[1000], *ch;
    struct stat finfo;
    char *filename = destname;

    for(hdp = getconf_holdingdisks(); hdp != NULL; hdp = hdp->next) {
        num_holdingdisks++;
    }
    used = alloc(sizeof(int) * num_holdingdisks);
    for(i=0;i<num_holdingdisks;i++)
	used[i] = 0;
    result = alloc( sizeof(assignedhd_t *) * (num_holdingdisks+1) );
    result[0] = NULL;
    while(filename != NULL && filename[0] != '\0') {
	strncpy(dirname, filename, 999);
	dirname[999]='\0';
	ch = strrchr(dirname,'/');
        *ch = '\0';
	ch = strrchr(dirname,'/');
        *ch = '\0';

	for(j = 0, hdp = getconf_holdingdisks(); hdp != NULL;
						 hdp = hdp->next, j++ ) {
	    if(strcmp(dirname,hdp->diskdir)==0) {
		break;
	    }
	}

	if(stat(filename, &finfo) == -1) {
	    fprintf(stderr, "stat %s: %s\n", filename, strerror(errno));
	    finfo.st_size = 0;
	}
	used[j] += (finfo.st_size+1023)/1024;
	if((fd = open(filename,O_RDONLY)) == -1) {
	    fprintf(stderr,"build_diskspace: open of %s failed: %s\n",
		    filename, strerror(errno));
	    return NULL;
	}
	buflen = fullread(fd, buffer, sizeof(buffer));
	parse_file_header(buffer, &file, buflen);
	close(fd);
	filename = file.cont_filename;
    }

    for(j = 0, i=0, hdp = getconf_holdingdisks(); hdp != NULL;
						  hdp = hdp->next, j++ ) {
	if(used[j]) {
	    result[i] = alloc(sizeof(assignedhd_t));
	    result[i]->disk = hdp;
	    result[i]->reserved = used[j];
	    result[i]->used = used[j];
	    result[i]->destname = stralloc(destname);
	    result[i+1] = NULL;
	    i++;
	}
    }

    return result;
}

static void
holdingdisk_state(time_str)
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

static void
update_failed_dump_to_tape(dp)
    disk_t *dp;
{
/* JLM
 * should simply set no_bump
 */

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
static int
dump_to_tape(dp)
    disk_t *dp;
{
    dumper_t *dumper;
    int failed = 0;
    int filenum;
    long origsize = 0;
    long dumpsize = 0;
    long dumptime = 0;
    float tapetime = 0;
    cmd_t cmd;
    int result_argc;
    char *result_argv[MAX_ARGS+1];

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
	return 2;	/* fatal problem */
    }

    /* tell the taper to read from a port number of its choice */

    taper_cmd(PORT_WRITE, dp, NULL, sched(dp)->level, sched(dp)->datestamp);
    cmd = getresult(taper, 1, &result_argc, result_argv, MAX_ARGS+1);
    if(cmd != PORT) {
	printf("driver: did not get PORT from taper for %s:%s\n",
		dp->host->hostname, dp->name);
	fflush(stdout);
	return 2;	/* fatal problem */
    }
    /* copy port number */
    dumper->output_port = atoi(result_argv[2]);

    /* tell the dumper to dump to a port */

    dumper_cmd(dumper, PORT_DUMP, dp);
    dp->host->start_t = time(NULL) + 15;

    /* update statistics & print state */

    taper_busy = dumper->busy = 1;
    dp->host->inprogress += 1;
    dp->inprogress = 1;
    sched(dp)->timestamp = time((time_t *)0);
    allocate_bandwidth(dp->host->netif, sched(dp)->est_kps);
    idle_reason = NOT_IDLE;

    short_dump_state();

    /* wait for result from dumper */

    cmd = getresult(dumper->fd, 1, &result_argc, result_argv, MAX_ARGS+1);

    if(cmd != BOGUS)
	free_serial(result_argv[2]);

    switch(cmd) {
    case BOGUS:
	/* either eof or garbage from dumper */
	log_add(L_WARNING, "%s pid %ld is messed up, ignoring it.\n",
	        dumper->name, (long)dumper->pid);
	dumper->down = 1;	/* mark it down so it isn't used again */
	failed = 1;	/* dump failed, must still finish up with taper */
	break;

    case DONE: /* DONE <handle> <origsize> <dumpsize> <dumptime> <errstr> */
	/* everything went fine */
	origsize = (long)atof(result_argv[3]);
	/*dumpsize = (long)atof(result_argv[4]);*/
	dumptime = (long)atof(result_argv[5]);
	break;

    case NO_ROOM: /* NO-ROOM <handle> */
	dumper_cmd(dumper, ABORT, dp);
	cmd = getresult(dumper->fd, 1, &result_argc, result_argv, MAX_ARGS+1);
	if(cmd != BOGUS)
	    free_serial(result_argv[2]);
	assert(cmd == ABORT_FINISHED);

    case TRYAGAIN: /* TRY-AGAIN <handle> <errstr> */
    default:
	/* dump failed, but we must still finish up with taper */
	failed = 1;	/* problem with dump, possibly nonfatal */
	break;
	
    case FAILED: /* FAILED <handle> <errstr> */
	/* dump failed, but we must still finish up with taper */
	failed = 2;     /* fatal problem with dump */
	break;
    }

    /*
     * Note that at this point, even if the dump above failed, it may
     * not be a fatal failure if taper below says we can try again.
     * E.g. a dumper failure above may actually be the result of a
     * tape overflow, which in turn causes dump to see "broken pipe",
     * "no space on device", etc., since taper closed the port first.
     */

    cmd = getresult(taper, 1, &result_argc, result_argv, MAX_ARGS+1);

    switch(cmd) {
    case PARTIAL:
    case DONE: /* DONE <handle> <label> <tape file> <err mess> */
	if(result_argc != 5) {
	    error("error [dump to tape DONE result_argc != 5: %d]", result_argc);
	}

	free_serial(result_argv[2]);

	if(failed == 1) goto tryagain;	/* dump didn't work */
	else if(failed == 2) goto fatal;

	sscanf(result_argv[5],"[sec %f kb %ld ",&tapetime,&dumpsize);

	if(cmd == DONE) {
	    /* every thing went fine */
	    update_info_dumper(dp, origsize, dumpsize, dumptime);
	    filenum = atoi(result_argv[4]);
	    update_info_taper(dp, result_argv[3], filenum, sched(dp)->level);
	    /* note that update_info_dumper() must be run before
	       update_info_taper(), since update_info_dumper overwrites
	       tape information.  */
	}

	break;

    case TRYAGAIN: /* TRY-AGAIN <handle> <err mess> */
    tryagain:
	update_failed_dump_to_tape(dp);
	free_serial(result_argv[2]);
	enqueue_disk(&runq, dp);
	break;


    case TAPE_ERROR: /* TAPE-ERROR <handle> <err mess> */
    case BOGUS:
    default:
    fatal:
	update_failed_dump_to_tape(dp);
	free_serial(result_argv[2]);
	failed = 2;	/* fatal problem */
	start_degraded_mode(&runq);
    }

    /* reset statistics & return */

    taper_busy = dumper->busy = 0;
    dp->host->inprogress -= 1;
    dp->inprogress = 0;
    deallocate_bandwidth(dp->host->netif, sched(dp)->est_kps);

    return failed;
}

static int
queue_length(q)
    disklist_t q;
{
    disk_t *p;
    int len;

    for(len = 0, p = q.head; p != NULL; len++, p = p->next);
    return len;
}

static void
short_dump_state()
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
    printf(" roomq: %d", queue_length(roomq));
    printf(" wakeup: %d", (int)sleep_time);
    printf(" driver-idle: %s\n", idle_strings[idle_reason]);
    interface_state(wall_time);
    holdingdisk_state(wall_time);
    fflush(stdout);
}

#if 0
static void
dump_state(str)
    const char *str;
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
    dump_queue("ROOM", roomq, 5, stdout);
    dump_queue("RUN ", runq, 5, stdout);
    printf("================\n");
    fflush(stdout);
}
#endif
