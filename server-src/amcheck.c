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
 * $Id: amcheck.c,v 1.50.2.19.2.7.2.20.2.8 2004/11/19 13:21:49 martinea Exp $
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
#include "token.h"
#include "util.h"
#include "pipespawn.h"
#include "amfeatures.h"

/*
 * If we don't have the new-style wait access functions, use our own,
 * compatible with old-style BSD systems at least.  Note that we don't
 * care about the case w_stopval == WSTOPPED since we don't ask to see
 * stopped processes, so should never get them from wait.
 */
#ifndef WEXITSTATUS
#   define WEXITSTATUS(r)       (((union wait *) &(r))->w_retcode)
#   define WTERMSIG(r)          (((union wait *) &(r))->w_termsig)

#   undef WIFSIGNALED
#   define WIFSIGNALED(r)        (((union wait *) &(r))->w_termsig != 0)
#endif

#define BUFFER_SIZE	32768

static int conf_ctimeout;
static int overwrite;
dgram_t *msg = NULL;

static disklist_t *origqp;

static uid_t uid_dumpuser;

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
    error("Usage: amcheck%s [-M <username>] [-mawsclt] <conf> [host [disk]* ]*", versionsuffix());
}

static unsigned long malloc_hist_1, malloc_size_1;
static unsigned long malloc_hist_2, malloc_size_2;

static am_feature_t *our_features = NULL;
static char *our_feature_string = NULL;
static char *displayunit;
static long int unitdivisor;

