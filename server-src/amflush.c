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
 * $Id: amflush.c,v 1.72 2002/04/13 19:24:51 jrjackson Exp $
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
#include "driverio.h"
#include "server_util.h"

char *config_name = NULL;
char *config_dir = NULL;
static char *conf_logdir;
FILE *driver_stream;
char *driver_program;
char *reporter_program;
char *logroll_program;
char *datestamp;
sl_t *datestamp_list;

/* local functions */
int main P((int main_argc, char **main_argv));
void flush_holdingdisk P((char *diskdir, char *datestamp));
void confirm P((void));
void detach P((void));
void run_dumps P((void));
static int get_letter_from_user P((void));

int main(main_argc, main_argv)
int main_argc;
char **main_argv;
{
    int foreground;
    struct passwd *pw;
    char *dumpuser;
    char **datearg = NULL;
    int nb_datearg = 0;
    int fd;
    char *conffile;
    char *conf_diskfile;
    char *conf_tapelist;
    char *conf_logfile;
    disklist_t diskq;
    disk_t *dp;
    pid_t pid;
    pid_t driver_pid, reporter_pid;
    amwait_t exitcode;
    int opt;
    dumpfile_t file;
    sl_t *holding_list=NULL;
    sle_t *holding_file;
    int driver_pipe[2];

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

    set_pname("amflush");

    signal(SIGPIPE, SIG_IGN);

    erroutput_type = ERR_INTERACTIVE;
    foreground = 0;

    /* process arguments */

    while((opt = getopt(main_argc, main_argv, "fD:")) != EOF) {
	switch(opt) {
	case 'f': foreground = 1;
		  break;
	case 'D': if (datearg == NULL)
		      datearg = alloc(21*sizeof(char *));
		  if(nb_datearg == 20) {
		      fprintf(stderr,"maximum of 20 -D arguments.\n");
		      exit(1);
		  }
		  datearg[nb_datearg++] = stralloc(optarg);
		  datearg[nb_datearg] = NULL;
		  break;
	}
    }
    main_argc -= optind, main_argv += optind;

    if(main_argc < 1)
	error("Usage: amflush%s [-f] [-D date]* <confdir> [host [disk]* ]*", versionsuffix());

    config_name = main_argv[0];
    config_dir = vstralloc(CONFIG_DIR, "/", config_name, "/", NULL);

    conffile = stralloc2(config_dir, CONFFILE_NAME);
    if(read_conffile(conffile))
	error("errors processing config file \"%s\"", conffile);
    amfree(conffile);

    conf_diskfile = getconf_str(CNF_DISKFILE);
    if (*conf_diskfile == '/') {
	conf_diskfile = stralloc(conf_diskfile);
    } else {
	conf_diskfile = stralloc2(config_dir, conf_diskfile);
    }
    if (read_diskfile(conf_diskfile, &diskq) < 0)
	error("could not read disklist file \"%s\"", conf_diskfile);
    match_disklist(&diskq, main_argc-1, main_argv+1);
    amfree(conf_diskfile);

    conf_tapelist = getconf_str(CNF_TAPELIST);
    if (*conf_tapelist == '/') {
	conf_tapelist = stralloc(conf_tapelist);
    } else {
	conf_tapelist = stralloc2(config_dir, conf_tapelist);
    }
    if(read_tapelist(conf_tapelist))
	error("could not load tapelist \"%s\"", conf_tapelist);
    amfree(conf_tapelist);

    datestamp = construct_datestamp(NULL);

    dumpuser = getconf_str(CNF_DUMPUSER);
    if((pw = getpwnam(dumpuser)) == NULL)
	error("dumpuser %s not found in password file", dumpuser);
    if(pw->pw_uid != getuid())
	error("must run amflush as user %s", dumpuser);

    conf_logdir = getconf_str(CNF_LOGDIR);
    if (*conf_logdir == '/') {
	conf_logdir = stralloc(conf_logdir);
    } else {
	conf_logdir = stralloc2(config_dir, conf_logdir);
    }
    conf_logfile = vstralloc(conf_logdir, "/log", NULL);
    if (access(conf_logfile, F_OK) == 0)
	error("%s exists: amdump or amflush is already running, or you must run amcleanup", conf_logfile);
    amfree(conf_logfile);

    driver_program = vstralloc(libexecdir, "/", "driver", versionsuffix(),
			       NULL);
    reporter_program = vstralloc(sbindir, "/", "amreport", versionsuffix(),
				 NULL);
    logroll_program = vstralloc(libexecdir, "/", "amlogroll", versionsuffix(),
				NULL);

    if(datearg) {
	sle_t *dir, *next_dir;
	int i, ok;

	datestamp_list = pick_all_datestamp(1);
	for(dir = datestamp_list->first; dir != NULL;) {
	    next_dir = dir->next;
	    ok = 0;
	    for(i=0; i<nb_datearg && ok==0; i++) {
		ok = match_datestamp(datearg[i], dir->name);
	    }
	    if(ok == 0) { /* remove dir */
		remove_sl(datestamp_list, dir);
		amfree(dir->name);
		amfree(dir);
	    }
	    dir = next_dir;
	}
    }
    else {
	datestamp_list = pick_datestamp(1);
    }

    confirm();

    for(dp = diskq.head; dp != NULL; dp = dp->next) {
	if(dp->todo)
	    log_add(L_DISK, "%s %s", dp->host->hostname, dp->name);
    }

    if(!foreground) detach();
    erroutput_type = (ERR_AMANDALOG|ERR_INTERACTIVE);
    set_logerror(logerror);
    fprintf(stderr, "amflush: datestamp %s\n", datestamp);
    log_add(L_START, "date %s", datestamp);

    /* START DRIVER */
    if(pipe(driver_pipe) == -1) {
	error("error [opening pipe to driver: %s]", strerror(errno));
    }
    if((driver_pid = fork()) == 0) {
	/*
	 * This is the child process.
	 */
	dup2(driver_pipe[0], 0);
	close(driver_pipe[1]);
	execle(driver_program,
	       "driver", config_name, "nodump", (char *)0,
	       safe_env());
	error("cannot exec %s: %s", driver_program, strerror(errno));
    } else if(driver_pid == -1) {
	error("cannot fork for %s: %s", driver_program, strerror(errno));
    }
    driver_stream = fdopen(driver_pipe[1], "w");

    holding_list = get_flush(datestamp_list, NULL, 1, 0);
    for(holding_file=holding_list->first; holding_file != NULL;
				   holding_file = holding_file->next) {
	get_dumpfile(holding_file->name, &file);

	dp = lookup_disk(file.name, file.disk);
	if (dp->todo == 0) continue;

	fprintf(stderr,
		"FLUSH %s %s %s %d %s\n",
		file.name,
		file.disk,
		file.datestamp,
		file.dumplevel,
		holding_file->name);
	fprintf(driver_stream,
		"FLUSH %s %s %s %d %s\n",
		file.name,
		file.disk,
		file.datestamp,
		file.dumplevel,
		holding_file->name);
    }
    fprintf(stderr, "ENDFLUSH\n"); fflush(stderr);
    fprintf(driver_stream, "ENDFLUSH\n"); fflush(driver_stream);
    fclose(driver_stream);

    /* WAIT DRIVER */
    while(1) {
	if((pid = wait(&exitcode)) == -1) {
	    if(errno == EINTR) {
		continue;
	    } else {
		error("wait for %s: %s", driver_program, strerror(errno));
	    }
	} else if (pid == driver_pid) {
	    break;
	}
    }

    free_sl(datestamp_list);
    datestamp_list = NULL;
    free_sl(holding_list);
    holding_list = NULL;

    if(!foreground) { /* rename errfile */
	char *errfile, *errfilex, *nerrfilex, number[100];
	int tapecycle;
	int maxdays, days;
		
	struct stat stat_buf;

	errfile = vstralloc(conf_logdir, "/amflush", NULL);
	errfilex = NULL;
	nerrfilex = NULL;
	tapecycle = getconf_int(CNF_TAPECYCLE);
	maxdays = tapecycle + 2;
	days = 1;
	/* First, find out the last existing errfile,           */
	/* to avoid ``infinite'' loops if tapecycle is infinite */

	snprintf(number,100,"%d",days);
	errfilex = newvstralloc(errfilex, errfile, ".", number, NULL);
	while ( days < maxdays && stat(errfilex,&stat_buf)==0) {
	    days++;
	    snprintf(number,100,"%d",days);
	    errfilex = newvstralloc(errfilex, errfile, ".", number, NULL);
	}
	snprintf(number,100,"%d",days);
	errfilex = newvstralloc(errfilex, errfile, ".", number, NULL);
	nerrfilex = NULL;
	while (days > 1) {
	    amfree(nerrfilex);
	    nerrfilex = errfilex;
	    days--;
	    snprintf(number,100,"%d",days);
	    errfilex = vstralloc(errfile, ".", number, NULL);
	    if (rename(errfilex, nerrfilex) != 0) {
		error("cannot rename \"%s\" to \"%s\": %s",
		      errfilex, nerrfilex, strerror(errno));
	    }
	}
	errfilex = newvstralloc(errfilex, errfile, ".1", NULL);
	if (rename(errfile,errfilex) != 0) {
	    error("cannot rename \"%s\" to \"%s\": %s",
		  errfilex, nerrfilex, strerror(errno));
	}
	amfree(errfile);
	amfree(errfilex);
	amfree(nerrfilex);
    }

    /*
     * Have amreport generate report and send mail.  Note that we do
     * not bother checking the exit status.  If it does not work, it
     * can be rerun.
     */

    if((reporter_pid = fork()) == 0) {
	/*
	 * This is the child process.
	 */
	execle(reporter_program,
	       "amreport", config_name, (char *)0,
	       safe_env());
	error("cannot exec %s: %s", reporter_program, strerror(errno));
    } else if(reporter_pid == -1) {
	error("cannot fork for %s: %s", reporter_program, strerror(errno));
    }
    while(1) {
	if((pid = wait(&exitcode)) == -1) {
	    if(errno == EINTR) {
		continue;
	    } else {
		error("wait for %s: %s", reporter_program, strerror(errno));
	    }
	} else if (pid == reporter_pid) {
	    break;
	}
    }

    /*
     * Call amlogroll to rename the log file to its datestamped version.
     * Since we exec at this point, our exit code will be that of amlogroll.
     */

    execle(logroll_program,
	   "amreport", config_name, (char *)0,
	   safe_env());
    error("cannot exec %s: %s", logroll_program, strerror(errno));
    return 0;				/* keep the compiler happy */
}


