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
 * $Id: driverio.c,v 1.40 1999/02/16 03:18:42 martinea Exp $
 *
 * I/O-related functions for driver program
 */
#include "amanda.h"
#include "clock.h"
#include "conffile.h"
#include "diskfile.h"
#include "infofile.h"
#include "logfile.h"
#include "token.h"

#define GLOBAL		/* the global variables defined here */
#include "driverio.h"

char *cmdstr[] = {
    "BOGUS", "QUIT", "DONE",
    "FILE-DUMP", "PORT-DUMP", "CONTINUE", "ABORT",	/* dumper cmds */
    "FAILED", "TRY-AGAIN", "NO-ROOM", "ABORT-FINISHED",	/* dumper results */
    "FATAL-TRY-AGAIN",
    "START-TAPER", "FILE-WRITE", "PORT-WRITE",		/* taper cmds */
    "PORT", "TAPE-ERROR", "TAPER-OK",			/* taper results */
    NULL
};

void addfd(fd)
int fd;
{
    if(fd < 0 || fd >= FD_SETSIZE) {
	error("addfd: descriptor %d out of range (0 .. %d)\n",
	      fd, FD_SETSIZE-1);
    }
    FD_SET(fd, &readset);
    if(fd > maxfd) maxfd = fd;
}

char *childstr(fd)
int fd;
{
    static char *str = NULL;
    char fd_str[NUM_STR_SIZE];
    dumper_t *dumper;

    if(fd == taper) return "taper";

    for(dumper = dmptable; dumper < dmptable+inparallel; dumper++)
	if(dumper->outfd == fd) return dumper->name;

    ap_snprintf(fd_str, sizeof(fd_str), "%d", fd);
    str = newvstralloc(str, "unknown child (fd ", fd_str, ")", NULL);
    return str;
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
	execle(taper_program, "taper", (char *)0, safe_env());
	error("exec %s: %s", taper_program, strerror(errno));
    default:	/* parent process */
	aclose(fd[1]);
	taper = fd[0];
	addfd(taper);
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
	execle(dumper_program, "dumper", (char *)0, safe_env());
	error("exec %s (%s): %s", dumper_program,
	      dumper->name, strerror(errno));
    default:	/* parent process */
	aclose(fd[1]);
	dumper->infd = dumper->outfd = fd[0];
	addfd(dumper->outfd);
	dumper->busy = dumper->down = 0;
	dumper->dp = NULL;
	fprintf(stderr,"driver: started %s pid %d\n",
		dumper->name, dumper->pid);
	fflush(stderr);
    }
}

void startup_dump_processes(dumper_program)
char *dumper_program;
{
    int i;
    dumper_t *dumper;
    char number[NUM_STR_SIZE];

    for(dumper = dmptable, i = 0; i < inparallel; dumper++, i++) {
	ap_snprintf(number, sizeof(number), "%d", i);
	dumper->name = stralloc2("dumper", number);

	startup_dump_process(dumper, dumper_program);
    }
}

tok_t getresult(fd, show, result_argc, result_argv, max_arg)
int fd;
int show;
int *result_argc;
char **result_argv;
int max_arg;
{
    int arg;
    tok_t t;
    char *line;

    if((line = areads(fd)) == NULL) {
	if(errno) {
	    error("reading result from %s: %s", childstr(fd), strerror(errno));
	}
	*result_argc = 0;				/* EOF */
    } else {
	*result_argc = split(line, result_argv, max_arg, " ");
    }
    amfree(line);

    if(show) {
	printf("driver: result time %s from %s:",
	       walltime_str(curclock()),
	       childstr(fd));
	for(arg = 1; arg <= *result_argc; arg++)
	    printf(" %s", result_argv[arg]);
	printf("\n");
	fflush(stdout);
    }

#ifdef DEBUG
    printf("argc = %d\n", *result_argc);
    for(arg = 0; arg < max_arg; arg++)
	printf("argv[%d] = \"%s\"\n", arg, result_argv[arg]);
#endif

    if(*result_argc < 1) return BOGUS;

    for(t = BOGUS+1; t < LAST_TOK; t++)
	if(strcmp(result_argv[1], cmdstr[t]) == 0) return t;

    return BOGUS;
}


