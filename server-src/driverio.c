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
 * driver.c - controlling process for the Amanda backup system
 */
#include "amanda.h"
#include "clock.h"
#include "conffile.h"
#include "diskfile.h"
#include "infofile.h"
#include "logfile.h"

#define GLOBAL		/* the global variables defined here */
#include "driver.h"

char *cmdstr[] = {
    "BOGUS", "QUIT", "DONE",
    "FILE-DUMP", "PORT-DUMP", "CONTINUE", "ABORT",	/* dumper cmds */
    "FAILED", "TRY-AGAIN", "NO-ROOM", "ABORT-FINISHED",	/* dumper results */
    "FATAL-TRY-AGAIN",
    "START-TAPER", "FILE-WRITE", "PORT-WRITE", 		/* taper cmds */
    "PORT", "TAPE-ERROR", "TAPER-OK",			/* taper results */
    NULL
};

char *pname = "driver";

int main(main_argc, main_argv)
int main_argc;
char **main_argv;
{
    erroutput_type = (ERR_AMANDALOG|ERR_INTERACTIVE);
    return driver_main(main_argc, main_argv);
}

void addfd(fd)
int fd;
{
    FD_SET(fd, &readset);
    if(fd > maxfd) maxfd = fd;
}

char *childstr(fd)
int fd;
{
    static char str[80];
    dumper_t *dumper;

    if(fd == taper) return "taper";

    for(dumper = dmptable; dumper < dmptable+inparallel; dumper++)
	if(dumper->outfd == fd) {
	    sprintf(str, "dumper%ld", (long)(dumper-dmptable));
	    return str;
	}

    sprintf(str, "unknown child (fd %d)", fd);
    return str;
}


void startup_tape_process()
{
    int fd[2];

    if(socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == -1)
	error("taper pipe: %s", strerror(errno));

    switch(taper_pid = fork()) {
    case -1:
	error("fork taper: %s", strerror(errno));
    case 0:	/* child process */
	close(fd[0]);
	if(dup2(fd[1], 0) == -1 || dup2(fd[1], 1) == -1)
	    error("taper dup2: %s", strerror(errno));
	execl(taper_program, "taper", (char *)0);
	error("exec %s: %s", taper_program, strerror(errno));
    default:	/* parent process */
	close(fd[1]);
	taper = fd[0];
	addfd(taper);
    }
}

void startup_dump_process(dumper)
dumper_t *dumper;
{
    int fd[2];

    if(socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == -1)
	error("dumper%d pipe: %s", dumper-dmptable, strerror(errno));

    switch(dumper->pid = fork()) {
    case -1:
	error("fork dumper%d: %s", dumper-dmptable, strerror(errno));
    case 0:		/* child process */
	close(fd[0]);
	if(dup2(fd[1], 0) == -1 || dup2(fd[1], 1) == -1)
	    error("dumper%d dup2: %s", dumper-dmptable, strerror(errno));
	execl(dumper_program, "dumper", (char *)0);
	error("exec %s (dumper%d): %s",dumper_program,
	      dumper-dmptable,strerror(errno));
    default:	/* parent process */
	close(fd[1]);
	dumper->infd = dumper->outfd = fd[0];
	addfd(dumper->outfd);
	dumper->busy = dumper->down = 0;
	dumper->dp = NULL;
	fprintf(stderr,"driver: started dumper%ld pid %d\n", 
		(long)(dumper-dmptable), dumper->pid);
	fflush(stderr);
    }
}

void startup_dump_processes()
{
    int i;
    dumper_t *dumper;

    for(dumper = dmptable, i = 0; i < inparallel; dumper++, i++)
	startup_dump_process(dumper);
}

char line[MAX_LINE];

tok_t getresult(fd)
int fd;
{
    char *p; 
    int arg, len;
    tok_t t;

    if((len = read(fd, line, MAX_LINE)) == -1)
	error("reading result from %s: %s", childstr(fd), strerror(errno));

    line[len] = '\0';

    argc = split(line, argv, MAX_ARGS+1, " ");

    printf("driver: result time %s from %s:",
	   walltime_str(curclock()),
	   childstr(fd)); 
    for(arg = 1; arg <= argc; arg++)
	printf(" %s", argv[arg]);
    printf("\n");
    fflush(stdout);

#ifdef DEBUG
    printf("argc = %d\n", argc);
    for(arg = 0; arg < MAX_ARGS+1; arg++)
        printf("argv[%d] = \"%s\"\n", arg, argv[arg]);
#endif

    if(argc < 1) return BOGUS;

    for(t = BOGUS+1; t < LAST_TOK; t++)
	if(!strcmp(argv[1], cmdstr[t])) return t;
    
    return BOGUS;
}


