/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991, 1996 University of Maryland at College Park
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
 * $Id: killpgrp.c,v 1.6 1998/06/01 19:13:13 jrj Exp $
 *
 * if it is the process group leader, it kills all processes in its
 * process group when it is killed itself.
 */
#include "amanda.h"
#include "version.h"

#ifdef HAVE_GETPGRP
#ifdef GETPGRP_VOID
#define AM_GETPGRP() getpgrp()
#else
#define AM_GETPGRP() getpgrp(getpid())
#endif
#else
/* we cannot check it, so let us assume it is ok */
#define AM_GETPGRP() getpid()
#endif
 
#if defined(USE_RUNDUMP) || defined(VDUMP) || defined(XFSDUMP)
#  undef ERRMSG
#else
#  define ERRMSG "killpgrp not enabled on this system.\n"
#endif

int main P((int argc, char **argv));
#ifndef ERRMSG
static void term_kill_soft P((int sig));
static void term_kill_hard P((int sig));
#endif

int main(argc, argv)
int argc;
char **argv;
{
#ifndef ERRMSG
    amwait_t status;
#endif
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

    set_pname("killpgrp");

    dbopen();
    dbprintf(("%s: version %s\n", argv[0], version()));

#ifdef ERRMSG							/* { */

    fprintf(stderr, ERRMSG);
    dbprintf(("%s: %s", argv[0], ERRMSG));
    dbclose();
    return 1;

#else								/* } { */

    /* we should be invoked by CLIENT_LOGIN */
    {
	struct passwd *pwptr;
	char *pwname = CLIENT_LOGIN;
	if((pwptr = getpwnam(pwname)) == NULL)
	    error("error [cannot find user %s in passwd file]\n", pwname);

#ifdef FORCE_USERID
	if (getuid() != pwptr->pw_uid)
	    error("error [must be invoked by %s]\n", pwname);

	if (geteuid() != 0)
	    error("error [must be setuid root]\n");
#endif	/* FORCE_USERID */

	setuid(0);
    }

    if (AM_GETPGRP() != getpid()) {
	error("error [must be the process group leader]\n");
    }

    signal(SIGTERM, term_kill_soft);

    while (getchar() != EOF) {
	/* wait until EOF */
    }

    term_kill_soft(0);

    for(;;) {
	if (wait(&status) != -1)
	    break;
	if (errno != EINTR) {
	    error("error [wait() failed: %s]\n", strerror(errno));
	    return -1;
	}
    }

    dbprintf(("child process exited with status %d\n", WEXITSTATUS(status)));

    return WEXITSTATUS(status);
#endif								/* } */
}

#ifndef ERRMSG							/* { */
static void term_kill_soft(sig)
int sig;
{
    pid_t dumppid = getpid();
    int killerr;

    signal(SIGTERM, SIG_IGN);
    signal(SIGALRM, term_kill_hard);
    alarm(3);
    /*
     * First, try to kill the dump process nicely.  If it ignores us
     * for three seconds, hit it harder.
     */
    dbprintf(("sending SIGTERM to process group %ld\n", (long) dumppid));
    killerr = kill(-dumppid, SIGTERM);
    if (killerr == -1) {
	dbprintf(("kill failed: %s\n", strerror(errno)));
    }
}

static void term_kill_hard(sig)
int sig;
{
    pid_t dumppid = getpid();
    int killerr;

    dbprintf(("it won\'t die with SIGTERM, but SIGKILL should do\n"));
    dbprintf(("do\'t expect any further output, this will be suicide\n"));
    killerr = kill(-dumppid, SIGKILL);
    /* should never reach this point, but so what? */
    if (killerr == -1) {
	dbprintf(("kill failed: %s\n", strerror(errno)));
	dbprintf(("waiting until child terminates\n"));
    }
}
#endif								/* } */