void taper_cmd(cmd, /* optional */ ptr, destname, level, datestamp)
tok_t cmd;
void *ptr;
char *destname;
int level;
char *datestamp;
{
    char *cmdline = NULL;
    char number[NUM_STR_SIZE];
    disk_t *dp;
    int l, n, s;

    switch(cmd) {
    case START_TAPER:
	cmdline = vstralloc("START-TAPER ", (char *)ptr, "\n", NULL);
	break;
    case FILE_WRITE:
	dp = (disk_t *) ptr;
	ap_snprintf(number, sizeof(number), "%d", level);
	cmdline = vstralloc("FILE-WRITE",
			    " ", disk2serial(dp),
			    " ", destname,
			    " ", dp->host->hostname,
			    " ", dp->name,
			    " ", number,
			    " ", datestamp,
			    "\n", NULL);
	break;
    case PORT_WRITE:
	dp = (disk_t *) ptr;
	ap_snprintf(number, sizeof(number), "%d", level);
	cmdline = vstralloc("PORT-WRITE",
			    " ", disk2serial(dp),
			    " ", dp->host->hostname,
			    " ", dp->name,
			    " ", number,
			    " ", datestamp,
			    "\n", NULL);
	break;
    case QUIT:
	cmdline = stralloc("QUIT\n");
	break;
    default:
	assert(0);
    }
    /*
     * Note: cmdline already has a '\n'.
     */
    printf("driver: send-cmd time %s to taper: %s",
	   walltime_str(curclock()), cmdline);
    fflush(stdout);
    for(l = 0, n = strlen(cmdline); l < n; l += s) {
	if((s = write(taper, cmdline + l, n - l)) < 0) {
	    error("writing taper command: %s", strerror(errno));
	}
    }
    amfree(cmdline);
}

void dumper_cmd(dumper, cmd, /* optional */ dp)
dumper_t *dumper;
tok_t cmd;
disk_t *dp;
{
    char *cmdline = NULL;
    char number[NUM_STR_SIZE];
    char chunksize[NUM_STR_SIZE];
    int l, n, s;
    char *o;

    switch(cmd) {
    case FILE_DUMP:
	ap_snprintf(number, sizeof(number), "%d", sched(dp)->level);
	ap_snprintf(chunksize, sizeof(chunksize), "%ld",
		    (long)sched(dp)->holdp->chunksize);
	o = optionstr(dp);
	cmdline = vstralloc(cmdstr[cmd],
			    " ", disk2serial(dp),
			    " ", sched(dp)->destname,
			    " ", dp->host->hostname,
			    " ", dp->name,
			    " ", number,
			    " ", sched(dp)->dumpdate,
			    " ", chunksize,
			    " ", dp->program,
			    " |", o,
			    "\n", NULL);
	amfree(o);
	break;
    case PORT_DUMP:
	ap_snprintf(number, sizeof(number), "%d", sched(dp)->level);
	o = optionstr(dp);
	cmdline = vstralloc(cmdstr[cmd],
			    " ", disk2serial(dp),
			    " ", sched(dp)->destname,
			    " ", dp->host->hostname,
			    " ", dp->name,
			    " ", number,
			    " ", sched(dp)->dumpdate,
			    " ", dp->program,
			    " |", o,
			    "\n", NULL);
	amfree(o);
	break;
    case QUIT:
    case ABORT:
    case CONTINUE:
	cmdline = stralloc2(cmdstr[cmd], "\n");
	break;
    default:
	assert(0);
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
	for(l = 0, n = strlen(cmdline); l < n; l += s) {
	    if((s = write(dumper->infd, cmdline + l, n - l)) < 0) {
		error("writing %s command: %s", dumper->name, strerror(errno));
	    }
	}
    }
    amfree(cmdline);
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
}


char *disk2serial(dp)
disk_t *dp;
{
    int s;
    static char str[NUM_STR_SIZE];

    /* find unused serial number */
    for(s = 0; s < MAX_SERIAL; s++) if(stable[s].gen == 0) break;
    if(s >= MAX_SERIAL) {
	printf("driver: error time %s bug: out of serial numbers\n",
	       walltime_str(curclock()));
	s = 0;
    }

    stable[s].gen = generation++;
    stable[s].dp = dp;

    ap_snprintf(str, sizeof(str), "%02d-%05ld", s, stable[s].gen);
    return str;
}

void update_info_dumper(dp, origsize, dumpsize, dumptime)
disk_t *dp;
long origsize;
long dumpsize;
long dumptime;
{
    int level;
    info_t info;
    stats_t *infp;
    perf_t *perfp;
    int rc;

    level = sched(dp)->level;

    rc = open_infofile(getconf_str(CNF_INFOFILE));
    if(rc)
	error("could not open infofile %s: %s (%d)", getconf_str(CNF_INFOFILE),
	      strerror(errno), rc);

    get_info(dp->host->hostname, dp->name, &info);

    infp = &info.inf[level];
    infp->size = origsize;
    infp->csize = dumpsize;
    infp->secs = dumptime;
/*GS    if(dp->record)*/ infp->date = sched(dp)->timestamp;

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
    if(put_info(dp->host->hostname, dp->name, &info))
	error("infofile update failed (%s,%s)\n", dp->host->hostname, dp->name);

    close_infofile();
}

void update_info_taper(dp, label, filenum)
disk_t *dp;
char *label;
int filenum;
{
    int level;
    info_t info;
    stats_t *infp;
    int rc;

    level = sched(dp)->level;

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
