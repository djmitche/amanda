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
 * $Id: driverio.c,v 1.69 2004/08/31 12:53:42 martinea Exp $
 *
 * I/O-related functions for driver program
 */
#include "amanda.h"
#include "util.h"
#include "clock.h"
#include "conffile.h"
#include "diskfile.h"
#include "infofile.h"
#include "logfile.h"
#include "token.h"
#include "server_util.h"

#define GLOBAL		/* the global variables defined here */
#include "driverio.h"

int nb_chunker = 0;

static const char *childstr P((int));

void init_driverio()
{
    dumper_t *dumper;

    taper = -1;

    for(dumper = dmptable; dumper < dmptable + MAX_DUMPERS; dumper++) {
	dumper->fd = -1;
    }
}


static const char *
childstr(fd)
    int fd;
{
    static char buf[NUM_STR_SIZE + 32];
    dumper_t *dumper;

    if (fd == taper)
	return ("taper");

    for (dumper = dmptable; dumper < dmptable + MAX_DUMPERS; dumper++) {
	if (dumper->fd == fd)
	    return (dumper->name);
	if (dumper->chunker->fd == fd)
	    return (dumper->chunker->name);
    }
    snprintf(buf, sizeof(buf), "unknown child (fd %d)", fd);
    return (buf);
}


void startup_tape_process(taper_program)
char *taper_program;
{
    int fd[2];

    if(socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == -1)
	error("taper pipe: %s", strerror(errno));
    if(fd[0] < 0 || fd[0] >= FD_SETSIZE) {
	error("taper socketpair 0: descriptor %d out of range (0 .. %d)\n",
	      fd[0], FD_SETSIZE-1);
    }
    if(fd[1] < 0 || fd[1] >= FD_SETSIZE) {
	error("taper socketpair 1: descriptor %d out of range (0 .. %d)\n",
	      fd[1], FD_SETSIZE-1);
    }

    switch(taper_pid = fork()) {
    case -1:
	error("fork taper: %s", strerror(errno));
    case 0:	/* child process */
	aclose(fd[0]);
	if(dup2(fd[1], 0) == -1 || dup2(fd[1], 1) == -1)
	    error("taper dup2: %s", strerror(errno));
	execle(taper_program, "taper", config_name, (char *)0, safe_env());
	error("exec %s: %s", taper_program, strerror(errno));
    default:	/* parent process */
	aclose(fd[1]);
	taper = fd[0];
	taper_ev_read = NULL;
    }
}

void startup_dump_process(dumper, dumper_program)
dumper_t *dumper;
char *dumper_program;
{
    int fd[2];

    if(socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == -1)
	error("%s pipe: %s", dumper->name, strerror(errno));

    switch(dumper->pid = fork()) {
    case -1:
	error("fork %s: %s", dumper->name, strerror(errno));
    case 0:		/* child process */
	aclose(fd[0]);
	if(dup2(fd[1], 0) == -1 || dup2(fd[1], 1) == -1)
	    error("%s dup2: %s", dumper->name, strerror(errno));
	execle(dumper_program,
	       dumper->name ? dumper->name : "dumper",
	       config_name,
	       (char *)0,
	       safe_env());
	error("exec %s (%s): %s", dumper_program,
	      dumper->name, strerror(errno));
    default:	/* parent process */
	aclose(fd[1]);
	dumper->fd = fd[0];
	dumper->ev_read = NULL;
	dumper->busy = dumper->down = 0;
	dumper->dp = NULL;
	fprintf(stderr,"driver: started %s pid %d\n",
		dumper->name, dumper->pid);
	fflush(stderr);
    }
}

void startup_dump_processes(dumper_program, inparallel)
char *dumper_program;
int inparallel;
{
    int i;
    dumper_t *dumper;
    char number[NUM_STR_SIZE];

    for(dumper = dmptable, i = 0; i < inparallel; dumper++, i++) {
	snprintf(number, sizeof(number), "%d", i);
	dumper->name = stralloc2("dumper", number);
	dumper->chunker = &chktable[i];
	chktable[i].name = stralloc2("chunker", number);
	chktable[i].dumper = dumper;
	chktable[i].fd = -1;

	startup_dump_process(dumper, dumper_program);
    }
}

