/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991,1993 University of Maryland
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
 * $Id: amcheck.c,v 1.30.2.5 1998/04/08 16:26:34 amcore Exp $
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

#define CHECK_TIMEOUT   30
#define BUFFER_SIZE	32768

char *pname = "amcheck";

static int mailout, overwrite;
dgram_t *msg = NULL;

/* local functions */

void usage P((void));
int start_client_checks P((int fd));
int start_server_check P((int fd));
int main P((int argc, char **argv));
int scan_init P((int rc, int ns, int bk));
int taperscan_slot P((int rc, char *slotstr, char *device));
char *taper_scan P((void));

void usage()
{
    error("Usage: amcheck%s [-mwsc] <conf>", versionsuffix());
}

int main(argc, argv)
int argc;
char **argv;
{
    char buffer[BUFFER_SIZE], *cmd = NULL;
    char *confdir, *version_string;
    char *mainfname = NULL, *tempfname = NULL;
    char pid_str[NUM_STR_SIZE];
    char *confname;
    int do_clientchk, clientchk_pid, client_probs;
    int do_serverchk, serverchk_pid, server_probs;
    int opt, size, result_port, tempfd, mainfd;
    amwait_t retstat;
    pid_t pid;
    extern int optind;
    int l, n, s;
    int fd;

    for(fd = 3; fd < FD_SETSIZE; fd++) {
	/*
	 * Make sure nobody spoofs us with a lot of extra open files
	 * that would cause an open we do to get a very high file
	 * descriptor, which in turn might be used as an index into
	 * an array (e.g. an fd_set).
	 */
	close(fd);
    }

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

    version_string = vstralloc("\n",
			       "(brought to you by Amanda ", version(), ")\n",
			       NULL);

    mailout = overwrite = 0;
    do_serverchk = do_clientchk = 1;
    server_probs = client_probs = 0;
    tempfd = mainfd = -1;

    /* process arguments */

    while((opt = getopt(argc, argv, "mwsc")) != EOF) {
	switch(opt) {
	case 'm':	mailout = 1; break;
	case 'w':	overwrite = 1; break;
	case 's':	do_serverchk = 1; do_clientchk = 0; break;
	case 'c':	do_serverchk = 0; do_clientchk = 1; break;
	case '?':
	default:
	    usage();
	}
    }
    argc -= optind, argv += optind;

    if(argc != 1) usage();

    confname = *argv;

    confdir = vstralloc(CONFIG_DIR, "/", confname, NULL);
    if(chdir(confdir) != 0)
	error("could not cd to confdir %s: %s", confdir, strerror(errno));

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
    if(do_clientchk && do_serverchk) {
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

    /* in parent, errors go to the main output file */

    dup2(mainfd, 1);
    dup2(mainfd, 2);
    pname = "amcheck-parent";

    /* start server side checks */

    if(do_serverchk)
	serverchk_pid = start_server_check(mainfd);
    else
	serverchk_pid = 0;

    /* start client side checks */

    if(do_clientchk) {
	clientchk_pid = start_client_checks(do_serverchk? tempfd : mainfd);
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
	    char *msg = NULL;

	    ap_snprintf(number, sizeof(number), "%ld", (long)pid);
	    msg = vstralloc("parent: reaped bogus pid ", number, "\n", NULL);
	    for(l = 0, n = strlen(buffer); l < n; l += s) {
		if((s = write(mainfd, buffer + l, n - l)) < 0) {
		    error("write main file: %s", strerror(errno));
		}
	    }
	    amfree(msg);
	}
    }


    /* copy temp output to main output and write tagline */

    if(do_clientchk && do_serverchk) {
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

    for(l = 0, n = strlen(version_string); l < n; l += s) {
	if((s = write(mainfd, version_string + l, n - l)) < 0) {
	    error("write main file: %s", strerror(errno));
	}
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
			" ", getconf_str(CNF_MAILTO),
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
extern char *datestamp;
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
		pname, slotstr, changer_resultstr);
	return 1;
    }
    else if(rc == 1) {
	fprintf(errf, "%s: slot %s: %s\n", pname, slotstr, changer_resultstr);
	return 0;
    }
    else {
	if((errstr = tape_rdlabel(device, &datestamp, &label)) != NULL) {
	    fprintf(errf, "%s: slot %s: %s\n", pname, slotstr, errstr);
	} else {
	    /* got an amanda tape */
	    fprintf(errf, "%s: slot %s: date %-8s label %s",
		    pname, slotstr, datestamp, label);
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
		if(tp != NULL && tp->position < tapedays)
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

    if((tp = lookup_tapepos(getconf_int(CNF_TAPECYCLE))) == NULL)
	searchlabel = NULL;
    else
	searchlabel = tp->label;

    found = 0;
    got_match = 0;

    changer_scan(scan_init, taperscan_slot);

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

int start_server_check(fd)
int fd;
{
    char *errstr, *tapename;
    generic_fs_stats_t fs;
    tape_t *tp;
    FILE *outf;
    holdingdisk_t *hdp;
    int pid, tapebad, disklow, logbad;
    int inparallel;

    pname = "amcheck-server";

    switch(pid = fork()) {
    case -1: error("could not fork server check: %s", strerror(errno));
    case 0: break;
    default:
	return pid;
    }

    startclock();

    if(read_tapelist(getconf_str(CNF_TAPELIST)))
	error("parse error in %s", getconf_str(CNF_TAPELIST));

    if((outf = fdopen(fd, "w")) == NULL)
	error("fdopen %d: %s", fd, strerror(errno));
    errf = outf;

    fprintf(outf, "Amanda Tape Server Host Check\n");
    fprintf(outf, "-----------------------------\n");

    /* check available disk space */

    disklow = 0;

    inparallel = getconf_int(CNF_INPARALLEL);

    for(hdp = holdingdisks; hdp != NULL; hdp = hdp->next) {
	if(get_fs_stats(hdp->diskdir, &fs) == -1) {
	    fprintf(outf, "ERROR: statfs %s: %s\n",
		    hdp->diskdir, strerror(errno));
	    disklow = 1;
	}
	else if(access(hdp->diskdir, W_OK) == -1) {
	    fprintf(outf, "ERROR: %s is unwritable: %s\n",
		    hdp->diskdir, strerror(errno));
	    disklow = 1;
	}
	else if(fs.avail == -1) {
	    fprintf(outf,
	       "WARNING: avail disk space unknown for %s. %ld KB requested.\n",
		    hdp->diskdir, hdp->disksize);
	    disklow = 1;
	}
	else if(fs.avail < hdp->disksize) {
	    fprintf(outf,
		    "WARNING: %s: only %ld KB free (%ld KB requested).\n",
		    hdp->diskdir, fs.avail, hdp->disksize);
	    disklow = 1;
	}
	else
	    fprintf(outf, "%s: %ld KB disk space available, that's plenty.\n",
		    hdp->diskdir, fs.avail);
    }

    /* check that the log file is writable if it already exists */

    {
	char *logdir;
	char *logfile;

	logbad = 0;

	logdir = getconf_str(CNF_LOGDIR);
	logfile = vstralloc(logdir, "/log", NULL);

	if(access(logdir, W_OK) == -1) {
	    fprintf(outf, "ERROR: %s is unwritable: %s\n",
		logdir, strerror(errno));
	    logbad = 1;
	}

	if(access(logfile, F_OK) == 0 && access(logfile, W_OK) != 0) {
	    fprintf(outf, "ERROR: %s is not writable\n", logfile);
	    logbad = 1;
	}

	amfree(logfile);
    }

    /* check that the tape is a valid amanda tape */

    tapebad = 0;

    tapedays = getconf_int(CNF_TAPECYCLE);
    labelstr = getconf_str(CNF_LABELSTR);
    tapename = getconf_str(CNF_TAPEDEV);

    if (!getconf_seen(CNF_TPCHANGER) && getconf_int(CNF_RUNTAPES) != 1) {
	fprintf(outf,
		"WARNING: if a tape changer is not available, runtapes must be set to 1.\n");
    }

    if(changer_init() && (tapename = taper_scan()) == NULL) {
	fprintf(outf, "ERROR: %s.\n", changer_resultstr);
	tapebad = 1;
    } else if(access(tapename,F_OK|R_OK|W_OK) == -1) {
	fprintf(outf, "ERROR: %s: %s\n",tapename,strerror(errno));
	tapebad = 1;
    } else if((errstr = tape_rdlabel(tapename, &datestamp, &label)) != NULL) {
	fprintf(outf, "ERROR: %s: %s.\n", tapename, errstr);
	tapebad = 1;
    } else {
	tp = lookup_tapelabel(label);
	if(tp != NULL && tp->position < tapedays) {
	    fprintf(outf, "ERROR: cannot overwrite active tape %s.\n", label);
	    tapebad = 1;
	}
	if(!match(labelstr, label)) {
	    fprintf(outf, "ERROR: label %s doesn't match labelstr \"%s\".\n",
		    label, labelstr);
	    tapebad = 1;
	}

    }

    if(tapebad) {
	tape_t *exptape = lookup_tapepos(tapedays);
	fprintf(outf, "       (expecting ");
	if(exptape != NULL) fprintf(outf, "tape %s or ", exptape->label);
	fprintf(outf, "a new tape)\n");
    }

    if(!tapebad && overwrite) {
	if((errstr = tape_writable(tapename)) != NULL) {
	    fprintf(outf,
		    "ERROR: tape %s label ok, but is not writable.\n", label);
	    tapebad = 1;
	}
	else fprintf(outf, "Tape %s is writable.\n", label);
    }
    else fprintf(outf, "NOTE: skipping tape-writable test.\n");

    if(!tapebad)
	fprintf(outf, "Tape %s label ok.\n", label);

    {
	char *indexdir = getconf_str(CNF_INDEXDIR);
	struct stat statbuf;
	if((stat(indexdir, &statbuf) == -1)
	   || !S_ISDIR(statbuf.st_mode)
	   || (access(indexdir, W_OK) == -1)) {
	    fprintf(outf, "Index dir \"%s\" doesn't exist or is not writable.\n",
		    indexdir);
	}
    }

    if (access(COMPRESS_PATH, X_OK) == -1)
      fprintf(outf, "%s is not executable, server-compression and indexing will not work\n",
	      COMPRESS_PATH);

    fprintf(outf, "Server check took %s seconds.\n", walltime_str(curclock()));

    fflush(outf);
    exit(tapebad || disklow || logbad);
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

#ifdef KRB4_SECURITY
    int kamanda_port;
#endif

    pname = "amcheck-clients";

    switch(pid = fork()) {
    case -1: error("could not fork client check: %s", strerror(errno));
    case 0: break;
    default:
	return pid;
    }

    startclock();

    if((origqp = read_diskfile(getconf_str(CNF_DISKFILE))) == NULL)
	error("could not load \"%s\"\n", getconf_str(CNF_DISKFILE));

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

    while(!empty(*origqp)) {
	req = vstralloc("SERVICE selfcheck\n",
			"OPTIONS ;\n",
			NULL);
	hostp = origqp->head->host;
	for(dp = hostp->disks; dp != NULL; dp = dp->hostnext) {
	    char *t;

	    remove_disk(origqp, dp);
	    t = vstralloc(req,
			  dp->program, 
			  " ",
			  dp->name,
			  " 0 OPTIONS |",
			  optionstr(dp),
			  "\n",
			  NULL);
	    amfree(req);
	    req = t;
	}
	hostcount++;

#ifdef KRB4_SECURITY
	if(hostp->disks->auth == AUTH_KRB4)
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
		    "ERROR: %s: couldn't resolve hostname\n", hostp->hostname);
	    remote_errors++;
	}
	check_protocol();
    }
    run_protocol();

    fprintf(outf,
     "Client check: %d hosts checked in %s seconds, %d problems found.\n",
	    hostcount, walltime_str(curclock()), remote_errors);
    fflush(outf);
    exit(remote_errors > 0);
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
    if(hostp->disks->auth == AUTH_KRB4 &&
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
