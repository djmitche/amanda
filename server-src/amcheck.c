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
 * $Id: amcheck.c,v 1.57 1999/04/01 15:34:11 martinea Exp $
 *
 * checks for common problems in server and clients
 */
#include "amanda.h"
#include "conffile.h"
#include "statfs.h"
#include "diskfile.h"
#include "tapefile.h"
#include "tapeio.h"
#include "changer.h"
#include "protocol.h"
#include "clock.h"
#include "version.h"
#include "amindex.h"

#define CHECK_TIMEOUT   30
#define BUFFER_SIZE	32768

static int overwrite;
dgram_t *msg = NULL;
char *confname;

/* local functions */

void usage P((void));
int start_client_checks P((int fd));
int start_server_check P((int fd, int do_localchk, int do_tapechk));
int main P((int argc, char **argv));
int scan_init P((int rc, int ns, int bk));
int taperscan_slot P((int rc, char *slotstr, char *device));
char *taper_scan P((void));
int test_server_pgm P((FILE *outf, char *dir, char *pgm,
		       int suid, uid_t dumpuid));

void usage()
{
    error("Usage: amcheck%s [-M <username>] [-mwsct] <conf>", versionsuffix());
}

static unsigned long malloc_hist_1, malloc_size_1;
static unsigned long malloc_hist_2, malloc_size_2;

