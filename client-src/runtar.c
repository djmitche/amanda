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
 * $Id: runtar.c,v 1.13 2002/10/27 14:31:00 martinea Exp $
 *
 * runs GNUTAR program as root
 */
#include "amanda.h"
#include "version.h"

int main P((int argc, char **argv));

int main(argc, argv)
int argc;
char **argv;
{
#ifdef GNUTAR
    int i;
#endif
    int fd;
    char *e;

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

    set_pname("runtar");

    dbopen();
    dbprintf(("%s: version %s\n", argv[0], version()));

#ifndef GNUTAR

    fprintf(stderr,"gnutar not available on this system.\n");
    dbprintf(("%s: gnutar not available on this system.\n", argv[0]));
    dbclose();
    return 1;

#else

    if(client_uid == (uid_t) -1) {
	error("error [cannot find user %s in passwd file]\n", CLIENT_LOGIN);
    }

#ifdef FORCE_USERID
    if (getuid() != client_uid)
	error("error [must be invoked by %s]\n", CLIENT_LOGIN);

    if (geteuid() != 0)
	error("error [must be setuid root]\n");
#endif

#if !defined (DONT_SUID_ROOT)
    setuid(0);
#endif

    dbprintf(("running: %s: ",GNUTAR));
    for (i=0; argv[i]; i++)
	dbprintf(("%s ", argv[i]));
    dbprintf(("\n"));

    execve(GNUTAR, argv, safe_env());

    e = strerror(errno);
    dbprintf(("failed (%s)\n", e));
    dbclose();

    fprintf(stderr, "runtar: could not exec %s: %s\n", GNUTAR, e);
    return 1;
#endif
}
