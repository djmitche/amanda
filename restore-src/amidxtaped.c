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
/* $Id: amidxtaped.c,v 1.25.2.3.4.1.2.11 2004/01/29 19:26:51 martinea Exp $
 *
 * This daemon extracts a dump image off a tape for amrecover and
 * returns it over the network. It basically, reads a number of
 * arguments from stdin (it is invoked via inet), one per line,
 * preceeded by the number of them, and forms them into an argv
 * structure, then execs amrestore
 */

#include "amanda.h"
#include "clock.h"
#include "version.h"

#include "changer.h"
#include "tapeio.h"
#include "conffile.h"
#include "logfile.h"

static char *pgm = "amidxtaped";	/* in case argv[0] is not set */

char *conf_logdir = NULL;
char *conf_logfile = NULL;
int get_lock = 0;
char *found_device = NULL;

static char *get_client_line P((void));

/* get a line from client - line terminated by \r\n */
static char *
get_client_line()
{
    static char *line = NULL;
    char *part = NULL;
    int len;

    amfree(line);
    while(1) {
	if((part = agets(stdin)) == NULL) {
	    if(errno != 0) {
		dbprintf(("%s: read error: %s\n",
			  debug_prefix_time(NULL), strerror(errno)));
	    } else {
		dbprintf(("%s: EOF reached\n", debug_prefix_time(NULL)));
	    }
	    if(line) {
		dbprintf(("%s: unprocessed input:\n", debug_prefix_time(NULL)));
		dbprintf(("-----\n"));
		dbprintf(("%s\n", line));
		dbprintf(("-----\n"));
	    }
	    amfree(line);
	    amfree(part);
	    if(get_lock) {
		unlink(conf_logfile);
	    }
	    dbclose();
	    exit(1);
	    /* NOTREACHED */
	}
	if(line) {
	    strappend(line, part);
	    amfree(part);
	} else {
	    line = part;
	    part = NULL;
	}
	if((len = strlen(line)) > 0 && line[len-1] == '\r') {
	    line[len-1] = '\0';		/* zap the '\r' */
	    break;
	}
	/*
	 * Hmmm.  We got a "line" from agets(), which means it saw
	 * a '\n' (or EOF, etc), but there was not a '\r' before it.
	 * Put a '\n' back in the buffer and loop for more.
	 */
	strappend(line, "\n");
    }
    dbprintf(("%s: > %s\n", debug_prefix_time(NULL), line));
    return line;
}

int found = 0;
char *searchlabel = NULL;
int nslots, backwards;

int scan_init(rc, ns, bk)
int rc, ns, bk;
{
    if(rc) {
	if(get_lock) {
	    unlink(conf_logfile);
	}
        error("could not get changer info: %s", changer_resultstr);
    }
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
    char *datestamp = NULL, *label = NULL;

    if(rc == 2) {
	dbprintf(("%s: fatal slot %s: %s\n", debug_prefix_time(NULL),
		  slotstr, changer_resultstr));
        return 1;
    }
    else if(rc == 1) {
	dbprintf(("%s: slot %s: %s\n", debug_prefix_time(NULL),
		  slotstr, changer_resultstr));
        return 0;
    }
    else {
        if((errstr = tape_rdlabel(device, &datestamp, &label)) != NULL) {
	    dbprintf(("%s: slot %s: %s\n", debug_prefix_time(NULL),
		      slotstr, errstr));
        } else {
            /* got an amanda tape */
	    dbprintf(("%s: slot %s: date %-8s label %s",
		      debug_prefix_time(NULL), slotstr, datestamp, label));
            if(strcmp(label, FAKE_LABEL) == 0 ||
               strcmp(label, searchlabel) == 0) {
                /* it's the one we are looking for, stop here */
		found_device = newstralloc(found_device,device);
		dbprintf((" (exact label match)\n"));
                found = 1;
                return 1;
            }
            else {
		dbprintf((" (no match)\n"));
            }
        }
    }
    return 0;
}