void startup_chunk_process(chunker, chunker_program)
chunker_t *chunker;
char *chunker_program;
{
    int fd[2];

    if(socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == -1)
	error("%s pipe: %s", chunker->name, strerror(errno));

    switch(chunker->pid = fork()) {
    case -1:
	error("fork %s: %s", chunker->name, strerror(errno));
    case 0:		/* child process */
	aclose(fd[0]);
	if(dup2(fd[1], 0) == -1 || dup2(fd[1], 1) == -1)
	    error("%s dup2: %s", chunker->name, strerror(errno));
	execle(chunker_program,
	       chunker->name ? chunker->name : "chunker",
	       config_name,
	       (char *)0,
	       safe_env());
	error("exec %s (%s): %s", chunker_program,
	      chunker->name, strerror(errno));
    default:	/* parent process */
	aclose(fd[1]);
	chunker->down = 0;
	chunker->fd = fd[0];
	chunker->ev_read = NULL;
	fprintf(stderr,"driver: started %s pid %d\n",
		chunker->name, chunker->pid);
	fflush(stderr);
    }
}

cmd_t getresult(fd, show, result_argc, result_argv, max_arg)
int fd;
int show;
int *result_argc;
char **result_argv;
int max_arg;
{
    int arg;
    cmd_t t;
    char *line;

    if((line = areads(fd)) == NULL) {
	if(errno) {
	    error("reading result from %s: %s", childstr(fd), strerror(errno));
	}
	*result_argc = 0;				/* EOF */
    } else {
	*result_argc = split(line, result_argv, max_arg, " ");
    }

    if(show) {
	printf("driver: result time %s from %s:",
	       walltime_str(curclock()),
	       childstr(fd));
	if(line) {
	    for(arg = 1; arg <= *result_argc; arg++) {
		printf(" %s", result_argv[arg]);
	    }
	    putchar('\n');
	} else {
	    printf(" (eof)\n");
	}
	fflush(stdout);
    }
    amfree(line);

#ifdef DEBUG
    printf("argc = %d\n", *result_argc);
    for(arg = 0; arg < *result_argc; arg++)
	printf("argv[%d] = \"%s\"\n", arg, result_argv[arg]);
#endif

    if(*result_argc < 1) return BOGUS;

    for(t = (cmd_t)(BOGUS+1); t < LAST_TOK; t++)
	if(strcmp(result_argv[1], cmdstr[t]) == 0) return t;

    return BOGUS;
}


int taper_cmd(cmd, /* optional */ ptr, destname, level, datestamp)
cmd_t cmd;
void *ptr;
char *destname;
int level;
char *datestamp;
{
    char *cmdline = NULL;
    char number[NUM_STR_SIZE];
    disk_t *dp;
    char *features;

    switch(cmd) {
    case START_TAPER:
	cmdline = vstralloc(cmdstr[cmd], " ", (char *)ptr, "\n", NULL);
	break;
    case FILE_WRITE:
	dp = (disk_t *) ptr;
	snprintf(number, sizeof(number), "%d", level);
	features = am_feature_to_string(dp->host->features);
	cmdline = vstralloc(cmdstr[cmd],
			    " ", disk2serial(dp),
			    " ", destname,
			    " ", dp->host->hostname,
			    " ", features,
			    " ", dp->name,
			    " ", number,
			    " ", datestamp,
			    "\n", NULL);
	amfree(features);
	break;
    case PORT_WRITE:
	dp = (disk_t *) ptr;
	snprintf(number, sizeof(number), "%d", level);
	features = am_feature_to_string(dp->host->features);
	cmdline = vstralloc(cmdstr[cmd],
			    " ", disk2serial(dp),
			    " ", dp->host->hostname,
			    " ", features,
			    " ", dp->name,
			    " ", number,
			    " ", datestamp,
			    "\n", NULL);
	amfree(features);
	break;
    case QUIT:
	cmdline = stralloc2(cmdstr[cmd], "\n");
	break;
    default:
	error("Don't know how to send %s command to taper", cmdstr[cmd]);
    }
    /*
     * Note: cmdline already has a '\n'.
     */
    printf("driver: send-cmd time %s to taper: %s",
	   walltime_str(curclock()), cmdline);
    fflush(stdout);
    if (fullwrite(taper, cmdline, strlen(cmdline)) < 0) {
	printf("writing taper command: %s\n", strerror(errno));
	fflush(stdout);
	amfree(cmdline);
	return 0;
    }
    amfree(cmdline);
    return 1;
}