int main(argc, argv)
int argc;
char **argv;
{
    char buffer[BUFFER_SIZE], *cmd = NULL;
    char *confdir, *version_string;
    char *mainfname = NULL;
    char pid_str[NUM_STR_SIZE];
    int do_clientchk, clientchk_pid, client_probs;
    int do_localchk, do_tapechk, serverchk_pid, server_probs;
    int chk_flag;
    int opt, size, result_port, tempfd, mainfd;
    amwait_t retstat;
    pid_t pid;
    extern int optind;
    int l, n, s;
    int fd;
    char *mailto = NULL;
    extern char *optarg;
    int mailout;
    char *tempfname = NULL;
    
    for(fd = 3; fd < FD_SETSIZE; fd++) {
	/*
	 * Make sure nobody spoofs us with a lot of extra open files
	 * that would cause an open we do to get a very high file
	 * descriptor, which in turn might be used as an index into
	 * an array (e.g. an fd_set).
	 */
	close(fd);
    }

    set_pname("amcheck");

    malloc_size_1 = malloc_inuse(&malloc_hist_1);

    ap_snprintf(pid_str, sizeof(pid_str), "%ld", (long)getpid());

    erroutput_type = ERR_INTERACTIVE;

    /* set up dgram port first thing */

    msg = dgram_alloc();

    if(dgram_bind(msg, &result_port) == -1)
	error("could not bind result datagram port: %s", strerror(errno));

    if(geteuid() == 0) {
	/* set both real and effective uid's to real uid, likewise for gid */
	setgid(getgid());
	setuid(getuid());
    }

    mailout = overwrite = 0;
    do_localchk = do_tapechk = do_clientchk = 0;
    chk_flag = 0;
    server_probs = client_probs = 0;
    tempfd = mainfd = -1;

    /* process arguments */

    while((opt = getopt(argc, argv, "M:mwsclt")) != EOF) {
	switch(opt) {
	case 'm':	mailout = 1; break;
	case 'M':	mailout = 1; mailto=optarg; break;
	case 's':	do_localchk = 1; do_tapechk = 1;
			chk_flag = 1;
			break;
	case 'c':	do_clientchk = 1;
			chk_flag = 1;
			break;
	case 'l':	do_localchk = 1;
			chk_flag = 1;
			break;
	case 'w':	do_tapechk = 1; overwrite = 1;
			chk_flag = 1;
			break;
	case 't':	do_tapechk = 1;
			chk_flag = 1;
			break;
	case '?':
	default:
	    usage();
	}
    }
    argc -= optind, argv += optind;
    if(! chk_flag) {
	do_localchk = do_clientchk = do_tapechk = 1;
    }

    if(argc != 1) usage();

    confname = *argv;

    confdir = vstralloc(CONFIG_DIR, "/", confname, NULL);
    if(chdir(confdir) != 0)
	error("could not cd to confdir %s: %s", confdir, strerror(errno));
    amfree(confdir);

    if(read_conffile(CONFFILE_NAME))
	error("could not read amanda config file");

    /*
     * If both server and client side checks are being done, the server
     * check output goes to the main output, while the client check output
     * goes to a temporary file and is copied to the main output when done.
     *
     * If the output is to be mailed, the main output is also a disk file,
     * otherwise it is stdout.
     */
    if(do_clientchk && (do_localchk || do_tapechk)) {
	/* we need the temp file */
	tempfname = vstralloc("/tmp/amcheck.temp.", pid_str, NULL);
	if((tempfd = open(tempfname, O_RDWR|O_CREAT|O_TRUNC, 0600)) == -1)
	    error("could not open %s: %s", tempfname, strerror(errno));
    }

    if(mailout) {
	/* the main fd is a file too */
	mainfname = vstralloc("/tmp/amcheck.main.", pid_str, NULL);
	if((mainfd = open(mainfname, O_RDWR|O_CREAT|O_TRUNC, 0600)) == -1)
	    error("could not open %s: %s", mainfname, strerror(errno));
    }
    else
	/* just use stdout */
	mainfd = 1;

    /* start server side checks */

    if(do_localchk || do_tapechk)
	serverchk_pid = start_server_check(mainfd, do_localchk, do_tapechk);
    else
	serverchk_pid = 0;

    /* start client side checks */

    if(do_clientchk) {
	clientchk_pid = start_client_checks((do_localchk || do_tapechk) ? tempfd : mainfd);
    }
    else
	clientchk_pid = 0;

    /* wait for child processes and note any problems */

    while(1) {
	if((pid = wait(&retstat)) == -1) {
	    if(errno == EINTR) continue;
	    else break;
	}
	else if(pid == clientchk_pid) {
	    client_probs = WIFSIGNALED(retstat) || WEXITSTATUS(retstat);
	    clientchk_pid = 0;
	}
	else if(pid == serverchk_pid) {
	    server_probs = WIFSIGNALED(retstat) || WEXITSTATUS(retstat);
	    serverchk_pid = 0;
	}
	else {
	    char number[NUM_STR_SIZE];
	    char *wait_msg = NULL;

	    ap_snprintf(number, sizeof(number), "%ld", (long)pid);
	    wait_msg = vstralloc("parent: reaped bogus pid ", number, "\n",
				 NULL);
	    for(l = 0, n = strlen(buffer); l < n; l += s) {
		if((s = write(mainfd, buffer + l, n - l)) < 0) {
		    error("write main file: %s", strerror(errno));
		}
	    }
	    amfree(wait_msg);
	}
    }
    amfree(msg);

    /* copy temp output to main output and write tagline */

    if(do_clientchk && (do_localchk || do_tapechk)) {
	if(lseek(tempfd, 0, 0) == -1)
	    error("seek temp file: %s", strerror(errno));

	while((size=read(tempfd, buffer, sizeof(buffer))) > 0) {
	    for(l = 0; l < size; l += s) {
		if((s = write(mainfd, buffer + l, size - l)) < 0) {
		    error("write main file: %s", strerror(errno));
		}
	    }
	}
	if(size < 0)
	    error("read temp file: %s", strerror(errno));
	aclose(tempfd);
	unlink(tempfname);
    }
    amfree(tempfname);

    version_string = vstralloc("\n",
			       "(brought to you by Amanda ", version(), ")\n",
			       NULL);
    for(l = 0, n = strlen(version_string); l < n; l += s) {
	if((s = write(mainfd, version_string + l, n - l)) < 0) {
	    error("write main file: %s", strerror(errno));
	}
    }
    amfree(version_string);

    malloc_size_2 = malloc_inuse(&malloc_hist_2);

    if(malloc_size_1 != malloc_size_2) {
	malloc_list(fileno(stderr), malloc_hist_1, malloc_hist_2);
    }

    /* send mail if requested, but only if there were problems */

    if((server_probs || client_probs) && mailout) {
	fflush(stdout);
	if(close(mainfd) == -1)
	    error("close main file: %s", strerror(errno));
	mainfd = -1;

	cmd = vstralloc(MAILER,
			" -s ", "\"",
			  getconf_str(CNF_ORG),
			  " AMANDA PROBLEM: FIX BEFORE RUN, IF POSSIBLE\"",
			" ", (mailto ? mailto : getconf_str(CNF_MAILTO)),
			" < ", mainfname,
			NULL);
	if(system(cmd) != 0)
	    error("mail command failed: %s", cmd);
	amfree(cmd);
    }
    if(mailout)
	unlink(mainfname);
    return (server_probs || client_probs);
}

/* --------------------------------------------------- */

int nslots, backwards, found, got_match, tapedays;
char *datestamp;
char *first_match_label = NULL, *first_match = NULL, *found_device = NULL;
char *label;
char *searchlabel, *labelstr;
tape_t *tp;
FILE *errf;

int scan_init(rc, ns, bk)
int rc, ns, bk;
{
    if(rc)
	error("could not get changer info: %s", changer_resultstr);

    nslots = ns;
    backwards = bk;

    return 0;
}