int lock_logfile()
{
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
    log_add(L_INFO, "amidxtaped");
    return 1;
}


int main(argc, argv)
int argc;
char **argv;
{
    int amrestore_nargs;
    char **amrestore_args;
    char *buf = NULL;
    int i;
    char *amrestore_path;
    pid_t pid;
    int isafile;
    struct stat stat_tape;
    char *tapename = NULL;
    int fd;
    char *s, *fp;
    int ch;
    char *errstr = NULL;
    struct sockaddr_in addr;
    amwait_t status;

    int re_header = 0;
    int re_end = 0;
    char *re_label = NULL;
    char *re_fsf = NULL;
    char *re_device = NULL;
    char *re_host = NULL;
    char *re_disk = NULL;
    char *re_datestamp = NULL;
    char *re_config = NULL;

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

    /*
     * When called via inetd, it is not uncommon to forget to put the
     * argv[0] value on the config line.  On some systems (e.g. Solaris)
     * this causes argv and/or argv[0] to be NULL, so we have to be
     * careful getting our name.
     */
    if (argc >= 1 && argv != NULL && argv[0] != NULL) {
	if((pgm = strrchr(argv[0], '/')) != NULL) {
	    pgm++;
	} else {
	    pgm = argv[0];
	}
    }

    set_pname(pgm);

#ifdef FORCE_USERID

    /* we'd rather not run as root */

    if(geteuid() == 0) {
	if(client_uid == (uid_t) -1) {
	    error("error [cannot find user %s in passwd file]\n", CLIENT_LOGIN);
	}

	initgroups(CLIENT_LOGIN, client_gid);
	setgid(client_gid);
	setuid(client_uid);
    }

#endif	/* FORCE_USERID */

    /* initialize */
    /* close stderr first so that debug file becomes it - amrestore
       chats to stderr, which we don't want going to client */
    /* if no debug file, ship to bit bucket */
    (void)close(STDERR_FILENO);
    dbopen();
    startclock();
    dbprintf(("%s: version %s\n", pgm, version()));
#ifdef DEBUG_CODE
    if(dbfd() != -1 && dbfd() != STDERR_FILENO)
    {
	if(dup2(dbfd(),STDERR_FILENO) != STDERR_FILENO)
	{
	    perror("amidxtaped can't redirect stderr to the debug file");
	    dbprintf(("%s: can't redirect stderr to the debug file\n",
		      debug_prefix_time(NULL)));
	    return 1;
	}
    }
#else
    if ((i = open("/dev/null", O_WRONLY)) == -1 ||
	(i != STDERR_FILENO &&
	 (dup2(i, STDERR_FILENO) != STDERR_FILENO ||
	  close(i) != 0))) {
	perror("amidxtaped can't redirect stderr");
	return 1;
    }
#endif

    if (! (argc >= 1 && argv != NULL && argv[0] != NULL)) {
	dbprintf(("%s: WARNING: argv[0] not defined: check inetd.conf\n",
		  debug_prefix_time(NULL)));
    }

    i = sizeof (addr);
    if (getpeername(0, (struct sockaddr *)&addr, &i) == -1)
	error("getpeername: %s", strerror(errno));
    if (addr.sin_family != AF_INET || ntohs(addr.sin_port) == 20) {
	error("connection rejected from %s family %d port %d",
	      inet_ntoa(addr.sin_addr), addr.sin_family, htons(addr.sin_port));
    }

    /* do the security thing */
    amfree(buf);
    buf = stralloc(get_client_line());
    s = buf;
    ch = *s++;

    skip_whitespace(s, ch);
    if (ch == '\0')
    {
	error("cannot parse SECURITY line");
    }
    fp = s-1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';
    if (strcmp(fp, "SECURITY") != 0)
    {
	error("cannot parse SECURITY line");
    }
    skip_whitespace(s, ch);
    if (!security_ok(&addr, s-1, 0, &errstr)) {
	error("security check failed: %s", errstr);
    }

    /* get the number of arguments */
    amrestore_nargs = 0;
    do {
	amfree(buf);
	buf = stralloc(get_client_line());
	if(strncmp(buf, "LABEL=", 6) == 0) {
	    re_label = stralloc(buf+6);
	}
	else if(strncmp(buf, "FSF=", 4) == 0) {
	    int fsf = atoi(buf+4);
	    if(fsf > 0) {
		re_fsf = stralloc(buf+4);
	    }
	}
	else if(strncmp(buf, "HEADER", 6) == 0) {
	    re_header = 1;
	}
	else if(strncmp(buf, "DEVICE=", 7) == 0) {
	    re_device = stralloc(buf+7);
	}
	else if(strncmp(buf, "HOST=", 5) == 0) {
	    re_host = stralloc(buf+5);
	}
	else if(strncmp(buf, "DISK=", 5) == 0) {
	    re_disk = stralloc(buf+5);
	}
	else if(strncmp(buf, "DATESTAMP=", 10) == 0) {
	    re_datestamp = stralloc(buf+10);
	}
	else if(strncmp(buf, "END", 3) == 0) {
	    re_end = 1;
	}
	else if(strncmp(buf, "CONFIG=", 7) == 0) {
	    re_config = stralloc(buf+7);
	}
	else if(buf[0] != '\0' && buf[0] >= '0' && buf[0] <= '9') {
	    amrestore_nargs = atoi(buf);
	    re_end = 1;
	}
	else {
	}
    } while (re_end == 0);

    if(re_config) {
	char *conffile;
	config_dir = vstralloc(CONFIG_DIR, "/", re_config, "/", NULL);
	conffile = stralloc2(config_dir, CONFFILE_NAME);
	if (read_conffile(conffile)) {
	    dbprintf(("%s: config '%s' not found\n",
		      debug_prefix_time(NULL), re_config));
	    amfree(re_fsf);
	    amfree(re_label);
	    amfree(re_config);
	}
    }
    else {
	amfree(re_fsf);
	amfree(re_label);
    }

    if(re_device && re_config &&
       strcmp(re_device, getconf_str(CNF_TAPEDEV)) == 0) {
	get_lock = lock_logfile();
    }

    if(re_label && re_config &&
       strcmp(re_device, getconf_str(CNF_AMRECOVER_CHANGER)) == 0) {

	if(changer_init() == 0) {
	    dbprintf(("%s: No changer available\n",
		       debug_prefix_time(NULL)));
	}
	else {
	    searchlabel = stralloc(re_label);
	    changer_find(scan_init, taperscan_slot, searchlabel);
	    if(found == 0) {
		dbprintf(("%s: Can't find label \"%s\"\n",
			  debug_prefix_time(NULL), searchlabel));
		if(get_lock) {
		    unlink(conf_logfile);
		}
		dbclose();
		exit(1);
	    }
	    else {
		     re_device=stralloc(found_device);
		dbprintf(("%s: label \"%s\" found\n",
			  debug_prefix_time(NULL), searchlabel));
	    }
	}
    }

    if(re_fsf && re_config && getconf_int(CNF_AMRECOVER_DO_FSF) == 0) {
	amfree(re_fsf);
    }
    if(re_label && re_config && getconf_int(CNF_AMRECOVER_CHECK_LABEL) == 0) {
	amfree(re_label);
    }

    dbprintf(("%s: amrestore_nargs=%d\n",
	      debug_prefix_time(NULL),
	      amrestore_nargs));

    amrestore_args = (char **)alloc((amrestore_nargs+12)*sizeof(char *));
    i = 0;
    amrestore_args[i++] = "amrestore";
    if(re_header || re_device || re_host || re_disk || re_datestamp ||
       re_label || re_fsf) {

	amrestore_args[i++] = "-p";
	if(re_header) amrestore_args[i++] = "-h";
	if(re_label) {
	    amrestore_args[i++] = "-l";
	    amrestore_args[i++] = re_label;
	}
	if(re_fsf) {
	    amrestore_args[i++] = "-f";
	    amrestore_args[i++] = re_fsf;
	}
	if(re_device) amrestore_args[i++] = re_device;
	if(re_host) amrestore_args[i++] = re_host;
	if(re_disk) amrestore_args[i++] = re_disk;
	if(re_datestamp) amrestore_args[i++] = re_datestamp;
    }
    else { /* fe_amidxtaped_nargs */
	while (i <= amrestore_nargs) {
	    amrestore_args[i++] = stralloc(get_client_line());
	}
    }
    amrestore_args[i] = NULL;

    amrestore_path = vstralloc(sbindir, "/", "amrestore", NULL);

    /* so got all the arguments, now ready to execv */
    dbprintf(("%s: Ready to execv amrestore with:\n", debug_prefix_time(NULL)));
    dbprintf(("path = %s\n", amrestore_path));
    for (i = 0; amrestore_args[i] != NULL; i++)
    {
	dbprintf(("argv[%d] = \"%s\"\n", i, amrestore_args[i]));
    }

    if ((pid = fork()) == 0)
    {
	/* child */
	(void)execv(amrestore_path, amrestore_args);

	/* only get here if exec failed */
	dbprintf(("%s: child could not exec %s: %s\n",
		  debug_prefix_time(NULL),
		  amrestore_path,
		  strerror(errno)));
	return 1;
	/*NOT REACHED*/
    }

    /* this is the parent */
    if (pid == -1)
    {
	dbprintf(("%s: error forking amrestore child: %s\n",
		  debug_prefix_time(NULL), strerror(errno)));
	if(get_lock) {
	    unlink(conf_logfile);
	}
	dbclose();
	return 1;
    }

    /* wait for the child to do the restore */
    if (waitpid(pid, &status, 0) == -1)
    {
	dbprintf(("%s: error waiting for amrestore child: %s\n",
		  debug_prefix_time(NULL), strerror(errno)));
	if(get_lock) {
	    unlink(conf_logfile);
	}
	dbclose();
	return 1;
    }
    /* amrestore often sees the pipe reader (ie restore) quit in the middle
       of the file because it has extracted all of the files needed. This
       results in an exit status of 2. This unfortunately is the exit
       status returned by many errors. Only when the exit status is 1 is it
       guaranteed that an error existed. In all cases we should rewind the
       tape if we can so that a retry starts from the correct place */
    if (WIFEXITED(status) != 0)
    {

	dbprintf(("%s: amrestore terminated normally with status: %d\n",
		  debug_prefix_time(NULL), WEXITSTATUS(status)));
    }
    else
    {
	dbprintf(("%s: amrestore terminated abnormally.\n",
		  debug_prefix_time(NULL)));
    }

    /* rewind tape */
    if(re_device) {
	tapename = re_device;
    }
    else {
	/* the first non-option argument is the tape device */
	for (i = 1; i <= amrestore_nargs; i++)
	    if (amrestore_args[i][0] != '-')
		break;
	if (i > amrestore_nargs) {
	    dbprintf(("%s: Couldn't find tape in arguments\n",
		      debug_prefix_time(NULL)));
	    if(get_lock) {
		unlink(conf_logfile);
	    }
	    dbclose();
	    return 1;
	}

	tapename = stralloc(amrestore_args[i]);
    }
    if (tape_stat(tapename, &stat_tape) != 0) {
        error("could not stat %s: %s", tapename, strerror(errno));
    }
    isafile = S_ISREG((stat_tape.st_mode));
    if (!isafile) {
	char *errstr = NULL;

	dbprintf(("%s: rewinding tape ...\n", debug_prefix_time(NULL)));
	errstr = tape_rewind(tapename);

	if (errstr != NULL) {
	    dbprintf(("%s: %s\n", debug_prefix_time(NULL), errstr));
	    amfree(errstr);
	} else {
	    dbprintf(("%s: done\n", debug_prefix_time(NULL)));
	}
    }

    if(get_lock) {
	unlink(conf_logfile);
    }

    amfree(tapename);
    dbclose();
    return 0;
}
