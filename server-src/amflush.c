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
 * $Id: amflush.c,v 1.41.2.11 1999/09/08 23:27:24 jrj Exp $
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
char *reporter_program;
holding_t *holding_list;
char *datestamp;

/* local functions */
int main P((int argc, char **argv));
void flush_holdingdisk P((char *diskdir, char *datestamp));
void confirm P((void));
void detach P((void));
void run_dumps P((void));


int main(main_argc, main_argv)
int main_argc;
char **main_argv;
{
    int foreground;
    struct passwd *pw;
    char *dumpuser;
    int fd;
    char *conffile;
    char *conf_diskfile;
    char *conf_tapelist;
    char *conf_logfile;
    disklist_t *diskqp;

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

    if(main_argc > 1 && strcmp(main_argv[1], "-f") == 0) {
	foreground = 1;
	main_argc--,main_argv++;
    }

    if(main_argc != 2) {
	error("Usage: amflush%s [-f] <confdir>", versionsuffix());
    }

    config_name = main_argv[1];

    config_dir = vstralloc(CONFIG_DIR, "/", config_name, "/", NULL);
    conffile = stralloc2(config_dir, CONFFILE_NAME);
    if(read_conffile(conffile)) {
	error("could not read config file \"%s\"", conffile);
    }
    amfree(conffile);
    conf_diskfile = getconf_str(CNF_DISKFILE);
    if (*conf_diskfile == '/') {
	conf_diskfile = stralloc(conf_diskfile);
    } else {
	conf_diskfile = stralloc2(config_dir, conf_diskfile);
    }
    if((diskqp = read_diskfile(conf_diskfile)) == NULL) {
	error("could not read disklist file \"%s\"", conf_diskfile);
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

    datestamp = construct_datestamp();

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
    if (access(conf_logfile, F_OK) == 0) {
	error("%s exists: amdump or amflush is already running, or you must run amcleanup", conf_logfile);
    }
    amfree(conf_logfile);
 
    reporter_program = vstralloc(sbindir, "/", "amreport", versionsuffix(),
				 NULL);

    holding_list = pick_datestamp();
    confirm();
    if(!foreground) detach();
    erroutput_type = (ERR_AMANDALOG|ERR_INTERACTIVE);
    set_logerror(logerror);
    run_dumps();

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

	ap_snprintf(number,100,"%d",days);
	errfilex = newvstralloc(errfilex, errfile, ".", number, NULL);
	while ( days < maxdays && stat(errfilex,&stat_buf)==0) {
	    days++;
	    ap_snprintf(number,100,"%d",days);
	    errfilex = newvstralloc(errfilex, errfile, ".", number, NULL);
	}
	ap_snprintf(number,100,"%d",days);
	errfilex = newvstralloc(errfilex, errfile, ".", number, NULL);
	nerrfilex = NULL;
	while (days > 1) {
	    amfree(nerrfilex);
	    nerrfilex = errfilex;
	    days--;
	    ap_snprintf(number,100,"%d",days);
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

    /* now, have reporter generate report and send mail */

    execle(reporter_program, "amreport", config_name, (char *)0, safe_env());

    return 0;
}


int get_letter_from_user()
{
    int r = '\0';
    int ch;

    fflush(stdout); fflush(stderr);
    while((ch = getchar()) != EOF && ch != '\n' && isspace(ch)) {}
    if(ch == '\n') {
	ch = '\0';
    } else if (ch != EOF) {
	r = ch;
	if(islower(r)) r = toupper(r);
	while((ch = getchar()) != EOF && ch != '\n') {}
    } else {
	printf("\nGot EOF.  Goodbye.\n");
	exit(1);
    }
    return r;
}


void confirm()
/* confirm before detaching and running */
{
    tape_t *tp;
    char *tpchanger;
    holding_t *dir;

    if(holding_list == NULL) {
	printf("Could not find any Amanda directories to flush.\n");
	exit(1);
    }
    printf("\nFlushing dumps in");
    for(dir = holding_list; dir != NULL; dir = dir->next) {
	printf(" %s,",dir->name);
    }
    printf("\n");
    printf("today: %s\n",datestamp);
    tpchanger = getconf_str(CNF_TPCHANGER);
    if(*tpchanger != '\0') {
	printf("using tape changer \"%s\".\n", tpchanger);
    } else {
	printf("to tape drive %s.\n", getconf_str(CNF_TAPEDEV));
    }

    printf("Expecting ");
    tp = lookup_last_reusable_tape(0);
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


void flush_holdingdisk(diskdir, datestamp)
char *diskdir, *datestamp;
{
    DIR *workdir;
    struct dirent *entry;
    char *dirname = NULL;
    char *destname = NULL;
    int filenum;
    disk_t *dp;
    tok_t tok;
    int result_argc;
    char *result_argv[MAX_ARGS+1];
    dumpfile_t file;

    dirname = vstralloc(diskdir, "/", datestamp, NULL);

    if((workdir = opendir(dirname)) == NULL) {
	log_add(L_INFO, "%s: could not open working dir: %s",
	        dirname, strerror(errno));
	amfree(dirname);
	return;
    }

    while((entry = readdir(workdir)) != NULL) {
	if(is_dot_or_dotdot(entry->d_name)) {
	    continue;
	}

	destname = newvstralloc(destname,
				dirname, "/", entry->d_name,
				NULL);

	if(is_emptyfile(destname)) {
	    if(unlink(entry->d_name) == -1) {
		log_add(L_INFO,"%s: ignoring zero length file.", entry->d_name);
	    } else {
		log_add(L_INFO,"%s: removed zero length file.", entry->d_name);
	    }
	    continue;
	}

	get_dumpfile(destname, &file);
	if( file.type != F_DUMPFILE) {
	    if( file.type != F_CONT_DUMPFILE )
		log_add(L_INFO, "%s: ignoring cruft file.", entry->d_name);
	    continue;
	}

	dp = lookup_disk(file.name, file.disk);

	if (dp == NULL) {
	    log_add(L_INFO, "%s: disk %s:%s not in database, skipping it.",
		    entry->d_name, file.name, file.disk);
	    continue;
	}

	if(file.dumplevel < 0 || file.dumplevel > 9) {
	    log_add(L_INFO, "%s: ignoring file with bogus dump level %d.",
		    entry->d_name, file.dumplevel);
	    continue;
	}

	dp->up = NULL;
	taper_cmd(FILE_WRITE, dp, destname, file.dumplevel, file.datestamp);

	tok = getresult(taper, 0, &result_argc, result_argv, MAX_ARGS+1);
	if(tok == TRYAGAIN) {
	    /* we'll retry one time */
	    taper_cmd(FILE_WRITE, dp, destname,file.dumplevel,file.datestamp);
	    tok = getresult(taper, 0, &result_argc, result_argv, MAX_ARGS+1);
	}

	switch(tok) {
	case DONE: /* DONE <handle> <label> <tape file> <err mess> */
	    if(result_argc != 5) {
		error("error [DONE result_argc != 5: %d]", result_argc);
	    }
	    if( dp != serial2disk(result_argv[2]))
		error("Bad serial");
	    free_serial(result_argv[2]);

	    filenum = atoi(result_argv[4]);
	    if(file.is_partial == 0)
		update_info_taper(dp, result_argv[3], filenum, file.dumplevel);

	    unlink_holding_files(destname);
	    break;
	case TRYAGAIN: /* TRY-AGAIN <handle> <err mess> */
	    if (result_argc < 2) {
		error("error [taper TRYAGAIN result_argc < 2: %d]", result_argc);
	    }
	    if( dp != serial2disk(result_argv[2]))
		error("Bad serial");
	    free_serial(result_argv[2]);

	    log_add(L_WARNING,
		    "%s: too many taper retries, leaving file on disk",
		    destname);
	    break;

	case TAPE_ERROR: /* TAPE-ERROR <handle> <err mess> */
	    if( dp != serial2disk(result_argv[2]))
		error("Bad serial");
	    free_serial(result_argv[2]);
	    /* Note: fall through code... */

	default:
	    log_add(L_WARNING, "%s: taper error, leaving file on disk",
		    destname);
	    break;
	}
    }
    closedir(workdir);

    /* try to zap the now (hopefully) empty working dir */
    if(rmdir(dirname))
	log_add(L_WARNING, "Could not rmdir %s.  Check for cruft.",
	        dirname);
    amfree(destname);
    amfree(dirname);
}

void run_dumps()
{
    holdingdisk_t *hdisk;
    holding_t *dir;
    tok_t tok;
    int result_argc;
    char *result_argv[MAX_ARGS+1];
    char *taper_program;

    taper_program = vstralloc(libexecdir, "/", "taper", versionsuffix(), NULL);
    startclock();
    log_add(L_START, "date %s", datestamp);

    init_driverio();
    startup_tape_process(taper_program);
    taper_cmd(START_TAPER, datestamp, NULL, 0, NULL);
    tok = getresult(taper, 0, &result_argc, result_argv, MAX_ARGS+1);

    if(tok != TAPER_OK) {
	/* forget it */
	sleep(5);	/* let taper log first, but not really necessary */
	log_add(L_ERROR, "Cannot flush without tape.  Try again.");
	log_add(L_FINISH, "date %s time %s",
		datestamp, walltime_str(curclock()));
    } else {
	for(dir = holding_list; dir !=NULL; dir = dir->next) {
	    for(hdisk = getconf_holdingdisks(); hdisk != NULL; hdisk = hdisk->next)
		flush_holdingdisk(hdisk->diskdir, dir->name);
	}

	/* tell taper to quit, then wait for it */
	taper_cmd(QUIT, NULL, NULL, 0, NULL);
	while(wait(NULL) != -1);

    }

    log_add(L_FINISH, "date %s time %s", datestamp, walltime_str(curclock()));
}

