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
 * $Id: amflush.c,v 1.11 1997/11/11 06:39:43 amcore Exp $
 *
 * write files from work directory onto tape
 */
#include "amanda.h"

#include "conffile.h"
#include "diskfile.h"
#include "tapefile.h"
#include "logfile.h"
#include "clock.h"
#include "version.h"
#include "holding.h"

#define MAX_ARGS 10
#define MAX_LINE 1024

/* define schedule structure */

typedef struct sched_s {
    int level;			/* dump level */
    char destname[128];		/* file name */
} sched_t;

#define sched(dp)	((sched_t *) (dp)->up)

/* command/result tokens */

typedef enum {
    BOGUS, QUIT, DONE,
    FILE_DUMP, PORT_DUMP, CONTINUE, ABORT,		/* dumper cmds */
    FAILED, TRYAGAIN, NO_ROOM, ABORT_FINISHED,		/* dumper results */
    START_TAPER, FILE_WRITE, PORT_WRITE,		/* taper cmds */
    PORT, TAPE_ERROR, TAPER_OK,				/* taper results */
    LAST_TOK
} tok_t;

char *cmdstr[] = {
    "BOGUS", "QUIT", "DONE",
    "FILE-DUMP", "PORT-DUMP", "CONTINUE", "ABORT",	/* dumper cmds */
    "FAILED", "TRY-AGAIN", "NO-ROOM", "ABORT-FINISHED",	/* dumper results */
    "START-TAPER", "FILE-WRITE", "PORT-WRITE",		/* taper cmds */
    "PORT", "TAPE-ERROR", "TAPER-OK",			/* taper results */
    NULL
};

tok_t tok;

char *pname = "amflush";

int taper, taper_pid;

disklist_t *diskqp;

int result_argc;
char *result_argv[MAX_ARGS];
static char *config;
char datestamp[80];
char confdir[1024];
char taper_program[80], reporter_program[80];

/* local functions */
int main P((int argc, char **argv));
void flush_holdingdisk P((char *diskdir));
static void startup_tape_process P((void));
tok_t getresult P((int fd));
void taper_cmd P((tok_t cmd, void *ptr, char *destname, int level));
void confirm P((void));
void detach P((void));
void run_dumps P((void));


int main(argc, argv)
int argc;
char **argv;
{
    int foreground;
    struct passwd *pw;
    char *dumpuser;

    erroutput_type = ERR_INTERACTIVE;
    foreground = 0;

    if(argc > 1 && !strcmp(argv[1], "-f")) {
	foreground = 1;
	argc--,argv++;
    }

    if(argc != 2)
	error("Usage: amflush%s [-f] <confdir>", versionsuffix());

    config = argv[1];
    sprintf(confdir, "%s/%s", CONFIG_DIR, argv[1]);
    if(chdir(confdir) != 0)
	error("could not cd to confdir %s: %s",	confdir, strerror(errno));

    if(read_conffile(CONFFILE_NAME))
	error("could not read amanda config file\n");

    if((diskqp = read_diskfile(getconf_str(CNF_DISKFILE))) == NULL)
	error("could not read disklist file\n");

    if(read_tapelist(getconf_str(CNF_TAPELIST)))
	error("parse error in %s", getconf_str(CNF_TAPELIST));

    dumpuser = getconf_str(CNF_DUMPUSER);
    if((pw = getpwnam(dumpuser)) == NULL)
	error("dumpuser %s not found in password file", dumpuser);
    if(pw->pw_uid != getuid())
	error("must run amflush as user %s", dumpuser);

    sprintf(taper_program, "%s/taper%s", libexecdir, versionsuffix());
    sprintf(reporter_program, "%s/reporter%s", libexecdir, versionsuffix());

    pick_datestamp();
    confirm();
    if(!foreground) detach();
    erroutput_type = (ERR_AMANDALOG|ERR_INTERACTIVE);
    run_dumps();
    return 0;
}


void confirm()
/* confirm before detaching and running */
{
    tape_t *tp;
    char *tpchanger;

    printf("\nFlushing dumps in %s ", datestamp);
    tpchanger = getconf_str(CNF_TPCHANGER);
    if(*tpchanger != '\0') printf("using tape changer \"%s\".\n", tpchanger);
    else printf("to tape drive %s.\n", getconf_str(CNF_TAPEDEV));

    printf("Expecting ");
    tp = lookup_tapepos(getconf_int(CNF_TAPECYCLE));
    if(tp != NULL) printf("tape %s or ", tp->label);
    printf("a new tape.");
    tp = lookup_tapepos(1);
    if(tp != NULL) printf("  (The last dumps were to tape %s)", tp->label);

    printf("\nAre you sure you want to do this? ");
    if(get_letter_from_user() == 'Y') return;

    printf("Ok, quitting.  Run amflush again when you are ready.\n");
    exit(1);
}

void detach()
{
    int fd;

    fflush(stdout); fflush(stderr);
    if((fd = open("/dev/null", O_RDWR, 0666)) == -1)
	error("could not open /dev/null: %s", strerror(errno));

    switch(fork()) {
    case -1: error("could not fork: %s", strerror(errno));
    case 0:
	dup2(fd,0);
	dup2(fd,1);
	dup2(fd,2);
	close(fd);
	setsid();
	return;
    }

    puts("Running in background, you can log off now.");
    puts("You'll get mail when amflush is finished.");
    exit(0);
}