int taperscan_slot(rc, slotstr, device)
int rc;
char *slotstr;
char *device;
{
    char *errstr;

    if(rc == 2) {
	fprintf(errf, "%s: fatal slot %s: %s\n",
		get_pname(), slotstr, changer_resultstr);
	return 1;
    }
    else if(rc == 1) {
	fprintf(errf, "%s: slot %s: %s\n",
		get_pname(), slotstr, changer_resultstr);
	return 0;
    }
    else {
	if((errstr = tape_rdlabel(device, &datestamp, &label)) != NULL) {
	    fprintf(errf, "%s: slot %s: %s\n", get_pname(), slotstr, errstr);
	} else {
	    /* got an amanda tape */
	    fprintf(errf, "%s: slot %s: date %-8s label %s",
		    get_pname(), slotstr, datestamp, label);
	    if(searchlabel != NULL && strcmp(label, searchlabel) == 0) {
		/* it's the one we are looking for, stop here */
		fprintf(errf, " (exact label match)\n");
		found_device = newstralloc(found_device, device);
		found = 1;
		return 1;
	    }
	    else if(!match(labelstr, label))
		fprintf(errf, " (no match)\n");
	    else {
		/* not an exact label match, but a labelstr match */
		/* check against tape list */
		tp = lookup_tapelabel(label);
		if(tp != NULL && !reusable_tape(tp))
		    fprintf(errf, " (active tape)\n");
		else if(got_match)
		    fprintf(errf, " (labelstr match)\n");
		else {
		    got_match = 1;
		    first_match = newstralloc(first_match, slotstr);
		    first_match_label = newstralloc(first_match_label, label);
		    fprintf(errf, " (first labelstr match)\n");
		    if(!backwards || !searchlabel) {
			found = 2;
			found_device = newstralloc(found_device, device);
			return 1;
		    }
		}
	    }
	}
    }
    return 0;
}

char *taper_scan()
{
    char *outslot = NULL;

    if((tp = lookup_last_reusable_tape(0)) == NULL)
	searchlabel = NULL;
    else
	searchlabel = tp->label;

    found = 0;
    got_match = 0;

    changer_find(scan_init, taperscan_slot, searchlabel);

    if(found == 2)
	searchlabel = first_match_label;
    else if(!found && got_match) {
	searchlabel = first_match_label;
	amfree(found_device);
	if(changer_loadslot(first_match, &outslot, &found_device) == 0) {
	    found = 1;
	}
    } else if(!found) {
	if(searchlabel) {
	    changer_resultstr = newvstralloc(changer_resultstr,
					     "label ", searchlabel,
					     " or new tape not found in rack",
					     NULL);
	} else {
	    changer_resultstr = newstralloc(changer_resultstr,
					    "new tape not found in rack");
	}
    }
    amfree(outslot);

    return found ? found_device : NULL;
}

int test_server_pgm(outf, dir, pgm, suid, dumpuid)
FILE *outf;
char *dir;
char *pgm;
int suid;
uid_t dumpuid;
{
    struct stat statbuf;
    int pgmbad = 0;

    pgm = vstralloc(dir, "/", pgm, versionsuffix(), NULL);
    if(stat(pgm, &statbuf) == -1) {
	fprintf(outf, "ERROR: program %s: does not exist\n",
		pgm);
	pgmbad = 1;
    } else if (!S_ISREG(statbuf.st_mode)) {
	fprintf(outf, "ERROR: program %s: not a file\n",
		pgm);
	pgmbad = 1;
    } else if (access(pgm, X_OK) == -1) {
	fprintf(outf, "ERROR: program %s: not executable\n",
		pgm);
	pgmbad = 1;
    } else if (suid \
	       && dumpuid != 0
	       && (statbuf.st_uid != 0 || (statbuf.st_mode & 04000) == 0)) {
	fprintf(outf, "WARNING: program %s: not setuid-root\n",
		pgm);
    }
    amfree(pgm);
    return pgmbad;
}

