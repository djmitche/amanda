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
 * $Id: killpgrp.c,v 1.9 1999/09/15 00:31:27 jrj Exp $
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
 
int main P((int argc, char **argv));
static void term_kill_soft P((int sig));
static void term_kill_hard P((int sig));

int main(argc, argv)
int argc;
char **argv;
{
    amwait_t status;
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

    safe_cd();

    set_pname("killpgrp");

    dbopen();
    dbprintf(("%s: version %s\n", argv[0], version()));

    if(client_uid == (uid_t) -1) {
	error("error [cannot find user %s in passwd file]\n", CLIENT_LOGIN);
    }

#ifdef FORCE_USERID
    if (getuid() != client_uid)
	error("error [must be invoked by %s]\n", CLIENT_LOGIN);

    if (geteuid() != 0)
	error("error [must be setuid root]\n");
#endif	/* FORCE_USERID */

    setuid(0);

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
}

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