void flush_holdingdisk(diskdir)
char *diskdir;
{
    DIR *workdir;
    struct dirent *entry;
    char dirname[80], destname[128], hostname[256], diskname[80];
    int level;
    disk_t *dp;

    sprintf(dirname, "%s/%s", diskdir, datestamp);

    if((workdir = opendir(dirname)) == NULL) {
	log(L_INFO, "%s: could not open working dir: %s",
	    dirname, strerror(errno));
	return;
    }
    chdir(dirname);

    while((entry = readdir(workdir)) != NULL) {
	if(!strcmp(entry->d_name, ".") ||  !strcmp(entry->d_name, ".."))
	    continue;

	if(is_emptyfile(entry->d_name)) {
	    if(unlink(entry->d_name) == -1)
		log(L_INFO,"%s: ignoring zero length file.", entry->d_name);
	    else
		log(L_INFO,"%s: removed zero length file.", entry->d_name);
	    continue;
	}

	sprintf(destname, "%s/%s", dirname, entry->d_name);

	if(get_amanda_names(destname, hostname, diskname, &level)) {
	    log(L_INFO, "%s: ignoring cruft file.", entry->d_name);
	    continue;
	}

	dp = lookup_disk(hostname, diskname);

	if (dp == NULL) {
	    log(L_INFO, "%s: disk %s:%s not in database, skipping it.",
		entry->d_name, hostname, diskname);
	    continue;
	}

	if(level < 0 || level > 9) {
	    log(L_INFO, "%s: ignoring file with bogus dump level %d.",
		entry->d_name, level);
	    continue;
	}

	taper_cmd(FILE_WRITE, dp, destname, level);
	tok = getresult(taper);
	if(tok == TRYAGAIN) {
	    /* we'll retry one time */
	    taper_cmd(FILE_WRITE, dp, destname, level);
	    tok = getresult(taper);
	}

	switch(tok) {
	case DONE:
	    unlink(destname);
	    break;
	case TRYAGAIN:
	    log(L_WARNING, "%s: too many taper retries, leaving file on disk",
		destname);
	    break;
	default:
	    log(L_WARNING, "%s: taper error, leaving file on disk",
		destname);
	    break;
	}
    }

    closedir(workdir);

    /* try to zap the now (hopefully) empty working dir */
    chdir(confdir);
    if(rmdir(dirname))
	log(L_WARNING, "Could not rmdir %s.  Check for cruft.",
	    dirname);
}

void run_dumps()
{
    holdingdisk_t *hdisk;

    startclock();
    log(L_START, "date %s", datestamp);

    chdir(confdir);
    startup_tape_process();
    taper_cmd(START_TAPER, datestamp, NULL, 0);
    tok = getresult(taper);

    if(tok != TAPER_OK) {
	/* forget it */
	sleep(5);	/* let taper log first, but not really necessary */
	log(L_ERROR, "Cannot flush without tape.  Try again.");
	log(L_FINISH, "date %s time %s", datestamp, walltime_str(curclock()));
    }
    else {

	for(hdisk = holdingdisks; hdisk != NULL; hdisk = hdisk->next)
	    flush_holdingdisk(hdisk->diskdir);

	/* tell taper to quit, then wait for it */
	taper_cmd(QUIT, NULL, NULL, 0);
	while(wait(NULL) != -1);

    }

    log(L_FINISH, "date %s time %s", datestamp, walltime_str(curclock()));

    /* now, have reporter generate report and send mail */

    chdir(confdir);
    execl(reporter_program, "reporter", (char *)0);
}

static void startup_tape_process()
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
    }
}

char line[MAX_LINE];

tok_t getresult(fd)
int fd;
{
    char *p;
    int arg, len;
    tok_t t;

    if((len = read(fd, line, MAX_LINE)) == -1)
	error("reading result from taper: %s", strerror(errno));

    line[len] = '\0';

    p = line;
    result_argc = 0;
    while(*p) {
	while(isspace(*p)) p++;
	if(result_argc < MAX_ARGS) result_argv[result_argc++] = p;
	while(*p && !isspace(*p)) p++;
	if(*p) *p++ = '\0';
    }
    for(arg = result_argc; arg < MAX_ARGS; arg++) result_argv[arg] = "";

#ifdef DEBUG
    printf("argc = %d\n", result_argc);
    for(arg = 0; arg < MAX_ARGS; arg++)
	printf("argv[%d] = \"%s\"\n", arg, result_argv[arg]);
#endif

    for(t = BOGUS+1; t < LAST_TOK; t++)
	if(!strcmp(result_argv[0], cmdstr[t])) return t;

    return BOGUS;
}


void taper_cmd(cmd, /* optional */ ptr, destname, level)
tok_t cmd;
void *ptr;
char *destname;
int level;
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
	sprintf(cmdline, "FILE-WRITE handle %s %s %s %d\n",
		destname, dp->host->hostname, dp->name,
		level);
	break;
    case PORT_WRITE:
	dp = (disk_t *) ptr;
	sprintf(cmdline, "PORT-WRITE handle %s %s %d\n",
		dp->host->hostname, dp->name, level);
	break;
    case QUIT:
	sprintf(cmdline, "QUIT\n");
	break;
    default:
	assert(0);
    }
    len = strlen(cmdline);
    if(write(taper, cmdline, len) < len)
	error("writing taper command");
}