int start_server_check(fd, do_localchk, do_tapechk)
int fd;
{
    char *errstr, *tapename;
    generic_fs_stats_t fs;
    tape_t *tp;
    FILE *outf;
    holdingdisk_t *hdp;
    int pid;
    int tapebad = 0, disklow = 0, logbad = 0;
    int userbad = 0, infobad = 0, indexbad = 0, pgmbad = 0;
    int testtape = do_tapechk;
    uid_t uid_me = getuid();
    uid_t uid_dumpuser = uid_me;

    switch(pid = fork()) {
    case -1: error("could not fork server check: %s", strerror(errno));
    case 0: break;
    default:
	return pid;
    }

    dup2(fd, 1);
    dup2(fd, 2);

    set_pname("amcheck-server");

    amfree(msg);

    startclock();

    if((outf = fdopen(fd, "w")) == NULL)
	error("fdopen %d: %s", fd, strerror(errno));
    errf = outf;

    fprintf(outf, "Amanda Tape Server Host Check\n");
    fprintf(outf, "-----------------------------\n");

    /*
     * Make sure we are running as the dump user.
     */
    {
	char *dumpuser = getconf_str(CNF_DUMPUSER);
	struct passwd *pw;

	if ((pw = getpwnam(dumpuser)) == NULL) {
	    fprintf(outf, "ERROR: cannot look up dump user \"%s\"\n", dumpuser);
	    userbad = 1;
	} else {
	    uid_dumpuser = pw->pw_uid;
	}
	if ((pw = getpwuid(uid_me)) == NULL) {
	    fprintf(outf, "ERROR: cannot look up my own uid (%ld)\n",
		    (long)uid_me);
	    userbad = 1;
	}
	if (uid_me != uid_dumpuser) {
	    fprintf(outf, "ERROR: running as user \"%s\" instead of \"%s\"\n",
		    pw->pw_name, dumpuser);
	    userbad = 1;
	}
    }

    /*
     * Look up the programs used on the server side.
     */
    if(do_localchk) {
	if(access(libexecdir, X_OK) == -1) {
	    fprintf(outf, "ERROR: program dir %s: not accessible\n",
		    libexecdir);
	    pgmbad = 1;
	} else {
	    pgmbad = pgmbad \
		     || test_server_pgm(outf, libexecdir, "planner",
					1, uid_dumpuser);
	    pgmbad = pgmbad \
		     || test_server_pgm(outf, libexecdir, "dumper",
					1, uid_dumpuser);
	    pgmbad = pgmbad \
		     || test_server_pgm(outf, libexecdir, "driver",
					0, uid_dumpuser);
	    pgmbad = pgmbad \
		     || test_server_pgm(outf, libexecdir, "taper",
					0, uid_dumpuser);
	    pgmbad = pgmbad \
		     || test_server_pgm(outf, libexecdir, "getconf",
					0, uid_dumpuser);
	    pgmbad = pgmbad \
		     || test_server_pgm(outf, libexecdir, "amtrmidx",
					0, uid_dumpuser);
	}
	if(access(sbindir, X_OK) == -1) {
	    fprintf(outf, "ERROR: program dir %s: not accessible\n",
		    sbindir);
	    pgmbad = 1;
	} else {
	    pgmbad = pgmbad \
		     || test_server_pgm(outf, sbindir, "amcheck",
					1, uid_dumpuser);
	    pgmbad = pgmbad \
		     || test_server_pgm(outf, sbindir, "amdump",
					0, uid_dumpuser);
	    pgmbad = pgmbad \
		     || test_server_pgm(outf, sbindir, "amreport",
					0, uid_dumpuser);
	}
    }

    /*
     * Check that the tapelist file is writable if it already exists.
     * Also, check for a "hold" file (just because it is convenient
     * to do it here).
     */

    if(do_localchk || do_tapechk) {
	char *conf_tapelist;
	char *confdir;
	char *tapefile;
	char *holdfile;

	conf_tapelist=getconf_str(CNF_TAPELIST);
	confdir = vstralloc(CONFIG_DIR, "/", confname, NULL);
	tapefile = vstralloc(confdir, "/", conf_tapelist, NULL);
	holdfile = vstralloc(confdir, "/", "hold", NULL);
	if(access(confdir, W_OK) == -1) {
	    fprintf(outf, "ERROR: conf dir %s: not writable\n",
		    confdir);
	    tapebad = 1;
	} else if(access(tapefile, F_OK) == 0 && access(tapefile, W_OK) != 0) {
	    fprintf(outf, "ERROR: tape list %s: not writable\n",
		    tapefile);
	    tapebad = 1;
	} else if(read_tapelist(conf_tapelist)) {
	    fprintf(outf, "ERROR: tape list %s: parse error\n",
		    conf_tapelist);
	    tapebad = 1;
	}
	if(access(holdfile, F_OK) != -1) {
	    fprintf(outf, "NOTE: hold file %s exists\n",
		    holdfile);
	}
	amfree(confdir);
	amfree(tapefile);
	amfree(holdfile);
    }

    /* check available disk space */

    if(do_localchk) {
	for(hdp = getconf_holdingdisks(); hdp != NULL; hdp = hdp->next) {
	    if(get_fs_stats(hdp->diskdir, &fs) == -1) {
		fprintf(outf, "ERROR: holding disk %s: statfs: %s\n",
			hdp->diskdir, strerror(errno));
		disklow = 1;
	    }
	    else if(access(hdp->diskdir, W_OK) == -1) {
		fprintf(outf, "ERROR: holding disk %s: not writable: %s\n",
			hdp->diskdir, strerror(errno));
		disklow = 1;
	    }
	    else if(fs.avail == -1) {
		fprintf(outf,
			"WARNING: holding disk %s: available space unknown (%ld KB requested)\n",
			hdp->diskdir, hdp->disksize);
		disklow = 1;
	    }
	    else if(hdp->disksize > 0) {
		if(fs.avail < hdp->disksize) {
		    fprintf(outf,
			    "WARNING: holding disk %s: only %ld KB free (%ld KB requested)\n",
			    hdp->diskdir, fs.avail, hdp->disksize);
		    disklow = 1;
		}
		else
		    fprintf(outf,
			    "Holding disk %s: %ld KB disk space available, that's plenty\n",
			    hdp->diskdir, fs.avail);
	    }
	    else {
		if(fs.avail < -hdp->disksize) {
		    fprintf(outf,
			    "WARNING: holding disk %s: only %ld KB free, using nothing\n",
			    hdp->diskdir, fs.avail);
		    disklow = 1;
		}
		else
		    fprintf(outf,
			    "Holding disk %s: %ld KB disk space available, using %ld KB\n",
			    hdp->diskdir, fs.avail, fs.avail + hdp->disksize);
	    }
	}
    }

    /* check that the log file is writable if it already exists */

    if(do_localchk) {
	char *logdir;
	char *logfile;

	logdir = getconf_str(CNF_LOGDIR);
	logfile = vstralloc(logdir, "/log", NULL);

	if(access(logdir, W_OK) == -1) {
	    fprintf(outf, "ERROR: log dir %s: not writable\n", logdir);
	    logbad = 1;
	}

	if(access(logfile, F_OK) == 0) {
	    testtape = 0;
	    logbad = 1;
	    if(access(logfile, W_OK) != 0)
		fprintf(outf, "ERROR: log file %s: not writable\n",
			logfile);
	}

	if (testtape) {
	    logfile = newvstralloc(logfile, logdir, "/amdump", NULL);
	    if (access(logfile, F_OK) == 0) {
		testtape = 0;
		logbad = 1;
	    }
	}

	amfree(logfile);
    }

    if (testtape) {
	/* check that the tape is a valid amanda tape */

	tapedays = getconf_int(CNF_TAPECYCLE);
	labelstr = getconf_str(CNF_LABELSTR);
	tapename = getconf_str(CNF_TAPEDEV);

	if (!getconf_seen(CNF_TPCHANGER) && getconf_int(CNF_RUNTAPES) != 1) {
	    fprintf(outf,
		    "WARNING: if a tape changer is not available, runtapes must be set to 1\n");
	}

	if(changer_init() && (tapename = taper_scan()) == NULL) {
	    fprintf(outf, "ERROR: %s\n", changer_resultstr);
	    tapebad = 1;
	} else if(access(tapename,F_OK|R_OK|W_OK) == -1) {
	    fprintf(outf, "ERROR: %s: %s\n", tapename, strerror(errno));
	    tapebad = 1;
	} else if((errstr = tape_rdlabel(tapename, &datestamp, &label)) != NULL) {
	    fprintf(outf, "ERROR: %s: %s\n", tapename, errstr);
	    tapebad = 1;
	} else {
	    tp = lookup_tapelabel(label);
	    if(tp != NULL && !reusable_tape(tp)) {
		fprintf(outf, "ERROR: cannot overwrite active tape %s\n", label);
		tapebad = 1;
	    }
	    if(!match(labelstr, label)) {
		fprintf(outf, "ERROR: label %s doesn't match labelstr \"%s\"\n",
			label, labelstr);
		tapebad = 1;
	    }

	}

	if(tapebad) {
	    tape_t *exptape = lookup_last_reusable_tape(0);
	    fprintf(outf, "       (expecting ");
	    if(exptape != NULL) fprintf(outf, "tape %s or ", exptape->label);
	    fprintf(outf, "a new tape)\n");
	}

	if(!tapebad && overwrite) {
	    if((errstr = tape_writable(tapename)) != NULL) {
		fprintf(outf,
			"ERROR: tape %s label ok, but is not writable\n",
			label);
		tapebad = 1;
	    }
	    else fprintf(outf, "Tape %s is writable\n", label);
	}
	else fprintf(outf, "NOTE: skipping tape-writable test\n");

	if(!tapebad)
	    fprintf(outf, "Tape %s label ok\n", label);
    } else if (do_tapechk) {
	fprintf(outf, "WARNING: skipping tape test because amdump or amflush seem to be running\n");
	fprintf(outf, "WARNING: if they are not, you must run amcleanup\n");
    } else {
	fprintf(outf, "NOTE: skipping tape checks\n");
    }

    /*
     * See if the information file and index directory for each client
     * and disk is OK.  Since we may be seeing clients and/or disks for
     * the first time, these are just warnings, not errors.
     */
    if(do_localchk) {
	char *infodir = stralloc(getconf_str(CNF_INFOFILE));
	char *indexdir = stralloc(getconf_str(CNF_INDEXDIR));
	char *hostinfodir = NULL;
	char *hostindexdir = NULL;
	char *diskdir = NULL;
	char *infofile = NULL;
	struct stat statbuf;
	disklist_t *origqp;
	disk_t *dp;
	host_t *hostp;
	int indexdir_checked = 0;
	int hostindexdir_checked = 0;
	char *host;
	char *disk;

#if TEXTDB
	if(stat(infodir, &statbuf) == -1) {
	    fprintf(outf, "NOTE: info dir %s: does not exist\n",
		    infodir);
	    amfree(infodir);
	} else if (!S_ISDIR(statbuf.st_mode)) {
	    fprintf(outf, "ERROR: info dir %s: not a directory\n",
		    infodir);
	    amfree(infodir);
	    infobad = 1;
	} else if (access(infodir, W_OK) == -1) {
	    fprintf(outf, "ERROR: info dir %s: not writable\n",
		    infodir);
	    amfree(infodir);
	    infobad = 1;
	} else {
	    strappend(infodir, "/");
	}
#endif

	if((origqp = read_diskfile(getconf_str(CNF_DISKFILE))) == NULL)
	    error("could not load diskfile %s\n", getconf_str(CNF_DISKFILE));

	while(!empty(*origqp)) {
	    hostp = origqp->head->host;
	    host = sanitise_filename(hostp->hostname);
#if TEXTDB
	    if(infodir) {
		hostinfodir = newstralloc2(hostinfodir, infodir, host);
		if(stat(hostinfodir, &statbuf) == -1) {
		    fprintf(outf, "NOTE: info dir %s: does not exist\n",
			    hostinfodir);
		    amfree(hostinfodir);
		} else if (!S_ISDIR(statbuf.st_mode)) {
		    fprintf(outf, "ERROR: info dir %s: not a directory\n",
			    hostinfodir);
		    amfree(hostinfodir);
		    infobad = 1;
		} else if (access(hostinfodir, W_OK) == -1) {
		    fprintf(outf, "ERROR: info dir %s: not writable\n",
			    hostinfodir);
		    amfree(hostinfodir);
		    infobad = 1;
		} else {
		    strappend(hostinfodir, "/");
		}
	    }
#endif
	    for(dp = hostp->disks; dp != NULL; dp = dp->hostnext) {
		disk = sanitise_filename(dp->name);
#if TEXTDB
		if(hostinfodir) {
		    diskdir = newstralloc2(diskdir, hostinfodir, disk);
		    infofile = vstralloc(diskdir, "/", "info", NULL);
		    if(stat(diskdir, &statbuf) == -1) {
			fprintf(outf, "NOTE: info dir %s: does not exist\n",
				diskdir);
		    } else if (!S_ISDIR(statbuf.st_mode)) {
			fprintf(outf, "ERROR: info dir %s: not a directory\n",
				diskdir);
			infobad = 1;
		    } else if (access(diskdir, W_OK) == -1) {
			fprintf(outf, "ERROR: info dir %s: not writable\n",
				diskdir);
			infobad = 1;
		    } else if(stat(infofile, &statbuf) == -1) {
			fprintf(outf, "WARNING: info file %s: does not exist\n",
				infofile);
		    } else if (!S_ISREG(statbuf.st_mode)) {
			fprintf(outf, "ERROR: info file %s: not a file\n",
				infofile);
			infobad = 1;
		    } else if (access(infofile, R_OK) == -1) {
			fprintf(outf, "ERROR: info file %s: not readable\n",
				infofile);
			infobad = 1;
		    }
		    amfree(infofile);
		}
#endif
		if(dp->index) {
		    if(! indexdir_checked) {
			if(stat(indexdir, &statbuf) == -1) {
			    fprintf(outf, "NOTE: index dir %s: does not exist\n",
				    indexdir);
			    amfree(indexdir);
			} else if (!S_ISDIR(statbuf.st_mode)) {
			    fprintf(outf, "ERROR: index dir %s: not a directory\n",
				    indexdir);
			    amfree(indexdir);
			    indexbad = 1;
			} else if (access(indexdir, W_OK) == -1) {
			    fprintf(outf, "ERROR: index dir %s: not writable\n",
				    indexdir);
			    amfree(indexdir);
			    indexbad = 1;
			} else {
			    strappend(indexdir, "/");
			}
			indexdir_checked = 1;
		    }
		    if(indexdir) {
			if(! hostindexdir_checked) {
			    hostindexdir = stralloc2(indexdir, host);
			    if(stat(hostindexdir, &statbuf) == -1) {
			        fprintf(outf, "NOTE: index dir %s: does not exist\n",
				        hostindexdir);
			        amfree(hostindexdir);
			    } else if (!S_ISDIR(statbuf.st_mode)) {
			        fprintf(outf, "ERROR: index dir %s: not a directory\n",
				        hostindexdir);
			        amfree(hostindexdir);
			        indexbad = 1;
			    } else if (access(hostindexdir, W_OK) == -1) {
			        fprintf(outf, "ERROR: index dir %s: not writable\n",
				        hostindexdir);
			        amfree(hostindexdir);
			        indexbad = 1;
			    } else {
				strappend(hostindexdir, "/");
			    }
			    hostindexdir_checked = 1;
			}
			if(hostindexdir) {
			    diskdir = newstralloc2(diskdir, hostindexdir, disk);
			    if(stat(diskdir, &statbuf) == -1) {
				fprintf(outf, "NOTE: index dir %s: does not exist\n",
					diskdir);
			    } else if (!S_ISDIR(statbuf.st_mode)) {
				fprintf(outf, "ERROR: index dir %s: not a directory\n",
					diskdir);
				indexbad = 1;
			    } else if (access(diskdir, W_OK) == -1) {
				fprintf(outf, "ERROR: index dir %s: is not writable\n",
					diskdir);
				indexbad = 1;
			    }
			}
		    }
		}
		amfree(disk);
		remove_disk(origqp, dp);
	    }
	    amfree(host);
	    amfree(hostindexdir);
	    hostindexdir_checked = 0;
	}
	amfree(diskdir);
	amfree(hostinfodir);
	amfree(infodir);
	amfree(indexdir);
    }

    if(do_localchk) {
	if(access(COMPRESS_PATH, X_OK) == -1) {
	    fprintf(outf, "WARNING: %s is not executable, server-compression and indexing will not work\n",
	            COMPRESS_PATH);
	}
    }

    amfree(datestamp);
    amfree(label);

    fprintf(outf, "Server check took %s seconds\n", walltime_str(curclock()));

    fflush(outf);

    malloc_size_2 = malloc_inuse(&malloc_hist_2);

    if(malloc_size_1 != malloc_size_2) {
	malloc_list(fd, malloc_hist_1, malloc_hist_2);
    }

    exit(userbad \
	 || tapebad \
	 || disklow \
	 || logbad \
	 || infobad \
	 || indexbad \
	 || pgmbad);
    /* NOTREACHED */
    return 0;
}

