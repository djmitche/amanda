/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991-2000 University of Maryland at College Park
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
 * $Id: driver.c,v 1.58.2.31.2.8.2.11 2002/04/14 13:15:19 martinea Exp $
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
#include "version.h"
#include "driverio.h"
#include "server_util.h"

disklist_t waitq, runq, tapeq, roomq;
int pending_aborts, inside_dump_to_tape;
int use_lffo;
disk_t *taper_disk;
int degraded_mode;
unsigned long reserved_space;
unsigned long total_disksize;
char *dumper_program;
int  inparallel;
int nodump = 0;

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
void handle_dumper_result P((int fd));
void read_flush P((disklist_t *tapeqp));
void read_schedule P((disklist_t *waitqp, disklist_t *runqp));
int free_kps P((interface_t *ip));
void interface_state P((char *time_str));
void allocate_bandwidth P((interface_t *ip, int kps));
void deallocate_bandwidth P((interface_t *ip, int kps));
unsigned long free_space P((void));
assignedhd_t **find_diskspace P((unsigned long size, int *cur_idle, assignedhd_t *preferred));
char *diskname2filename P((char *dname));
int assign_holdingdisk P((assignedhd_t **holdp, disk_t *diskp));
static void adjust_diskspace P((disk_t *diskp, cmd_t cmd));
static void delete_diskspace P((disk_t *diskp));
assignedhd_t **build_diskspace P((char *destname));
void holdingdisk_state P((char *time_str));
int dump_to_tape P((disk_t *dp));
int queue_length P((disklist_t q));
void short_dump_state P((void));
void dump_state P((char *str));
int main P((int main_argc, char **main_argv));