int dumper_cmd(dumper, cmd, /* optional */ dp)
dumper_t *dumper;
cmd_t cmd;
disk_t *dp;
{
    char *cmdline = NULL;
    char number[NUM_STR_SIZE];
    char numberport[NUM_STR_SIZE];
    char *o;
    int activehd=0;
    assignedhd_t **h=NULL;
    char *device;
    char *features;

    if(dp && sched(dp) && sched(dp)->holdp) {
	h = sched(dp)->holdp;
	activehd = sched(dp)->activehd;
    }

    if(dp && dp->device) {
	device = dp->device;
    }
    else {
	device = "NODEVICE";
    }

    switch(cmd) {
    case PORT_DUMP:
	snprintf(number, sizeof(number), "%d", sched(dp)->level);
	snprintf(numberport, sizeof(numberport), "%d", dumper->output_port);
	features = am_feature_to_string(dp->host->features);
	o = optionstr(dp, dp->host->features, NULL);
	cmdline = vstralloc(cmdstr[cmd],
			    " ", disk2serial(dp),
			    " ", numberport,
			    " ", dp->host->hostname,
			    " ", features,
			    " ", dp->name,
			    " ", device,
			    " ", number,
			    " ", sched(dp)->dumpdate,
			    " ", dp->program,
			    " |", o,
			    "\n", NULL);
	amfree(features);
	amfree(o);
	break;
    case QUIT:
    case ABORT:
	if( dp ) {
	    cmdline = vstralloc(cmdstr[cmd],
				" ", sched(dp)->destname,
				"\n", NULL );
	} else {
	    cmdline = stralloc2(cmdstr[cmd], "\n");
	}
	break;
    default:
	error("Don't know how to send %s command to dumper", cmdstr[cmd]);
    }
    /*
     * Note: cmdline already has a '\n'.
     */
    if(dumper->down) {
	printf("driver: send-cmd time %s ignored to down dumper %s: %s",
	       walltime_str(curclock()), dumper->name, cmdline);
    } else {
	printf("driver: send-cmd time %s to %s: %s",
	       walltime_str(curclock()), dumper->name, cmdline);
	fflush(stdout);
	if (fullwrite(dumper->fd, cmdline, strlen(cmdline)) < 0) {
	    printf("writing %s command: %s\n", dumper->name, strerror(errno));
	    fflush(stdout);
	    amfree(cmdline);
	    return 0;
	}
    }
    amfree(cmdline);
    return 1;
}