/* --------------------------------------------------- */

int remote_errors;
FILE *outf;

static void handle_response P((proto_t *p, pkt_t *pkt));

int start_client_checks(fd)
int fd;
{
    disklist_t *origqp;
    disk_t *dp;
    host_t *hostp;
    char *req = NULL;
    int hostcount, rc, pid;
    int amanda_port;
    struct servent *amandad;
    int userbad = 0;

#ifdef KRB4_SECURITY
    int kamanda_port;
#endif

    switch(pid = fork()) {
    case -1: error("could not fork client check: %s", strerror(errno));
    case 0: break;
    default:
	return pid;
    }

    dup2(fd, 1);
    dup2(fd, 2);

    set_pname("amcheck-clients");

    startclock();

    if((origqp = read_diskfile(getconf_str(CNF_DISKFILE))) == NULL)
	error("could not load diskfile %s\n", getconf_str(CNF_DISKFILE));

    if((outf = fdopen(fd, "w")) == NULL)
	error("fdopen %d: %s", fd, strerror(errno));
    errf = outf;

    fprintf(outf, "\nAmanda Backup Client Hosts Check\n");
    fprintf(outf,   "--------------------------------\n");

    /*
     * Make sure we are running as the dump user.
     */
    {
	uid_t uid_me = getuid();
	uid_t uid_dumpuser = uid_me;
	char *dumpuser = getconf_str(CNF_DUMPUSER);
	struct passwd *pw;

	if ((pw = getpwnam(dumpuser)) == NULL) {
	    fprintf(outf, "ERROR: cannot look up dump user \"%s\"\n", dumpuser);
	    userbad = 1;
	} else {
	    uid_dumpuser = pw->pw_uid;
	}
	if ((pw = getpwuid(uid_me)) == NULL) {
	    fprintf(outf, "ERROR: cannot look up my own uid %ld\n",
		    (long)uid_me);
	    userbad = 1;
	}
	if (uid_me != uid_dumpuser) {
	    fprintf(outf, "ERROR: running as user \"%s\" instead of \"%s\"\n",
		    pw->pw_name, dumpuser);
	    userbad = 1;
	}
    }

#ifdef KRB4_SECURITY
    kerberos_service_init();
#endif

    proto_init(msg->socket, time(0), 1024);

    /* get remote service port */
    if((amandad = getservbyname(AMANDA_SERVICE_NAME, "udp")) == NULL)
	amanda_port = AMANDA_SERVICE_DEFAULT;
    else
	amanda_port = ntohs(amandad->s_port);

#ifdef KRB4_SECURITY
    if((amandad = getservbyname(KAMANDA_SERVICE_NAME, "udp")) == NULL)
	kamanda_port = KAMANDA_SERVICE_DEFAULT;
    else
	kamanda_port = ntohs(amandad->s_port);
#endif

    hostcount = remote_errors = 0;

    while(!empty(*origqp)) {
	req = vstralloc("SERVICE selfcheck\n",
			"OPTIONS ;\n",
			NULL);
	hostp = origqp->head->host;
	for(dp = hostp->disks; dp != NULL; dp = dp->hostnext) {
	    char *t;
	    char *o;

	    remove_disk(origqp, dp);
	    o = optionstr(dp);
	    if(strncmp(dp->program,"DUMP",4) == 0 || 
	       strncmp(dp->program,"GNUTAR",6) == 0)
		t = vstralloc(req,
			      dp->program, 
			      " ",
			      dp->name,
			      " 0 OPTIONS |",
			      o,
			      "\n",
			      NULL);
	    else
		t = vstralloc(req,
			      "DUMPER ",
			      dp->program, 
			      " ",
			      dp->name,
			      " 0 OPTIONS |",
			      o,
			      "\n",
			      NULL);
	    amfree(req);
	    amfree(o);
	    req = t;
	}
	hostcount++;

#ifdef KRB4_SECURITY
	if (strcasecmp(hostp->disks->security_driver, "KRB4") == 0)
	    rc = make_krb_request(hostp->hostname, kamanda_port, req,
				  hostp, CHECK_TIMEOUT, handle_response);
	else
#endif
	    rc = make_request(hostp->hostname, amanda_port, req,
			      hostp, CHECK_TIMEOUT, handle_response);

	req = NULL;				/* do not own this any more */

	if(rc) {
	    /* couldn't resolve hostname */
	    fprintf(outf,
		    "ERROR: %s: could not resolve hostname\n", hostp->hostname);
	    remote_errors++;
	}
	check_protocol();
    }
    run_protocol();
    amfree(msg);

    fprintf(outf,
     "Client check: %d host%s checked in %s seconds, %d problem%s found\n",
	    hostcount, (hostcount == 1) ? "" : "s",
	    walltime_str(curclock()),
	    remote_errors, (remote_errors == 1) ? "" : "s");
    fflush(outf);

    malloc_size_2 = malloc_inuse(&malloc_hist_2);

    if(malloc_size_1 != malloc_size_2) {
	malloc_list(fd, malloc_hist_1, malloc_hist_2);
    }

    exit(userbad || remote_errors > 0);
    /* NOTREACHED */
    return 0;
}

