/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991, 1997 University of Maryland
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

/* amflock.c - file locking routines.  Put here to hide the system
**             dependant stuff from the rest of the code.
**
** Notes:
** - These are "best effort" routines.
** - "configure" has three variables that are used to determine which type of
**   locking to use:
**     HAVE_POSIX_FCNTL - use fcntl().  The full job.
**     HAVE_FLOCK       - use flock().  Does just as well.
**     HAVE_LOCKF       - use lockf().  Only handles advisory, exclusive,
**                        blocking file locks as used by Amanda.
**     <none>           - No locking available.  User beware!
** - "configure" compiles this with -DCONFIGURE_TEST to try and determine
**   whether a particular type of locking works.
*/

#ifndef CONFIGURE_TEST
#include "amanda.h"
#endif

#ifdef HAVE_POSIX_FCNTL

static struct flock lock = {
	F_UNLCK,	/* Lock type, will be set below. */
	SEEK_SET,	/* Offset below starts at beginning, */
	0,		/* thus lock region starts at byte 0 */
	0		/* and goes to EOF. */
};			/* Don't need other field(s). */

#else
#ifdef HAVE_FLOCK

#if !defined(HAVE_FLOCK_DECL) && !defined(CONFIGURE_TEST)
extern int flock P((int fd, int operation));
#endif

#endif
#endif


#ifdef HAVE_LOCKF

/* XPG4-UNIX (eg, SGI IRIX, DEC DU) has F_ULOCK instead of F_UNLOCK */
#if defined(F_ULOCK) && !defined(F_UNLOCK)
#  define F_UNLOCK F_ULOCK
#endif

int use_lockf(fd, op)
int fd;
int op;
{
    off_t prevpos;

    /* save our current position */
    if((prevpos = lseek(fd, (off_t)0, SEEK_CUR)) == -1) return -1;

    /* a lock on the first byte of the file serves as our advisory file lock */
    if(lseek(fd, (off_t)0, SEEK_SET) == -1) return -1;

    if(op) {
	if(lockf(fd, F_LOCK, 1) == -1) return -1;
    }
    else {
	if(lockf(fd, F_UNLOCK, 1) == -1) return -1;
    }

    /* restore our current position */
    if(lseek(fd, prevpos, SEEK_SET) == -1) return -1;
    return 0;
}

#endif


/* Get a file lock (for read-only files).
*/
int amroflock(fd)
int fd;
{
	int r;

#ifdef HAVE_POSIX_FCNTL
	lock.l_type = F_RDLCK;
	r = fcntl(fd, F_SETLKW, &lock);
#else
	r = amflock(fd);
#endif

	return r;
}


/* Get a file lock.
*/
int amflock(fd)
int fd;
{
	int r;

#ifdef HAVE_POSIX_FCNTL
	lock.l_type = F_WRLCK;
	r = fcntl(fd, F_SETLKW, &lock);
#else
#ifdef HAVE_FLOCK
	r = flock(fd, LOCK_EX);
#else
#ifdef HAVE_LOCKF
	r = use_flock(fd, 1);
#endif
#endif
#endif

	return r;
}


/* Release a file lock.
*/
int amfunlock(fd)
int fd;
{
	int r;

#ifdef HAVE_POSIX_FCNTL
	lock.l_type = F_UNLCK;
	r = fcntl(fd, F_SETLK, &lock);
#else
#ifdef HAVE_FLOCK
	r = flock(fd, LOCK_UN);
#else
#ifdef HAVE_LOCKF
	r = use_flock(fd, 0);
#endif
#endif
#endif

	return r;
}


/* Test routine for use by configure.
** (I'm not sure why we use both return and exit!)
*/
#ifdef CONFIGURE_TEST
main()
{
    int lockfd;
    FILE *fp;

    if (lockfd = open("/tmp/conftest.lock", O_RDWR|O_CREAT, 0666) == -1)
	return (-1);
    close(lockfd);
    chmod("/tmp/conftest.lock", 0666);

    if (!(fp = fopen("/tmp/conftest.lock", "r")))
	return (-2);
    if (amroflock(fileno(fp)) == -1)
	exit(1);
    if (amfunlock(fileno(fp)) == -1)
	exit(2);
    fclose(fp);

    if (!(fp = fopen("/tmp/conftest.lock", "w")))
	return (-3);
    if (amflock(fileno(fp)) == -1)
	exit(3);
    if (amfunlock(fileno(fp)) == -1)
	exit(4);
    fclose(fp);

    exit(0);
}
#endif
