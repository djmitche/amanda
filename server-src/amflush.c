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
 * $Id: amflush.c,v 1.15 1997/12/23 11:50:55 amcore Exp $
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

char *pname = "amflush";

disklist_t *diskqp;

static char *config;
char confdir[1024];
char reporter_program[1024];

/* local functions */
int main P((int argc, char **argv));
void flush_holdingdisk P((char *diskdir));
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

    erroutput_type = ERR_INTERACTIVE;
    foreground = 0;

    if(main_argc > 1 && !strcmp(main_argv[1], "-f")) {
	foreground = 1;
	main_argc--,main_argv++;
    }

    if(main_argc != 2)
	error("Usage: amflush%s [-f] <confdir>", versionsuffix());

    config = main_argv[1];
    ap_snprintf(confdir, sizeof(confdir), "%s/%s", CONFIG_DIR, main_argv[1]);
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

    ap_snprintf(taper_program, sizeof(taper_program),
		"%s/taper%s", libexecdir, versionsuffix());
    ap_snprintf(reporter_program, sizeof(reporter_program),
		"%s/reporter%s", libexecdir, versionsuffix());

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

    ap_snprintf(dirname, sizeof(dirname), "%s/%s", diskdir, datestamp);

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

	ap_snprintf(destname, sizeof(destname),
		    "%s/%s", dirname, entry->d_name);

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

	taper_cmd(FILE_WRITE, dp);
	tok = getresult(taper);
	if(tok == TRYAGAIN) {
	    /* we'll retry one time */
	    taper_cmd(FILE_WRITE, dp);
	    tok = getresult(taper);
	}

	switch(tok) {
	case DONE: /* DONE <handle> <label> <tape file> <err mess> */
	    assert(argc == 5);

	    update_info_taper(dp, argv[3], atoi(argv[4]));
	    
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
    taper_cmd(START_TAPER, datestamp);
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
	taper_cmd(QUIT, NULL);
	while(wait(NULL) != -1);

    }

    log(L_FINISH, "date %s time %s", datestamp, walltime_str(curclock()));

    /* now, have reporter generate report and send mail */

    chdir(confdir);
    execle(reporter_program, "reporter", (char *)0, safe_env());
}