int chunker_cmd(chunker, cmd, dp)
chunker_t *chunker;
cmd_t cmd;
disk_t *dp;
{
    char *cmdline = NULL;
    char number[NUM_STR_SIZE];
    char chunksize[NUM_STR_SIZE];
    char use[NUM_STR_SIZE];
    char *o;
    int activehd=0;
    assignedhd_t **h=NULL;
    char *features;

    if(dp && sched(dp) && sched(dp)->holdp) {
	h = sched(dp)->holdp;
	activehd = sched(dp)->activehd;
    }

    switch(cmd) {
    case PORT_WRITE:
	holdalloc(h[activehd]->disk)->allocated_dumpers++;
	snprintf(number, sizeof(number), "%d", sched(dp)->level);
	snprintf(chunksize, sizeof(chunksize), "%ld", h[0]->disk->chunksize);
	snprintf(use, sizeof(use), "%ld", h[0]->reserved );
	features = am_feature_to_string(dp->host->features);
	o = optionstr(dp, dp->host->features, NULL);
	cmdline = vstralloc(cmdstr[cmd],
			    " ", disk2serial(dp),
			    " ", sched(dp)->destname,
			    " ", dp->host->hostname,
			    " ", features,
			    " ", dp->name,
			    " ", number,
			    " ", sched(dp)->dumpdate,
			    " ", chunksize,
			    " ", dp->program,
			    " ", use,
			    " |", o,
			    "\n", NULL);
	amfree(features);
	amfree(o);
	break;
    case CONTINUE:
	if( dp ) {
	    holdalloc(h[activehd]->disk)->allocated_dumpers++;
	    snprintf(chunksize, sizeof(chunksize), "%ld", 
		     h[activehd]->disk->chunksize );
	    snprintf(use, sizeof(use), "%ld", 
		     h[activehd]->reserved - h[activehd]->used );
	    cmdline = vstralloc(cmdstr[cmd],
				" ", disk2serial(dp),
				" ", h[activehd]->destname,
				" ", chunksize,
				" ", use,
				"\n", NULL );
	} else {
	    cmdline = stralloc2(cmdstr[cmd], "\n");
	}
	break;
    case QUIT:
	cmdline = stralloc2(cmdstr[cmd], "\n");
	break;
    case DONE:
    case FAILED:
	if( dp) {
	    cmdline = vstralloc(cmdstr[cmd],
				" ", disk2serial(dp),
				"\n",  NULL);
	} else {
	    cmdline = vstralloc(cmdstr[cmd], "\n");
	}
	break;
    default:
	error("Don't know how to send %s command to chunker", cmdstr[cmd]);
    }
    /*
     * Note: cmdline already has a '\n'.
     */
    printf("driver: send-cmd time %s to %s: %s",
	   walltime_str(curclock()), chunker->name, cmdline);
    fflush(stdout);
    if (fullwrite(chunker->fd, cmdline, strlen(cmdline)) < 0) {
	printf("writing %s command: %s\n", chunker->name, strerror(errno));
	fflush(stdout);
	amfree(cmdline);
	return 0;
    }
    amfree(cmdline);
    return 1;
}

#define MAX_SERIAL MAX_DUMPERS+1	/* one for the taper */

long generation = 1;

struct serial_s {
    long gen;
    disk_t *dp;
} stable[MAX_SERIAL];

disk_t *serial2disk(str)
char *str;
{
    int rc, s;
    long gen;

    rc = sscanf(str, "%d-%ld", &s, &gen);
    if(rc != 2) {
	error("error [serial2disk \"%s\" parse error]", str);
    } else if (s < 0 || s >= MAX_SERIAL) {
	error("error [serial out of range 0..%d: %d]", MAX_SERIAL, s);
    }
    if(gen != stable[s].gen)
	printf("driver: error time %s serial gen mismatch\n",
	       walltime_str(curclock()));
    return stable[s].dp;
}

void free_serial(str)
char *str;
{
    int rc, s;
    long gen;

    rc = sscanf(str, "%d-%ld", &s, &gen);
    if(!(rc == 2 && s >= 0 && s < MAX_SERIAL)) {
	/* nuke self to get core dump for Brett */
	fprintf(stderr, "driver: free_serial: str \"%s\" rc %d s %d\n",
		str, rc, s);
	fflush(stderr);
	abort();
    }

    if(gen != stable[s].gen)
	printf("driver: error time %s serial gen mismatch\n",
	       walltime_str(curclock()));
    stable[s].gen = 0;
    stable[s].dp = NULL;
}


void free_serial_dp(dp)
disk_t *dp;
{
    int s;

    for(s = 0; s < MAX_SERIAL; s++) {
	if(stable[s].dp == dp) {
	    stable[s].gen = 0;
	    return;
	}
    }

    printf("driver: error time %s serial not found\n",
	   walltime_str(curclock()));
}


char *disk2serial(dp)
disk_t *dp;
{
    int s;
    static char str[NUM_STR_SIZE];

    for(s = 0; s < MAX_SERIAL; s++) {
	if(stable[s].dp == dp) {
	    snprintf(str, sizeof(str), "%02d-%05ld", s, stable[s].gen);
	    return str;
	}
    }

    /* find unused serial number */
    for(s = 0; s < MAX_SERIAL; s++)
	if(stable[s].gen == 0 && stable[s].dp == NULL)
	    break;
    if(s >= MAX_SERIAL) {
	printf("driver: error time %s bug: out of serial numbers\n",
	       walltime_str(curclock()));
	s = 0;
    }

    stable[s].gen = generation++;
    stable[s].dp = dp;

    snprintf(str, sizeof(str), "%02d-%05ld", s, stable[s].gen);
    return str;
}