static void handle_response(p, pkt)
proto_t *p;
pkt_t *pkt;
{
    host_t *hostp;
    char *errstr;
    char *s;
    int ch;

    hostp = (host_t *) p->datap;

    if(p->state == S_FAILED) {
	if(pkt == NULL) {
	    fprintf(outf,
		    "WARNING: %s: selfcheck request timed out.  Host down?\n",
		    hostp->hostname);
#define sc "ERROR"
	} else if(strncmp(pkt->body, sc, sizeof(sc)-1) == 0) {
	    s = pkt->body + sizeof(sc)-1;
	    ch = *s++;
#undef sc
	    skip_whitespace(s, ch);
	    errstr = s - 1;

	    fprintf(outf, "ERROR: %s NAK: %s\n", hostp->hostname, errstr);
	} else {
	    fprintf(outf, "ERROR: %s NAK: [NAK parse failed]\n",
		    hostp->hostname);
	}
	remote_errors++;
	return;
    }

#ifdef KRB4_SECURITY
    if (strcasecmp(hostp->disks->security_driver, "KRB4") == 0 &&
       !check_mutual_authenticator(host2key(hostp->hostname), pkt, p)) {
	fprintf(outf, "ERROR: %s [mutual-authentication failed]\n",
		hostp->hostname);
	remote_errors++;
	return;
    }
#endif

    s = pkt->body;
    ch = *s++;
/*
    fprintf(errf, "got response from %s:\n----\n%s----\n\n",
	    hostp->hostname, resp);
*/

#define sc "OPTIONS"
    if(strncmp(s - 1, sc, sizeof(sc)-1) == 0) {
	/* no options yet */
	skip_line(s, ch);
    }
#undef sc

    while(ch) {
#define sc "ERROR"
	if(strncmp(s - 1, sc, sizeof(sc)-1) == 0) {
	    s += sizeof(sc)-1;
	    ch = s[-1];
#undef sc

	    skip_whitespace(s, ch);
	    errstr = s - 1;
	    skip_line(s, ch);
	    /* overwrite '\n'; s-1 points to the beginning of the next line */
	    s[-2] = '\0';

	    fprintf(outf, "ERROR: %s: %s\n", hostp->hostname, errstr);
	    remote_errors++;
	} else {
	    skip_line(s, ch);
	}
    }
}