static int get_letter_from_user()
{
    int r, ch;

    fflush(stdout); fflush(stderr);
    while((ch = getchar()) != EOF && ch != '\n' && isspace(ch)) {}
    if(ch == '\n') {
	r = '\0';
    } else if (ch != EOF) {
	r = ch;
	if(islower(r)) r = toupper(r);
	while((ch = getchar()) != EOF && ch != '\n') {}
    } else {
	r = ch;
	clearerr(stdin);
    }
    return r;
}


void confirm()
/* confirm before detaching and running */
{
    tape_t *tp;
    char *tpchanger;
    sle_t *dir;
    int ch;
    char *extra;

    if(datestamp_list == NULL) {
	printf("Could not find any Amanda directories to flush.\n");
	exit(1);
    }
    printf("\nToday is: %s\n",datestamp);
    printf("Flushing dumps in");
    extra = "";
    for(dir = datestamp_list->first; dir != NULL; dir = dir->next) {
	printf("%s %s", extra, dir->name);
	extra = ",";
    }
    tpchanger = getconf_str(CNF_TPCHANGER);
    if(*tpchanger != '\0') {
	printf(" using tape changer \"%s\".\n", tpchanger);
    } else {
	printf(" to tape drive \"%s\".\n", getconf_str(CNF_TAPEDEV));
    }

    printf("Expecting ");
    tp = lookup_last_reusable_tape(0);
    if(tp != NULL) printf("tape %s or ", tp->label);
    printf("a new tape.");
    tp = lookup_tapepos(1);
    if(tp != NULL) printf("  (The last dumps were to tape %s)", tp->label);

    while (1) {
	printf("\nAre you sure you want to do this [yN]? ");
	if((ch = get_letter_from_user()) == 'Y') {
	    return;
	} else if (ch == 'N' || ch == '\0' || ch == EOF) {
	    if (ch == EOF) {
		putchar('\n');
	    }
	    break;
	}
    }

    printf("Ok, quitting.  Run amflush again when you are ready.\n");
    exit(1);
}

void detach()
{
    int fd, fderr;
    char *errfile;

    fflush(stdout); fflush(stderr);
    if((fd = open("/dev/null", O_RDWR, 0666)) == -1)
	error("could not open /dev/null: %s", strerror(errno));

    switch(fork()) {
    case -1: error("could not fork: %s", strerror(errno));
    case 0:
	dup2(fd,0);
	aclose(fd);
	errfile = vstralloc(conf_logdir, "/amflush", NULL);
	if((fderr = open(errfile, O_WRONLY| O_CREAT | O_TRUNC, 0600)) == -1)
	    error("could not open %s: %s", errfile, strerror(errno));
	dup2(fderr,1);
	dup2(fderr,2);
	aclose(fderr);
	setsid();
	amfree(errfile);
	return;
    }

    puts("Running in background, you can log off now.");
    puts("You'll get mail when amflush is finished.");
    exit(0);
}