static int idle_reason;
char *datestamp;

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
char *config_name = NULL;
char *config_dir = NULL;

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
    FD_ZERO(&readset);

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
    if((origqp = read_diskfile(conf_diskfile)) == NULL) {
	error("could not load disklist \"%s\"", conf_diskfile);
    }
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
	if (mkpdir(newdir, 0770, (uid_t)-1, (gid_t)-1) != 0 && errno != EEXIST){
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

    waitq = *origqp;
    tapeq.head = tapeq.tail = NULL;
    roomq.head = roomq.tail = NULL;
    runq.head = runq.tail = NULL;

    read_flush(&tapeq);
    if(!nodump) read_schedule(&waitq, &runq);

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
	FD_CLR(taper,&readset);
    }

    if(empty(tapeq) || degraded_mode) {
	taper_busy = 0;
	taper_disk = NULL;
    }
    else {
	disk_t *dp;
	dp = dequeue_disk(&tapeq);
	taper_disk = dp;
	taper_busy = 1;
	taper_cmd(FILE_WRITE, dp, sched(dp)->destname, sched(dp)->level, 
		  sched(dp)->datestamp);
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

	for(fd = 0; fd <= maxfd; fd++) {
	    /*
	     * The first pass through the following loop, we have
	     * data ready for areads (called by getresult, called by
	     * handle_.*_result).  But that may read more than one record,
	     * so we need to keep processing as long as areads has data.
	     * We will get control back after each record and the buffer
	     * will go empty (indicated by areads_dataready(fd) == 0)
	     * after the last one available has been processed.
	     */
	    while(FD_ISSET(fd, &selectset) || areads_dataready(fd) > 0) {
		if(fd == taper) handle_taper_result();
		else handle_dumper_result(fd);
		FD_CLR(fd, &selectset);
	    }
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
		log_add(L_FAIL, "%s %s %s %d [dump to tape failed]",
		        diskp->host->hostname,
			diskp->name,
			sched(diskp)->datestamp,
			sched(diskp)->level);
	}
	else
	    log_add(L_FAIL, "%s %s %s %d [%s]",
		    diskp->host->hostname, diskp->name,
		    sched(diskp)->datestamp, sched(diskp)->level,
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

    if(taper >= 0) {
	taper_cmd(QUIT, NULL, NULL, 0, NULL);
    }

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
	    ap_snprintf(number, sizeof(number), "%ld", (long)pid);
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

int start_some_dumps(rq)
disklist_t *rq;
{
    int total, cur_idle;
    disk_t *diskp, *diskp_accept;
    dumper_t *dumper;
    assignedhd_t **holdp=NULL, **holdp_accept;
    time_t now = time(NULL);

    total = 0;
    idle_reason = IDLE_NO_DUMPERS;
    sleep_time.tv_sec = SLEEP_MAX;
    sleep_time.tv_usec = 0;
    any_delayed_disk = 0;

    if(rq->head == NULL) {
	idle_reason = 0;
	return 0;
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
    for(dumper = dmptable; dumper < dmptable+inparallel; dumper++) {
	if(dumper->busy || dumper->down) continue;
	/* found an idle dumper, now find a disk for it */
	diskp = rq->head;
	diskp_accept = NULL;
	holdp_accept = NULL;

	if(idle_reason == IDLE_NO_DUMPERS)
	    idle_reason = NOT_IDLE;

	cur_idle = NOT_IDLE;

	while(diskp) {
	    assert(diskp->host != NULL && sched(diskp) != NULL);

	    /* round estimate to next multiple of DISK_BLOCK_KB */
	    sched(diskp)->est_size = am_round(sched(diskp)->est_size,
					      DISK_BLOCK_KB);

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
	    else if(diskp->host->netif->curusage > 0 &&
		    sched(diskp)->est_kps > free_kps(diskp->host->netif))
		cur_idle = max(cur_idle, IDLE_NO_BANDWIDTH);
	    else if(sched(diskp)->no_space)
		cur_idle = max(cur_idle, IDLE_NO_DISKSPACE);
	    else if((holdp = find_diskspace(sched(diskp)->est_size,&cur_idle,NULL)) == NULL)
		cur_idle = max(cur_idle, IDLE_NO_DISKSPACE);
	    else if(diskp->no_hold) {
		free_assignedhd(holdp);
		cur_idle = max(cur_idle, IDLE_NO_HOLD);
	    } else if(client_constrained(diskp)) {
		free_assignedhd(holdp);
		cur_idle = max(cur_idle, IDLE_CLIENT_CONSTRAINED);
	    } else {

		/* disk fits, dump it */
		int accept = !diskp_accept;
		if(!accept) {
		    char dumptype;
		    char *dumporder = getconf_str(CNF_DUMPORDER);
		    if(strlen(dumporder) <= (dumper-dmptable)) {
			if(dumper-dmptable < 3)
			    dumptype = 't';
			else
			    dumptype = 'T';
		    }
		    else {
			dumptype = dumporder[dumper-dmptable];
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
		      default:	log_add(L_WARNING, "Unknown dumporder character \'%c\', using 's'.\n",
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
	    diskp = diskp->next;
	}

	diskp = diskp_accept;
	holdp = holdp_accept;
	if(diskp) {
	    cur_idle = NOT_IDLE;
	    sched(diskp)->act_size = 0;
	    allocate_bandwidth(diskp->host->netif, sched(diskp)->est_kps);
	    sched(diskp)->activehd = assign_holdingdisk(holdp, diskp);
	    diskp->host->inprogress += 1;	/* host is now busy */
	    diskp->inprogress = 1;
	    sched(diskp)->dumper = dumper;
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
		log_add(L_FAIL, "%s %s %s %d [can't switch to incremental dump]",
		        dp->host->hostname, dp->name,
			sched(dp)->datestamp, sched(dp)->level);
	    }
	}
    }

    *queuep = newq;
    degraded_mode = 1;

    dump_schedule(queuep, "after start degraded mode");
}

void continue_dumps()
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
	h = find_diskspace( sched(dp)->est_size - sched(dp)->act_size, &active_dumpers, h[i] );
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
	    } else if( !dp || sched(dp)->est_size > sched(dumper->dp)->est_size ) {
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
	dumper_cmd( sched(dp)->dumper, ABORT, NULL );
	pending_aborts++;
    }
}

void handle_taper_result()
{
    disk_t *dp;
    int filenum;
    cmd_t cmd;
    int result_argc;
    char *result_argv[MAX_ARGS+1];

    cmd = getresult(taper, 1, &result_argc, result_argv, MAX_ARGS+1);

    switch(cmd) {

    case DONE:	/* DONE <handle> <label> <tape file> <err mess> */
	if(result_argc != 5) {
	    error("error: [taper DONE result_argc != 5: %d", result_argc);
	}

	dp = serial2disk(result_argv[2]);
	free_serial(result_argv[2]);

	filenum = atoi(result_argv[4]);
	update_info_taper(dp, result_argv[3], filenum, sched(dp)->level);

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
	    taper_cmd(FILE_WRITE, dp, sched(dp)->destname, sched(dp)->level,
		      sched(dp)->datestamp);
	}
	continue_dumps(); /* continue with those dumps waiting for diskspace */
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
	    log_add(L_FAIL, "%s %s %d %s [too many taper retries]",
		    dp->host->hostname, dp->name, sched(dp)->level,
		    sched(dp)->datestamp);
	    continue_dumps();
	}
	else {
	    sched(dp)->attempted++;
	    headqueue_disk(&tapeq, dp);
	}

	/* run next thing from queue */
	if(empty(tapeq)) {
	    taper_busy = 0;
	    taper_disk = NULL;
	}
	else {
	    dp = dequeue_disk(&tapeq);
	    taper_disk = dp;
	    taper_cmd(FILE_WRITE, dp, sched(dp)->destname, sched(dp)->level,
		      sched(dp)->datestamp);
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
	FD_CLR(taper,&readset);
	if(cmd != TAPE_ERROR) aclose(taper);
	continue_dumps();
	break;
    default:
	error("driver received unexpected token (%d) from taper", cmd);
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


void handle_dumper_result(fd)
     int fd;
{
    assignedhd_t **h=NULL;
    dumper_t *dumper;
    disk_t *dp, *sdp;
    long origsize;
    long dumpsize;
    long dumptime;
    cmd_t cmd;
    int result_argc;
    char *result_argv[MAX_ARGS+1];
    int i, dummy;
    int activehd = -1;

    dumper = lookup_dumper(fd);
    dp = dumper->dp;
    assert(dp && sched(dp) && sched(dp)->destname);

    if(dp && sched(dp) && sched(dp)->holdp) {
	h = sched(dp)->holdp;
	activehd = sched(dp)->activehd;
    }

    cmd = getresult(fd, 1, &result_argc, result_argv, MAX_ARGS+1);

    if(cmd != BOGUS) {
	sdp = serial2disk(result_argv[2]); /* result_argv[2] always contains the serial number */
	assert(sdp == dp);
    }

    switch(cmd) {

    case DONE: /* DONE <handle> <origsize> <dumpsize> <dumptime> <err str> */
	if(result_argc != 6) {
	    error("error [dumper DONE result_argc != 6: %d]", result_argc);
	}

	free_serial(result_argv[2]);

	origsize = (long)atof(result_argv[3]);
	dumpsize = (long)atof(result_argv[4]);
	dumptime = (long)atof(result_argv[5]);
	update_info_dumper(dp, origsize, dumpsize, dumptime);

	/* adjust holdp[active]->used using the real dumpsize and all other
	 * holdp[i]->used as an estimate.
	 */

	dummy = 0;
	for( i = 0, h = sched(dp)->holdp; i < activehd; i++ ) {
	    dummy += h[i]->used;
	}

	rename_tmp_holding(sched(dp)->destname, 1);
	assert( h && activehd >= 0 );
	h[activehd]->used = size_holding_files(sched(dp)->destname) - dummy;
	deallocate_bandwidth(dp->host->netif, sched(dp)->est_kps);
	holdalloc(h[activehd]->disk)->allocated_dumpers--;
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
	} else if(!degraded_mode) {
	    taper_disk = dp;
	    taper_busy = 1;
	    taper_cmd(FILE_WRITE, dp, sched(dp)->destname, sched(dp)->level,
		      sched(dp)->datestamp);
	}
	dp = NULL;
	continue_dumps();

	break;

    case TRYAGAIN: /* TRY-AGAIN <handle> <err str> */
    case FATAL_TRYAGAIN:
	free_serial(result_argv[2]);

	rename_tmp_holding(sched(dp)->destname, 0);
	deallocate_bandwidth(dp->host->netif, sched(dp)->est_kps);
	assert( h && activehd >= 0 );
	holdalloc(h[activehd]->disk)->allocated_dumpers--;
	/* Because we don't know how much was written to disk the
	 * following functions *must* be called together!
	 */
	adjust_diskspace(dp, DONE);
	delete_diskspace(dp);
	dumper->busy = 0;
	dp->host->inprogress -= 1;
	dp->inprogress = 0;

	if(sched(dp)->attempted) {
	    log_add(L_FAIL, "%s %s %d %s[could not connect to %s]",
		    dp->host->hostname, dp->name,
		    sched(dp)->level, sched(dp)->datestamp, dp->host->hostname);
	} else {
	    sched(dp)->attempted++;
	    enqueue_disk(&runq, dp);
	}
	continue_dumps();

	if(cmd == FATAL_TRYAGAIN) {
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
	assert( h && activehd >= 0 );
	holdalloc(h[activehd]->disk)->allocated_dumpers--;
	/* Because we don't know how much was written to disk the
	 * following functions *must* be called together!
	 */
	adjust_diskspace(dp, DONE);
	delete_diskspace(dp);
	dumper->busy = 0;
	dp->host->inprogress -= 1;
	dp->inprogress = 0;
	continue_dumps();

	/* no need to log this, dumper will do it */
	/* sleep in case the dumper failed because of a temporary network
	   problem, as NIS or NFS... */
	sleep(15);
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
	if( h[++activehd] ) { /* There's still some allocated space left. Tell
			       * the dumper about it. */
	    sched(dp)->activehd++;
	    dumper_cmd( dumper, CONTINUE, dp );
	} else { /* !h[++activehd] - must allocate more space */
	    sched(dp)->act_size = sched(dp)->est_size; /* not quite true */
	    sched(dp)->est_size = sched(dp)->act_size * 21 / 20; /* +5% */
	    sched(dp)->est_size = am_round(sched(dp)->est_size, DISK_BLOCK_KB);
	    h = find_diskspace( sched(dp)->est_size - sched(dp)->act_size,
				&dummy,
				h[activehd-1] );
	    if( !h ) {
    /*	    cur_idle = max(cur_idle, IDLE_NO_DISKSPACE); */
	        /* No diskspace available. The reason for this will be
	         * determined in continue_dumps(). */
	        enqueue_disk( &roomq, dp );
	        continue_dumps();
	    } else {
	        /* OK, allocate space for disk and have dumper continue */
		sched(dp)->activehd = assign_holdingdisk( h, dp );
		dumper_cmd( dumper, CONTINUE, dp );
		amfree(h);
	    }
	}
	break;

    case ABORT_FINISHED: /* ABORT-FINISHED <handle> */
	assert(pending_aborts);
	free_serial(result_argv[2]);

	rename_tmp_holding(sched(dp)->destname, 0);
	deallocate_bandwidth(dp->host->netif, sched(dp)->est_kps);
	/* Because we don't know how much was written to disk the
	 * following functions *must* be called together!
	 */
	adjust_diskspace(dp, DONE);
	delete_diskspace(dp);
	sched(dp)->attempted++;
	enqueue_disk(&runq, dp);	/* we'll try again later */
	dumper->busy = 0;
	dp->host->inprogress -= 1;
	dp->inprogress = 0;
	dp = NULL;
	pending_aborts--;
	continue_dumps();
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
	    assert( h && activehd >= 0 );
	    holdalloc(h[activehd]->disk)->allocated_dumpers--;
	    /* Because we don't know how much was written to disk the
	     * following functions *must* be called together!
	     */
	    adjust_diskspace(dp, DONE);
	    delete_diskspace(dp);
	    dp->host->inprogress -= 1;
	    dp->inprogress = 0;
	    if(sched(dp)->attempted) {
		log_add(L_FAIL, "%s %s %d %s [%s died]",
		        dp->host->hostname, dp->name,
		        sched(dp)->level, sched(dp)->datestamp, dumper->name);
	    }
	    else {
		log_add(L_WARNING, "%s died while dumping %s:%s lev %d.",
		        dumper->name, dp->host->hostname, dp->name,
		        sched(dp)->level);
		sched(dp)->attempted++;
		enqueue_disk(&runq, dp);
	    }
	    dp = NULL;
	    continue_dumps();
	}
	break;

    default:
	assert(0);
    }

    return;
}


void read_flush(tapeqp)
disklist_t *tapeqp;
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

    /* read schedule from stdin */

    for(line = 0; (inpline = agets(stdin)) != NULL; free(inpline)) {
	line++;

	s = inpline;
	ch = *s++;

	skip_whitespace(s, ch);			/* find the command */
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
	/*sp->holdp = NULL; JLM: must be build*/
	sp->holdp = build_diskspace(destname);
        if(sp->holdp == NULL) continue;
	sp->dumper = NULL;
	sp->timestamp = (time_t)0;

	dp1->up = (char *)sp;

	enqueue_disk(tapeqp, dp1);
    }
    amfree(inpline);
}


void read_schedule(waitqp, runqp)
disklist_t *waitqp, *runqp;
{
    sched_t *sp;
    disk_t *dp;
    int level, line, priority;
    char *dumpdate, *degr_dumpdate;
    int degr_level;
    long time, degr_time;
    unsigned long size, degr_size;
    char *hostname, *features, *diskname, *datestamp, *inpline = NULL;
    char *command;
    char *s;
    int ch;

    /* read schedule from stdin */

    for(line = 0; (inpline = agets(stdin)) != NULL; free(inpline)) {
	line++;

	s = inpline;
	ch = *s++;

	skip_whitespace(s, ch);			/* find the command */
	if(ch == '\0') {
	    error("schedule line %d: syntax error (no command)", line);
	    continue;
	}
	command = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';

	if(strcmp(command,"DUMP") != 0) {
	    error("schedule line %d: syntax error (%s != DUMP)", line, command);
	    continue;
	}

	skip_whitespace(s, ch);			/* find the host name */
	if(ch == '\0') {
	    error("schedule line %d: syntax error (no host name)", line);
	    continue;
	}
	hostname = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';

	skip_whitespace(s, ch);			/* find the feature list */
	if(ch == '\0') {
	    error("schedule line %d: syntax error (no feature list)", line);
	    continue;
	}
	features = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';

	skip_whitespace(s, ch);			/* find the disk name */
	if(ch == '\0') {
	    error("schedule line %d: syntax error (no disk name)", line);
	    continue;
	}
	diskname = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';

	skip_whitespace(s, ch);			/* find the datestamp */
	if(ch == '\0') {
	    error("schedule line %d: syntax error (no datestamp)", line);
	    continue;
	}
	datestamp = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';

	skip_whitespace(s, ch);			/* find the priority number */
	if(ch == '\0' || sscanf(s - 1, "%d", &priority) != 1) {
	    error("schedule line %d: syntax error (bad priority)", line);
	    continue;
	}
	skip_integer(s, ch);

	skip_whitespace(s, ch);			/* find the level number */
	if(ch == '\0' || sscanf(s - 1, "%d", &level) != 1) {
	    error("schedule line %d: syntax error (bad level)", line);
	    continue;
	}
	skip_integer(s, ch);

	skip_whitespace(s, ch);			/* find the dump date */
	if(ch == '\0') {
	    error("schedule line %d: syntax error (bad dump date)", line);
	    continue;
	}
	dumpdate = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';

	skip_whitespace(s, ch);			/* find the size number */
	if(ch == '\0' || sscanf(s - 1, "%lu", &size) != 1) {
	    error("schedule line %d: syntax error (bad size)", line);
	    continue;
	}
	skip_integer(s, ch);

	skip_whitespace(s, ch);			/* find the time number */
	if(ch == '\0' || sscanf(s - 1, "%ld", &time) != 1) {
	    error("schedule line %d: syntax error (bad estimated time)", line);
	    continue;
	}
	skip_integer(s, ch);

	degr_dumpdate = NULL;			/* flag if degr fields found */
	skip_whitespace(s, ch);			/* find the degr level number */
	if(ch != '\0') {
	    if(sscanf(s - 1, "%d", &degr_level) != 1) {
		error("schedule line %d: syntax error (bad degr level)", line);
		continue;
	    }
	    skip_integer(s, ch);

	    skip_whitespace(s, ch);		/* find the degr dump date */
	    if(ch == '\0') {
		error("schedule line %d: syntax error (bad degr dump date)", line);
		continue;
	    }
	    degr_dumpdate = s - 1;
	    skip_non_whitespace(s, ch);
	    s[-1] = '\0';

	    skip_whitespace(s, ch);		/* find the degr size number */
	    if(ch == '\0'  || sscanf(s - 1, "%lu", &degr_size) != 1) {
		error("schedule line %d: syntax error (bad degr size)", line);
		continue;
	    }
	    skip_integer(s, ch);

	    skip_whitespace(s, ch);		/* find the degr time number */
	    if(ch == '\0' || sscanf(s - 1, "%lu", &degr_time) != 1) {
		error("schedule line %d: syntax error (bad degr estimated time)", line);
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
	dp->host->features = am_string_to_feature(features);
	remove_disk(waitqp, dp);
	insert_disk(&runq, dp, sort_by_time);
    }
    amfree(inpline);
    if(line == 0)
	log_add(L_WARNING, "WARNING: got empty schedule from planner");
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
	res = ip->maxusage - ip->curusage;
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

assignedhd_t **find_diskspace(size, cur_idle, pref)
unsigned long size;
int *cur_idle;
assignedhd_t *pref;
/* Rewrite by Peter Conrad <conrad@opus5.de>, June '99:
 *  - enable splitting a dump across several holding disks
 *  - allocate only as much as size tells us, dumpers may request more later
 * We return an array of pointers to assignedhd_t. The array contains at
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
	fprintf(stdout,"find diskspace: size %ld hf %ld df %ld da %ld ha %ld\n",		size, hfree, dfree, dalloc, halloc);
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
#ifdef HOLD_DEBUG
	printf("find diskspace: not enough diskspace. Left with %lu K\n", size);
	fflush(stdout);
#endif
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

int assign_holdingdisk(holdp, diskp)
assignedhd_t **holdp;
disk_t *diskp;
{
/* Modified by Peter Conrad <conrad@opus5.de>, June '99
 * Modifications for splitting dumps across holding disks:
 * sched(diskp)->holdp now contains an array of pointers to assignedhd_t.
 */
    int i, j, c, l=0;
    unsigned long size;
    char *sfn = sanitise_filename(diskp->name);
    char lvl[64];
    assignedhd_t **new_holdp;

    ap_snprintf( lvl, sizeof(lvl), "%d", sched(diskp)->level );

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
        printf("assigning holding disk %s to disk %s:%s, reserved %lu, left %lu\n",
                holdp[i]->disk->diskdir, diskp->host->hostname, diskp->name,
                holdp[i]->reserved, size );
        fflush(stdout);
#endif
	holdp[i] = NULL; /* so it doesn't get free()d... */
    }
    sched(diskp)->holdp[j] = NULL;
    sched(diskp)->destname = newstralloc(sched(diskp)->destname,sched(diskp)->holdp[0]->destname);
    amfree(sfn);

    return l;
}

static void adjust_diskspace(diskp, cmd)
disk_t *diskp;
cmd_t cmd;
{
/* Re-write by Peter Conrad <conrad@opus5.de>, March '99
 * Modifications for splitting dumps across holding disks:
 * Dumpers no longer write more than they've allocated, therefore an
 * adjustment may only free some allocated space.
 * 08/99: Jean-Louis suggested that dumpers tell us how much they've written.
 * We just believe them and don't stat all the files but rely on the used
 * field.
 */

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

static void delete_diskspace(diskp)
disk_t *diskp;
{
/* Re-write by Peter Conrad <conrad@opus5.de>, March '99
 * Modifications for splitting dumps across holding disks:
 * After implementing Jean-Louis' suggestion (see above) this looks much
 * simpler... again, we rely on assignedhd_s containing correct info
 */
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

    unlink_holding_files(holdp[0]->destname); /* no need for the entire list, 
                                       		 because unlink_holding_files
						 will walk through all files
                                       		 using cont_filename */

    free_assignedhd(sched(diskp)->holdp);
    sched(diskp)->holdp = NULL;
    sched(diskp)->act_size = 0;
    amfree(sched(diskp)->destname);
}

assignedhd_t **build_diskspace(destname)
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
    long origsize = 0;
    long dumpsize = 0;
    long dumptime = 0;
    cmd_t cmd;
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

    taper_cmd(PORT_WRITE, dp, NULL, sched(dp)->level, sched(dp)->datestamp);
    cmd = getresult(taper, 1, &result_argc, result_argv, MAX_ARGS+1);
    if(cmd != PORT) {
	printf("driver: did not get PORT from taper for %s:%s\n",
		dp->host->hostname, dp->name);
	fflush(stdout);
	inside_dump_to_tape = 0;
	return 2;	/* fatal problem */
    }
    /* copy port number */
    sched(dp)->destname = newvstralloc(sched(dp)->destname, result_argv[2], NULL );

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

    cmd = getresult(dumper->outfd, 1, &result_argc, result_argv, MAX_ARGS+1);

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

    case DONE: /* DONE <handle> <origsize> <dumpsize> <dumptime> <err str> */
	/* everything went fine */
	origsize = (long)atof(result_argv[3]);
	dumpsize = (long)atof(result_argv[4]);
	dumptime = (long)atof(result_argv[5]);
	break;

    case NO_ROOM: /* NO-ROOM <handle> */
	dumper_cmd(dumper, ABORT, dp);
	cmd = getresult(dumper->outfd, 1, &result_argc, result_argv, MAX_ARGS+1);
	if(cmd != BOGUS)
	    free_serial(result_argv[2]);
	assert(cmd == ABORT_FINISHED);

    case TRYAGAIN: /* TRY-AGAIN <handle> <err str> */
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
    case DONE: /* DONE <handle> <label> <tape file> <err mess> */
	if(result_argc != 5) {
	    error("error [dump to tape DONE result_argc != 5: %d]", result_argc);
	}

	free_serial(result_argv[2]);

	if(failed == 1) goto tryagain;	/* dump didn't work */
	else if(failed == 2) goto fatal;

	/* every thing went fine */
	update_info_dumper(dp, origsize, dumpsize, dumptime);
	filenum = atoi(result_argv[4]);
	update_info_taper(dp, result_argv[3], filenum, sched(dp)->level);
	/* note that update_info_dumper() must be run before
	   update_info_taper(), since update_info_dumper overwrites
	   tape information.  */

	break;

    case TRYAGAIN: /* TRY-AGAIN <handle> <err mess> */
    tryagain:
	update_failed_dump_to_tape(dp);
	free_serial(result_argv[2]);
	headqueue_disk(&runq, dp);
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
    printf(" roomq: %d", queue_length(roomq));
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
    dump_queue("ROOM", roomq, 5, stdout);
    dump_queue("RUN ", runq, 5, stdout);
    printf("================\n");
    fflush(stdout);
}