void taper_cmd(cmd, /* optional */ ptr)
tok_t cmd;
void *ptr;
{
    char cmdline[MAX_LINE];
    disk_t *dp;
    int len;

    switch(cmd) {
    case START_TAPER:
	sprintf(cmdline, "START-TAPER %s\n", (char *) ptr);
	break;
    case FILE_WRITE:
	dp = (disk_t *) ptr;
	sprintf(cmdline, "FILE-WRITE %s %s %s %s %d\n", disk2serial(dp),
		sched(dp)->destname, dp->host->hostname, dp->name,
		sched(dp)->level);
	break;
    case PORT_WRITE:
	dp = (disk_t *) ptr;
	sprintf(cmdline, "PORT-WRITE %s %s %s %d\n", disk2serial(dp),
		dp->host->hostname, dp->name, sched(dp)->level);
	break;
    case QUIT:
	sprintf(cmdline, "QUIT\n");
	break;
    default:
	assert(0);
    }
    len = strlen(cmdline);
printf("driver: send-cmd time %s to taper: %*.*s\n", 
       walltime_str(curclock()),
       len-1, len-1, cmdline);
fflush(stdout);    
    if(write(taper, cmdline, len) < len)
	error("writing taper command");
}

char *optionstr(dp)
disk_t *dp;
{
    static char str[512];

    strcpy(str,";");

    if(dp->dtype->auth == AUTH_BSD) strcat(str, "bsd-auth;");
    else if(dp->dtype->auth == AUTH_KRB4) {
	strcat(str, "krb4-auth;");
	if(dp->dtype->kencrypt) strcat(str, "kencrypt;");
    }

    if(dp->dtype->srvcompress) strcat(str, "srvcompress;");
    else if(dp->dtype->compress_best) strcat(str, "compress-best;");
    else if(dp->dtype->compress_fast) strcat(str, "compress-fast;");
    if(!dp->dtype->record) strcat(str,"no-record;");
    if(dp->dtype->index) strcat(str,"index;");
    if(dp->dtype->exclude) strcat(str, dp->dtype->exclude);

    return str;
}

void dumper_cmd(dumper, cmd, /* optional */ dp)
dumper_t *dumper;
tok_t cmd;
disk_t *dp;
{
    char cmdline[MAX_LINE];
    int len;
    int lev;

    switch(cmd) {
    case FILE_DUMP:
    case PORT_DUMP:
	lev = sched(dp)->level;
	sprintf(cmdline, "%s %s %s %s %s %d %s %s |%s\n", cmdstr[cmd],
		disk2serial(dp), sched(dp)->destname, dp->host->hostname,
		dp->name, lev, sched(dp)->dumpdate, dp->dtype->program,
		optionstr(dp));
	break;
    case QUIT:
    case ABORT:
    case CONTINUE:
	sprintf(cmdline, "%s\n", cmdstr[cmd]);
	break;
    default:
	assert(0);
    }
    len = strlen(cmdline);
printf("driver: send-cmd time %s to dumper%ld: %*.*s\n", 
       walltime_str(curclock()), (long)(dumper-dmptable),
       len-1, len-1, cmdline);
fflush(stdout);
    if(write(dumper->infd, cmdline, len) < len)
	error("writing dumper%d command: %s", dumper-dmptable, strerror(errno));
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
    assert(rc == 2 && s >= 0 && s < MAX_SERIAL);
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
	kill(getpid(), SIGSEGV);
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
    static char str[80];

    /* find unused serial number */
    for(s = 0; s < MAX_SERIAL; s++) if(stable[s].gen == 0) break;
    if(s >= MAX_SERIAL) {
	printf("driver: error time %s bug: out of serial numbers\n",
	       walltime_str(curclock()));
	s = 0;
    }

    stable[s].gen = generation++;
    stable[s].dp = dp;

    sprintf(str, "%02d-%05ld", s, stable[s].gen);
    return str;
}

void update_info_dumper(dp, origsize, dumpsize, dumptime)
disk_t *dp;
long origsize;
long dumpsize;
long dumptime;
{
    int level;
    info_t inf;
    stats_t *infp;
    perf_t *perfp;
    int rc;

    level = sched(dp)->level;

    rc = open_infofile(getconf_str(CNF_INFOFILE));
    if(rc)
	error("could not open infofile %s: %s (%d)", getconf_str(CNF_INFOFILE),
	      strerror(errno), rc);

    get_info(dp->host->hostname, dp->name, &inf);
	
    infp = &inf.inf[level];
    infp->size = origsize;
    infp->csize = dumpsize;
    infp->secs = dumptime;
    if(dp->dtype->record) infp->date = sched(dp)->timestamp;

    if(level == 0) perfp = &inf.full;
    else perfp = &inf.incr;
    newperf(perfp->comp, origsize? (dumpsize/(float)origsize) : 1.0);
    newperf(perfp->rate, dumpsize/(infp->secs? infp->secs : 1.0));

    if(put_info(dp->host->hostname, dp->name, &inf))
	error("infofile update failed (%s,%s)\n", dp->host->hostname, dp->name);

    close_infofile();
}

void update_info_taper(dp, label, filenum)
disk_t *dp;
char *label;
int filenum;
{
    info_t inf;
    int level;
    int rc;

    level = sched(dp)->level;

    rc = open_infofile(getconf_str(CNF_INFOFILE));
    if(rc)
	error("could not open infofile %s: %s (%d)", getconf_str(CNF_INFOFILE),
	      strerror(errno), rc);

    get_info(dp->host->hostname, dp->name, &inf);

    /* XXX - should we record these two if no-record? */
    strcpy(inf.inf[level].label, label);
    inf.inf[level].filenum = filenum;

    inf.command = NO_COMMAND;

    if(put_info(dp->host->hostname, dp->name, &inf))
	error("infofile update failed (%s,%s)\n", dp->host->hostname, dp->name);

    close_infofile();
}