void update_info_dumper(dp, origsize, dumpsize, dumptime)
     disk_t *dp;
     long origsize;
     long dumpsize;
     long dumptime;
{
    int level, i;
    info_t info;
    stats_t *infp;
    perf_t *perfp;
    char *conf_infofile;

    level = sched(dp)->level;

    conf_infofile = getconf_str(CNF_INFOFILE);
    if (*conf_infofile == '/') {
	conf_infofile = stralloc(conf_infofile);
    } else {
	conf_infofile = stralloc2(config_dir, conf_infofile);
    }
    if (open_infofile(conf_infofile)) {
	error("could not open info db \"%s\"", conf_infofile);
    }
    amfree(conf_infofile);

    get_info(dp->host->hostname, dp->name, &info);

    /* Clean up information about this and higher-level dumps.  This
       assumes that update_info_dumper() is always run before
       update_info_taper(). */
    for (i = level; i < DUMP_LEVELS; ++i) {
      infp = &info.inf[i];
      infp->size = -1;
      infp->csize = -1;
      infp->secs = -1;
      infp->date = (time_t)-1;
      infp->label[0] = '\0';
      infp->filenum = 0;
    }

    /* now store information about this dump */
    infp = &info.inf[level];
    infp->size = origsize;
    infp->csize = dumpsize;
    infp->secs = dumptime;
    infp->date = sched(dp)->timestamp;

    if(level == 0) perfp = &info.full;
    else perfp = &info.incr;

    /* Update the stats, but only if the new values are meaningful */
    if(dp->compress != COMP_NONE && origsize > 0L) {
	newperf(perfp->comp, dumpsize/(float)origsize);
    }
    if(dumptime > 0L) {
	if(dumptime >= dumpsize)
	    newperf(perfp->rate, 1);
	else
	    newperf(perfp->rate, dumpsize/dumptime);
    }

    if(getconf_int(CNF_RESERVE)<100) {
	info.command = NO_COMMAND;
    }

    if(level == info.last_level)
	info.consecutive_runs++;
    else {
	info.last_level = level;
	info.consecutive_runs = 1;
    }

    for(i=NB_HISTORY-1;i>0;i--) {
	info.history[i] = info.history[i-1];
    }

    info.history[0].level = level;
    info.history[0].size  = origsize;
    info.history[0].csize = dumpsize;
    info.history[0].date  = sched(dp)->timestamp;

    if(put_info(dp->host->hostname, dp->name, &info))
	error("infofile update failed (%s,%s)\n", dp->host->hostname, dp->name);

    close_infofile();
}

void update_info_taper(dp, label, filenum, level)
disk_t *dp;
char *label;
int filenum;
int level;
{
    info_t info;
    stats_t *infp;
    int rc;

    rc = open_infofile(getconf_str(CNF_INFOFILE));
    if(rc)
	error("could not open infofile %s: %s (%d)", getconf_str(CNF_INFOFILE),
	      strerror(errno), rc);

    get_info(dp->host->hostname, dp->name, &info);

    infp = &info.inf[level];
    /* XXX - should we record these two if no-record? */
    strncpy(infp->label, label, sizeof(infp->label)-1);
    infp->label[sizeof(infp->label)-1] = '\0';
    infp->filenum = filenum;

    info.command = NO_COMMAND;

    if(put_info(dp->host->hostname, dp->name, &info))
	error("infofile update failed (%s,%s)\n", dp->host->hostname, dp->name);

    close_infofile();
}

/* Free an array of pointers to assignedhd_t after freeing the
 * assignedhd_t themselves. The array must be NULL-terminated.
 */
void free_assignedhd( ahd )
assignedhd_t **ahd;
{
    int i;

    if( !ahd ) { return; }

    for( i = 0; ahd[i]; i++ ) {
	amfree(ahd[i]->destname);
	amfree(ahd[i]);
    }
    amfree(ahd);
}