int main(argc, argv)
int argc;
char **argv;
{
    char buffer[BUFFER_SIZE];
    char *version_string;
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
    int alwaysmail;
    char *tempfname = NULL;
    char *conffile;
    char *conf_diskfile;
    char *dumpuser;
    struct passwd *pw;
    uid_t uid_me;

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

    set_pname("amcheck");
    dbopen();

    malloc_size_1 = malloc_inuse(&malloc_hist_1);

    ap_snprintf(pid_str, sizeof(pid_str), "%ld", (long)getpid());

    erroutput_type = ERR_INTERACTIVE;

    our_features = am_init_feature_set();
    our_feature_string = am_feature_to_string(our_features);

    /* set up dgram port first thing */

    msg = dgram_alloc();

    if(dgram_bind(msg, &result_port) == -1)
	error("could not bind result datagram port: %s", strerror(errno));

    if(geteuid() == 0) {
	/* set both real and effective uid's to real uid, likewise for gid */
	setgid(getgid());
	setuid(getuid());
    }
    uid_me = getuid();

    alwaysmail = mailout = overwrite = 0;
    do_localchk = do_tapechk = do_clientchk = 0;
    chk_flag = 0;
    server_probs = client_probs = 0;
    tempfd = mainfd = -1;

    /* process arguments */

    while((opt = getopt(argc, argv, "M:mawsclt")) != EOF) {
	switch(opt) {
	case 'M':	mailto=stralloc(optarg);
	case 'm':	
#ifdef MAILER
			mailout = 1;
#else
			printf("You can't use -%c because configure didn't find a mailer.\n",
				opt);
			exit(1);
#endif
			break;
	case 'a':	
#ifdef MAILER
			mailout = 1;
			alwaysmail = 1;
#else
			printf("You can't use -%c because configure didn't find a mailer.\n",
				opt);
			exit(1);
#endif
			break;
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

    if(argc < 1) usage();

    config_name = stralloc(*argv);

    config_dir = vstralloc(CONFIG_DIR, "/", config_name, "/", NULL);
    conffile = stralloc2(config_dir, CONFFILE_NAME);
    if(read_conffile(conffile)) {
	error("errors processing config file \"%s\"", conffile);
    }
    amfree(conffile);
    conf_ctimeout = getconf_int(CNF_CTIMEOUT);
    conf_diskfile = getconf_str(CNF_DISKFILE);
    if (*conf_diskfile == '/') {
	conf_diskfile = stralloc(conf_diskfile);
    } else {
	conf_diskfile = stralloc2(config_dir, conf_diskfile);
    }
    if((origqp = read_diskfile(conf_diskfile)) == NULL) {
	error("could not load disklist %s", conf_diskfile);
    }
    match_disklist(origqp, argc-1, argv+1);
    amfree(conf_diskfile);

    /*
     * Make sure we are running as the dump user.
     */
    dumpuser = getconf_str(CNF_DUMPUSER);
    if ((pw = getpwnam(dumpuser)) == NULL) {
	error("cannot look up dump user \"%s\"", dumpuser);
    }
    uid_dumpuser = pw->pw_uid;
    if ((pw = getpwuid(uid_me)) == NULL) {
	error("cannot look up my own uid (%ld)", (long)uid_me);
    }
    if (uid_me != uid_dumpuser) {
	error("running as user \"%s\" instead of \"%s\"",
	      pw->pw_name,
	      dumpuser);
    }

    displayunit = getconf_str(CNF_DISPLAYUNIT);
    unitdivisor = getcont_unit_divisor();

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
	tempfname = vstralloc(AMANDA_TMPDIR, "/amcheck.temp.", pid_str, NULL);
	if((tempfd = open(tempfname, O_RDWR|O_CREAT|O_TRUNC, 0600)) == -1)
	    error("could not open %s: %s", tempfname, strerror(errno));
	unlink(tempfname);			/* so it goes away on close */
	amfree(tempfname);
    }

    if(mailout) {
	/* the main fd is a file too */
	mainfname = vstralloc(AMANDA_TMPDIR, "/amcheck.main.", pid_str, NULL);
	if((mainfd = open(mainfname, O_RDWR|O_CREAT|O_TRUNC, 0600)) == -1)
	    error("could not open %s: %s", mainfname, strerror(errno));
	unlink(mainfname);			/* so it goes away on close */
	amfree(mainfname);
    }
    else
	/* just use stdout */
	mainfd = 1;

    /* start server side checks */

    if(do_localchk || do_tapechk) {
	serverchk_pid = start_server_check(mainfd, do_localchk, do_tapechk);
    } else {
	serverchk_pid = 0;
    }

    /* start client side checks */

    if(do_clientchk) {
	clientchk_pid = start_client_checks((do_localchk || do_tapechk) ? tempfd : mainfd);
    } else {
	clientchk_pid = 0;
    }

    /* wait for child processes and note any problems */

    while(1) {
	if((pid = wait(&retstat)) == -1) {
	    if(errno == EINTR) continue;
	    else break;
	} else if(pid == clientchk_pid) {
	    client_probs = WIFSIGNALED(retstat) || WEXITSTATUS(retstat);
	    clientchk_pid = 0;
	} else if(pid == serverchk_pid) {
	    server_probs = WIFSIGNALED(retstat) || WEXITSTATUS(retstat);
	    serverchk_pid = 0;
	} else {
	    char number[NUM_STR_SIZE];
	    char *wait_msg = NULL;

	    ap_snprintf(number, sizeof(number), "%ld", (long)pid);
	    wait_msg = vstralloc("parent: reaped bogus pid ", number, "\n",
				 NULL);
	    for(l = 0, n = strlen(wait_msg); l < n; l += s) {
		if((s = write(mainfd, wait_msg + l, n - l)) < 0) {
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
    }

    version_string = vstralloc("\n",
			       "(brought to you by Amanda ", version(), ")\n",
			       NULL);
    for(l = 0, n = strlen(version_string); l < n; l += s) {
	if((s = write(mainfd, version_string + l, n - l)) < 0) {
	    error("write main file: %s", strerror(errno));
	}
    }
    amfree(version_string);
    amfree(config_dir);
    amfree(config_name);
    amfree(our_feature_string);
    am_release_feature_set(our_features);
    our_features = NULL;

    malloc_size_2 = malloc_inuse(&malloc_hist_2);

    if(malloc_size_1 != malloc_size_2) {
	malloc_list(fileno(stderr), malloc_hist_1, malloc_hist_2);
    }

    /* send mail if requested, but only if there were problems */
#ifdef MAILER

#define	MAILTO_LIMIT	10

    if((server_probs || client_probs || alwaysmail) && mailout) {
	int mailfd;
	int nullfd;
	int errfd;
	FILE *ferr;
	char *subject;
	char **a;
	amwait_t retstat;
	pid_t mailpid;
	pid_t wpid;
	int r;
	int w;
	char *err = NULL;
	char *extra_info = NULL;
	char *line = NULL;
	int ret;
	int rc;
	int sig;
	char number[NUM_STR_SIZE];

	fflush(stdout);
	if(lseek(mainfd, (off_t)0, SEEK_SET) == -1) {
	    error("lseek main file: %s", strerror(errno));
	}
	if(alwaysmail && !(server_probs || client_probs)) {
	    subject = stralloc2(getconf_str(CNF_ORG),
			    " AMCHECK REPORT: NO PROBLEMS FOUND");
	} else {
	    subject = stralloc2(getconf_str(CNF_ORG),
			    " AMANDA PROBLEM: FIX BEFORE RUN, IF POSSIBLE");
	}
	/*
	 * Variable arg lists are hard to deal with when we do not know
	 * ourself how many args are involved.  Split the address list
	 * and hope there are not more than 9 entries.
	 *
	 * Remember that split() returns the original input string in
	 * argv[0], so we have to skip over that.
	 */
	a = (char **) alloc((MAILTO_LIMIT + 1) * sizeof(char *));
	memset(a, 0, (MAILTO_LIMIT + 1) * sizeof(char *));
	if(mailto) {
	    a[1] = mailto;
	    a[2] = NULL;
	} else {
	    n = split(getconf_str(CNF_MAILTO), a, MAILTO_LIMIT, " ");
	    a[n + 1] = NULL;
	}
	if((nullfd = open("/dev/null", O_RDWR)) < 0) {
	    error("nullfd: /dev/null: %s", strerror(errno));
	}
	mailpid = pipespawn(MAILER, STDIN_PIPE | STDERR_PIPE,
			    &mailfd, &nullfd, &errfd,
			    MAILER,
			    "-s", subject,
			          a[1], a[2], a[3], a[4],
			    a[5], a[6], a[7], a[8], a[9],
			    NULL);
	amfree(subject);
	/*
	 * There is the potential for a deadlock here since we are writing
	 * to the process and then reading stderr, but in the normal case,
	 * nothing should be coming back to us, and hopefully in error
	 * cases, the pipe will break and we will exit out of the loop.
	 */
	signal(SIGPIPE, SIG_IGN);
	while((r = fullread(mainfd, buffer, sizeof(buffer))) > 0) {
	    if((w = fullwrite(mailfd, buffer, r)) != r) {
		if(w < 0 && errno == EPIPE) {
		    strappend(extra_info, "EPIPE writing to mail process\n");
		    break;
		} else if(w < 0) {
		    error("mailfd write: %s", strerror(errno));
		} else {
		    error("mailfd write: wrote %d instead of %d", w, r);
		}
	    }
	}
	aclose(mailfd);
	ferr = fdopen(errfd, "r");
	for(; (line = agets(ferr)) != NULL; free(line)) {
	    strappend(extra_info, line);
	    strappend(extra_info, "\n");
	}
	afclose(ferr);
	errfd = -1;
	rc = 0;
	while ((wpid = wait(&retstat)) != -1) {
	    if (WIFSIGNALED(retstat)) {
		    ret = 0;
		    rc = sig = WTERMSIG(retstat);
	    } else {
		    sig = 0;
		    rc = ret = WEXITSTATUS(retstat);
	    }
	    if (rc != 0) {
		    if (ret == 0) {
			strappend(err, "got signal ");
			ret = sig;
		    } else {
			strappend(err, "returned ");
		    }
		    ap_snprintf(number, sizeof(number), "%d", ret);
		    strappend(err, number);
	    }
	}
	if (rc != 0) {
	    if(extra_info) {
		fputs(extra_info, stderr);
		amfree(extra_info);
	    }
	    error("error running mailer %s: %s", MAILER, err);
	}
    }
#endif
    dbclose();
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
	    if(searchlabel != NULL
	       && (strcmp(label, FAKE_LABEL) == 0
		   || strcmp(label, searchlabel) == 0)) {
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
		if(tp == NULL)
		    fprintf(errf, " (Not in tapelist)\n");
		else if(!reusable_tape(tp))
		    fprintf(errf, " (active tape)\n");
		else if(got_match == 0 && tp->datestamp == 0) {
		    got_match = 1;
		    first_match = newstralloc(first_match, slotstr);
		    first_match_label = newstralloc(first_match_label, label);
		    fprintf(errf, " (new tape)\n");
		    found = 3;
		    found_device = newstralloc(found_device, device);
		    return 1;
		}
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

    if(found == 2 || found == 3)
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
    int confbad = 0, tapebad = 0, disklow = 0, logbad = 0;
    int userbad = 0, infobad = 0, indexbad = 0, pgmbad = 0;
    int testtape = do_tapechk;

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
     * Check various server side config file settings.
     */
    if(do_localchk) {
	char *ColumnSpec;
	char *errstr = NULL;
	tapetype_t *tp;
	char *lbl_templ;

	ColumnSpec = getconf_str(CNF_COLUMNSPEC);
	if(SetColumDataFromString(ColumnData, ColumnSpec, &errstr) < 0) {
	    fprintf(outf, "ERROR: %s\n", errstr);
	    amfree(errstr);
	    confbad = 1;
	}
	tp = lookup_tapetype(getconf_str(CNF_TAPETYPE));
	lbl_templ = tp->lbl_templ;
	if(strcmp(lbl_templ, "") != 0) {
	    if(strchr(lbl_templ, '/') == NULL) {
		lbl_templ = stralloc2(config_dir, lbl_templ);
	    } else {
		lbl_templ = stralloc(lbl_templ);
	    }
	    if(access(lbl_templ, R_OK) == -1) {
		fprintf(outf,
			"ERROR: cannot access lbl_templ file %s: %s\n",
			lbl_templ,
			strerror(errno));
		confbad = 1;
	    }
#if !defined(LPRCMD)
	    fprintf(outf, "ERROR: lbl_templ set but no LPRCMD defined, you should reconfigure amanda\n       and make sure it find a lpr or lp command.\n");
	    confbad = 1;
#endif
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
		     || test_server_pgm(outf, libexecdir, "amtrmidx",
					0, uid_dumpuser);
	    pgmbad = pgmbad \
		     || test_server_pgm(outf, libexecdir, "amlogroll",
					0, uid_dumpuser);
	}
	if(access(sbindir, X_OK) == -1) {
	    fprintf(outf, "ERROR: program dir %s: not accessible\n",
		    sbindir);
	    pgmbad = 1;
	} else {
	    pgmbad = pgmbad \
		     || test_server_pgm(outf, sbindir, "amgetconf",
					0, uid_dumpuser);
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
	if(access(COMPRESS_PATH, X_OK) == -1) {
	    fprintf(outf, "WARNING: %s is not executable, server-compression and indexing will not work\n",
	            COMPRESS_PATH);
	}
    }

    /*
     * Check that the directory for the tapelist file is writable, as well
     * as the tapelist file itself (if it already exists).  Also, check for
     * a "hold" file (just because it is convenient to do it here) and warn
     * if tapedev is set to the null device.
     */

    if(do_localchk || do_tapechk) {
	char *conf_tapelist;
	char *tapefile;
	char *tape_dir;
	char *lastslash;
	char *holdfile;

	conf_tapelist=getconf_str(CNF_TAPELIST);
	if (*conf_tapelist == '/') {
	    tapefile = stralloc(conf_tapelist);
	} else {
	    tapefile = stralloc2(config_dir, conf_tapelist);
	}
	/*
	 * XXX There Really Ought to be some error-checking here... dhw
	 */
	tape_dir = stralloc(tapefile);
	if ((lastslash = strrchr((const char *)tape_dir, '/')) != NULL) {
	    *lastslash = '\0';
	/*
	 * else whine Really Loudly about a path with no slashes??!?
	 */
	}
	if(access(tape_dir, W_OK) == -1) {
	    fprintf(outf, "ERROR: tapelist dir %s: not writable\n", tape_dir);
	    tapebad = 1;
	} else if(access(tapefile, F_OK) == 0 && access(tapefile, W_OK) != 0) {
	    fprintf(outf, "ERROR: tape list %s: not writable\n", tapefile);
	    tapebad = 1;
	} else if(read_tapelist(tapefile)) {
	    fprintf(outf, "ERROR: tape list %s: parse error\n", tapefile);
	    tapebad = 1;
	}
	holdfile = vstralloc(config_dir, "/", "hold", NULL);
	if(access(holdfile, F_OK) != -1) {
	    fprintf(outf, "WARNING: hold file %s exists\n", holdfile);
	}
	amfree(tapefile);
	amfree(tape_dir);
	amfree(holdfile);
	tapename = getconf_str(CNF_TAPEDEV);
	if (strncmp(tapename, "null:", 5) == 0) {
	    fprintf(outf,
		    "WARNING: tapedev is %s, dumps will be thrown away\n",
		    tapename);
	    testtape = 0;
	    do_tapechk = 0;
	}
    }

    /* check available disk space */

    if(do_localchk) {
	for(hdp = holdingdisks; hdp != NULL; hdp = hdp->next) {
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
			hdp->diskdir, (long)hdp->disksize);
		disklow = 1;
	    }
	    else if(hdp->disksize > 0) {
		if(fs.avail < hdp->disksize) {
		    fprintf(outf,
			    "WARNING: holding disk %s: only %ld %sB free (%ld %sB requested)\n",
			    hdp->diskdir, (long)fs.avail/unitdivisor, displayunit,
			    (long)hdp->disksize/unitdivisor, displayunit);
		    disklow = 1;
		}
		else
		    fprintf(outf,
			    "Holding disk %s: %ld %sB disk space available, that's plenty\n",
			    hdp->diskdir, fs.avail/unitdivisor, displayunit);
	    }
	    else {
		if(fs.avail < -hdp->disksize) {
		    fprintf(outf,
			    "WARNING: holding disk %s: only %ld %sB free, using nothing\n",
			    hdp->diskdir, fs.avail/unitdivisor, displayunit);
		    disklow = 1;
		}
		else
		    fprintf(outf,
			    "Holding disk %s: %ld %sB disk space available, using %ld %sB\n",
			    hdp->diskdir, fs.avail/unitdivisor, displayunit,
			    (fs.avail + hdp->disksize)/unitdivisor, displayunit);
	    }
	}
    }

    /* check that the log file is writable if it already exists */

    if(do_localchk) {
	char *conf_logdir;
	char *logfile;
	char *olddir;
	struct stat stat_old;

	conf_logdir = getconf_str(CNF_LOGDIR);
	if (*conf_logdir == '/') {
	    conf_logdir = stralloc(conf_logdir);
	} else {
	    conf_logdir = stralloc2(config_dir, conf_logdir);
	}
	logfile = vstralloc(conf_logdir, "/log", NULL);

	if(access(conf_logdir, W_OK) == -1) {
	    fprintf(outf, "ERROR: log dir %s: not writable\n", conf_logdir);
	    logbad = 1;
	}

	if(access(logfile, F_OK) == 0) {
	    testtape = 0;
	    logbad = 1;
	    if(access(logfile, W_OK) != 0)
		fprintf(outf, "ERROR: log file %s: not writable\n",
			logfile);
	}

	olddir = vstralloc(conf_logdir, "/oldlog", NULL);
	if (stat(olddir,&stat_old) == 0) { /* oldlog exist */
	    if(!(S_ISDIR(stat_old.st_mode))) {
		fprintf(outf, "ERROR: oldlog directory \"%s\" is not a directory\n", olddir);
	    }
	    if(access(olddir, W_OK) == -1) {
		fprintf(outf, "ERROR: oldlog dir %s: not writable\n", olddir);
	    }
	}
	else if(lstat(olddir,&stat_old) == 0) {
	    fprintf(outf, "ERROR: oldlog directory \"%s\" is not a directory\n", olddir);
	}

	if (testtape) {
	    logfile = newvstralloc(logfile, conf_logdir, "/amdump", NULL);
	    if (access(logfile, F_OK) == 0) {
		testtape = 0;
		logbad = 1;
	    }
	}

	amfree(logfile);
	amfree(conf_logdir);
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
	} else if(tape_access(tapename,F_OK|R_OK|W_OK) == -1) {
	    fprintf(outf, "ERROR: %s: %s\n", tapename, strerror(errno));
	    tapebad = 1;
	} else if((errstr = tape_rdlabel(tapename, &datestamp, &label)) != NULL) {
	    fprintf(outf, "ERROR: %s: %s\n", tapename, errstr);
	    tapebad = 1;
	} else if(strcmp(label, FAKE_LABEL) != 0) {
	    if(!match(labelstr, label)) {
		fprintf(outf, "ERROR: label %s doesn't match labelstr \"%s\"\n",
			label, labelstr);
		tapebad = 1;
	    }
	    else {
		tp = lookup_tapelabel(label);
		if(tp == NULL) {
		    fprintf(outf, "ERROR: label %s match labelstr but it not listed in the tapelist file.\n", label);
		    tapebad = 1;
		}
		else if(tp != NULL && !reusable_tape(tp)) {
		    fprintf(outf, "ERROR: cannot overwrite active tape %s\n",
			    label);
		    tapebad = 1;
		}
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
	char *conf_infofile;
	char *conf_indexdir;
	char *hostinfodir = NULL;
	char *hostindexdir = NULL;
	char *diskdir = NULL;
	char *infofile = NULL;
	struct stat statbuf;
	disk_t *dp;
	am_host_t *hostp;
	int indexdir_checked = 0;
	int hostindexdir_checked = 0;
	char *host;
	char *disk;
	int conf_tapecycle, conf_runspercycle;

	conf_tapecycle = getconf_int(CNF_TAPECYCLE);
	conf_runspercycle = getconf_int(CNF_RUNSPERCYCLE);

	if(conf_tapecycle <= conf_runspercycle) {
		fprintf(outf, "WARNING: tapecycle (%d) <= runspercycle (%d).\n",
			conf_tapecycle, conf_runspercycle);
	}

	conf_infofile = stralloc(getconf_str(CNF_INFOFILE));
	if (*conf_infofile != '/') {
	    char *ci = stralloc2(config_dir, conf_infofile);
	    amfree(conf_infofile);
	    conf_infofile = ci;
	}
	conf_indexdir = stralloc(getconf_str(CNF_INDEXDIR));
	if (*conf_indexdir != '/') {
	    char *ci = stralloc2(config_dir, conf_indexdir);
	    amfree(conf_indexdir);
	    conf_indexdir = ci;
	}
#if TEXTDB
	if(stat(conf_infofile, &statbuf) == -1) {
	    fprintf(outf, "NOTE: info dir %s: does not exist\n", conf_infofile);
	    fprintf(outf, "NOTE: it will be created on the next run\n");
	    amfree(conf_infofile);
	} else if (!S_ISDIR(statbuf.st_mode)) {
	    fprintf(outf, "ERROR: info dir %s: not a directory\n", conf_infofile);
	    amfree(conf_infofile);
	    infobad = 1;
	} else if (access(conf_infofile, W_OK) == -1) {
	    fprintf(outf, "ERROR: info dir %s: not writable\n", conf_infofile);
	    amfree(conf_infofile);
	    infobad = 1;
	} else {
	    strappend(conf_infofile, "/");
	}
#endif
	while(!empty(*origqp)) {
	    hostp = origqp->head->host;
	    host = sanitise_filename(hostp->hostname);
#if TEXTDB
	    if(conf_infofile) {
		hostinfodir = newstralloc2(hostinfodir, conf_infofile, host);
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
			if(stat(conf_indexdir, &statbuf) == -1) {
			    fprintf(outf, "NOTE: index dir %s: does not exist\n",
				    conf_indexdir);
			    amfree(conf_indexdir);
			} else if (!S_ISDIR(statbuf.st_mode)) {
			    fprintf(outf, "ERROR: index dir %s: not a directory\n",
				    conf_indexdir);
			    amfree(conf_indexdir);
			    indexbad = 1;
			} else if (access(conf_indexdir, W_OK) == -1) {
			    fprintf(outf, "ERROR: index dir %s: not writable\n",
				    conf_indexdir);
			    amfree(conf_indexdir);
			    indexbad = 1;
			} else {
			    strappend(conf_indexdir, "/");
			}
			indexdir_checked = 1;
		    }
		    if(conf_indexdir) {
			if(! hostindexdir_checked) {
			    hostindexdir = stralloc2(conf_indexdir, host);
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
	amfree(conf_infofile);
	amfree(conf_indexdir);
    }

    amfree(datestamp);
    amfree(label);
    amfree(config_dir);
    amfree(config_name);

    fprintf(outf, "Server check took %s seconds\n", walltime_str(curclock()));

    fflush(outf);

    malloc_size_2 = malloc_inuse(&malloc_hist_2);

    if(malloc_size_1 != malloc_size_2) {
	malloc_list(fd, malloc_hist_1, malloc_hist_2);
    }

    exit(userbad \
	 || confbad \
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
int amanda_port;

#ifdef KRB4_SECURITY
int kamanda_port;
#endif

static void handle_response P((proto_t *p, pkt_t *pkt));

#define HOST_READY				((void *)0)	/* must be 0 */
#define HOST_ACTIVE				((void *)1)
#define HOST_DONE				((void *)2)

#define DISK_READY				((void *)0)	/* must be 0 */
#define DISK_ACTIVE				((void *)1)
#define DISK_DONE				((void *)2)

int start_host(hostp)
    am_host_t *hostp;
{
    disk_t *dp;
    char *req = NULL;
    int req_len = 0;
    int rc;
    int disk_count;
    char number[NUM_STR_SIZE];

    if(hostp->up != HOST_READY) {
	return 0;
    }

    if (strncmp (hostp->hostname,"localhost",9) == 0) {
        fprintf(outf,
                    "WARNING: Usage of fully qualified hostname recommended for Client %s.\n",
                    hostp->hostname);
    }

    /*
     * The first time through here we send a "noop" request.  This will
     * return the feature list from the client if it supports that.
     * If it does not, handle_result() will set the feature list to an
     * empty structure.  In either case, we do the disks on the second
     * (and subsequent) pass(es).
     */
    disk_count = 0;
    if(hostp->features != NULL) { /* selfcheck service */
	int has_features = am_has_feature(hostp->features,
					  fe_req_options_features);
	int has_hostname = am_has_feature(hostp->features,
					  fe_req_options_hostname);
	int has_maxdumps = am_has_feature(hostp->features,
					  fe_req_options_maxdumps);

	if(!am_has_feature(hostp->features, fe_selfcheck_req) &&
	   !am_has_feature(hostp->features, fe_selfcheck_req_device)) {
	    fprintf(outf,
		    "ERROR: Client %s does not support selfcheck REQ packet.\n",
		    hostp->hostname);
	}
	if(!am_has_feature(hostp->features, fe_selfcheck_rep)) {
	    fprintf(outf,
		    "ERROR: Client %s does not support selfcheck REP packet.\n",
		    hostp->hostname);
	}
	if(!am_has_feature(hostp->features, fe_sendsize_req_options) &&
	   !am_has_feature(hostp->features, fe_sendsize_req_no_options) &&
	   !am_has_feature(hostp->features, fe_sendsize_req_device)) {
	    fprintf(outf,
		    "ERROR: Client %s does not support sendsize REQ packet.\n",
		    hostp->hostname);
	}
	if(!am_has_feature(hostp->features, fe_sendsize_rep)) {
	    fprintf(outf,
		    "ERROR: Client %s does not support sendsize REP packet.\n",
		    hostp->hostname);
	}
	if(!am_has_feature(hostp->features, fe_sendbackup_req) &&
	   !am_has_feature(hostp->features, fe_sendbackup_req_device)) {
	    fprintf(outf,
		   "ERROR: Client %s does not support sendbackup REQ packet.\n",
		   hostp->hostname);
	}
	if(!am_has_feature(hostp->features, fe_sendbackup_rep)) {
	    fprintf(outf,
		   "ERROR: Client %s does not support sendbackup REP packet.\n",
		   hostp->hostname);
	}

	ap_snprintf(number, sizeof(number), "%d", hostp->maxdumps);
	req = vstralloc("SERVICE ", "selfcheck", "\n",
			"OPTIONS ",
			has_features ? "features=" : "",
			has_features ? our_feature_string : "",
			has_features ? ";" : "",
			has_maxdumps ? "maxdumps=" : "",
			has_maxdumps ? number : "",
			has_maxdumps ? ";" : "",
			has_hostname ? "hostname=" : "",
			has_hostname ? hostp->hostname : "",
			has_hostname ? ";" : "",
			"\n",
			NULL);

	req_len = strlen(req);
	req_len += 128;				/* room for SECURITY ... */
	req_len += 256;				/* room for non-disk answers */
	for(dp = hostp->disks; dp != NULL; dp = dp->hostnext) {
	    char *l;
	    int l_len;
	    char *o;
	    char* calcsize;

	    if(dp->todo == 0) continue;

	    if(dp->up != DISK_READY) {
		continue;
	    }
	    o = optionstr(dp, hostp->features, outf);

	    if(dp->device) {
		if(!am_has_feature(hostp->features, fe_selfcheck_req_device)) {
		    fprintf(outf,
		     "ERROR: %s:%s (%s): selfcheck does not support device.\n",
		     hostp->hostname, dp->name, dp->device);
		}
		if(!am_has_feature(hostp->features, fe_sendsize_req_device)) {
		    fprintf(outf,
		     "ERROR: %s:%s (%s): sendsize does not support device.\n",
		     hostp->hostname, dp->name, dp->device);
		}
		if(!am_has_feature(hostp->features, fe_sendbackup_req_device)) {
		    fprintf(outf,
		     "ERROR: %s:%s (%s): sendbackup does not support device.\n",
		     hostp->hostname, dp->name, dp->device);
		}
	    }
	    if(strcmp(dp->program, "DUMP") == 0 &&
	       !am_has_feature(hostp->features, fe_program_dump)) {
		fprintf(outf, "ERROR: %s:%s does not support DUMP.\n",
			hostp->hostname, dp->name);
	    }
	    if(strcmp(dp->program, "GNUTAR") == 0 &&
	       !am_has_feature(hostp->features, fe_program_gnutar)) {
		fprintf(outf, "ERROR: %s:%s does not support GNUTAR.\n",
			hostp->hostname, dp->name);
	    }
	    if(dp->estimate == ES_CALCSIZE &&
	       !am_has_feature(hostp->features, fe_calcsize_estimate)) {
		fprintf(outf, "ERROR: %s:%s does not support CALCSIZE for estimate, using CLIENT.\n",
			hostp->hostname, dp->name);
		dp->estimate = ES_CLIENT;
	    }

	    if(dp->estimate == ES_CALCSIZE &&
	       am_has_feature(hostp->features, fe_selfcheck_calcsize))
		calcsize = "CALCSIZE ";
	    else
		calcsize = "";

	    l = vstralloc(calcsize, 
			  dp->program, " ",
			  dp->name, " ",
			  dp->device ? dp->device : "",
			  " 0 OPTIONS |",
			  o,
			  "\n",
			  NULL);
	    l_len = strlen(l);
	    amfree(o);
	    /*
	     * Allow 2X for error response in return packet.
	     */
	    if(req_len + l_len > MAX_DGRAM / 2) {
		amfree(l);
		break;
	    }
	    strappend(req, l);
	    req_len += l_len;
	    amfree(l);
	    dp->up = DISK_ACTIVE;
	    disk_count++;
	}

    }
    else { /* noop service */
	req = vstralloc("SERVICE ", "noop", "\n",
			"OPTIONS ",
			"features=", our_feature_string, ";",
			"\n",
			NULL);
	for(dp = hostp->disks; dp != NULL; dp = dp->hostnext) {
	    if(dp->todo == 0) continue;

	    if(dp->up != DISK_READY) {
		continue;
	    }
	    disk_count++;
	}
    }
    if(disk_count == 0) {
	amfree(req);
	hostp->up = HOST_DONE;
	return 0;
    }

#ifdef KRB4_SECURITY
    if(hostp->disks->auth == AUTH_KRB4)
	rc = make_krb_request(hostp->hostname, kamanda_port, req,
			      hostp, conf_ctimeout, handle_response);
    else
#endif
        rc = make_request(hostp->hostname, amanda_port, req,
			  hostp, conf_ctimeout, handle_response);

    req = NULL;				/* do not own this any more */

    if(rc) {
	/* couldn't resolve hostname */
	fprintf(outf,
		"ERROR: %s: could not resolve hostname\n", hostp->hostname);
	remote_errors++;
	hostp->up = HOST_DONE;
    } else {
	hostp->up = HOST_ACTIVE;
    }
    return 1;
}

int start_client_checks(fd)
int fd;
{
    am_host_t *hostp;
    disk_t *dp;
    int hostcount, pid;
    struct servent *amandad;
    int userbad = 0;

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

    if((outf = fdopen(fd, "w")) == NULL)
	error("fdopen %d: %s", fd, strerror(errno));
    errf = outf;

    fprintf(outf, "\nAmanda Backup Client Hosts Check\n");
    fprintf(outf,   "--------------------------------\n");

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

    for(dp = origqp->head; dp != NULL; dp = dp->next) {
	hostp = dp->host;
	if(hostp->up == HOST_READY) {
	    if(start_host(hostp) == 1) {
		hostcount++;
		check_protocol();
	    }
	}
    }

    run_protocol();
    amfree(msg);

    fprintf(outf,
     "Client check: %d host%s checked in %s seconds, %d problem%s found\n",
	    hostcount, (hostcount == 1) ? "" : "s",
	    walltime_str(curclock()),
	    remote_errors, (remote_errors == 1) ? "" : "s");
    fflush(outf);

    amfree(config_dir);
    amfree(config_name);

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
    am_host_t *hostp;
    disk_t *dp;
    char *line;
    char *s;
    char *t;
    int ch;
    int tch;

    hostp = (am_host_t *) p->datap;
    hostp->up = HOST_READY;

    if(p->state == S_FAILED && pkt == NULL) {
	if(p->prevstate == S_REPWAIT) {
	    fprintf(outf,
		    "WARNING: %s: selfcheck reply timed out.\n",
		    hostp->hostname);
	}
	else {
	    fprintf(outf,
		    "WARNING: %s: selfcheck request timed out.  Host down?\n",
		    hostp->hostname);
	}
	remote_errors++;
	hostp->up = HOST_DONE;
	return;
    }

#ifdef KRB4_SECURITY
    if(hostp->disks->auth == AUTH_KRB4 &&
       !check_mutual_authenticator(host2key(hostp->hostname), pkt, p)) {
	fprintf(outf, "ERROR: %s [mutual-authentication failed]\n",
		hostp->hostname);
	remote_errors++;
	hostp->up = HOST_DONE;
	return;
    }
#endif

#if 0
    fprintf(errf, "got %sresponse from %s:\n----\n%s----\n\n",
	    (p->state == S_FAILED) ? "NAK " : "", hostp->hostname, pkt->body);
#endif

    s = pkt->body;
    ch = *s++;
    while(ch) {
	line = s - 1;
	skip_line(s, ch);
	if (s[-2] == '\n') {
	    s[-2] = '\0';
	}

#define sc "OPTIONS "
	if(strncmp(line, sc, sizeof(sc)-1) == 0) {
#undef sc

#define sc "features="
	    t = strstr(line, sc);
	    if(t != NULL && (isspace((int)t[-1]) || t[-1] == ';')) {
		t += sizeof(sc)-1;
#undef sc
		am_release_feature_set(hostp->features);
		if((hostp->features = am_string_to_feature(t)) == NULL) {
		    fprintf(outf, "ERROR: %s: bad features value: %s\n",
			    hostp->hostname, line);
		}
	    }

	    continue;
	}

#define sc "OK "
	if(strncmp(line, sc, sizeof(sc)-1) == 0) {
#undef sc
	    continue;
	}

#define sc "ERROR "
	if(strncmp(line, sc, sizeof(sc)-1) == 0) {
	    t = line + sizeof(sc)-1;
	    tch = t[-1];
#undef sc

	    skip_whitespace(t, tch);
	    /*
	     * If the "error" is that the "noop" service is unknown, it
	     * just means the client is "old" (does not support the service).
	     * We can ignore this.
	     */
	    if(hostp->features == NULL
	       && p->state == S_FAILED
	       && (strcmp(t - 1, "unknown service: noop") == 0
	           || strcmp(t - 1, "noop: invalid service") == 0)) {
	    } else {
		fprintf(outf, "ERROR: %s%s: %s\n",
		        (p->state == S_FAILED) ? "NAK " : "",
		        hostp->hostname,
		        t - 1);
		remote_errors++;
		hostp->up = HOST_DONE;
	    }
	    continue;
	}

	fprintf(outf, "ERROR: %s: unknown response: %s\n",
		hostp->hostname, line);
	remote_errors++;
	hostp->up = HOST_DONE;
    }
    if(hostp->up == HOST_READY && hostp->features == NULL) {
	/*
	 * The client does not support the features list, so give it an
	 * empty one.
	 */
	dbprintf(("%s: no feature set from host %s\n",
		  debug_prefix_time(NULL), hostp->hostname));
	hostp->features = am_set_default_feature_set();
    }
    for(dp = hostp->disks; dp != NULL; dp = dp->hostnext) {
	if(dp->up == DISK_ACTIVE) {
	    dp->up = DISK_DONE;
	}
    }
    start_host(hostp);
}
