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
** - "configure" has four variables that are used to determine which type of
**   locking to use:
**     USE_POSIX_FCNTL - use fcntl().  The full job.
**     USE_FLOCK       - use flock().  Does just as well.
**     USE_LOCKF       - use lockf().  Only handles advisory, exclusive,
**                       blocking file locks as used by Amanda.
**     USE_MYLOCK      - Home brew exclusive, blocking file lock.
**     <none>          - No locking available.  User beware!
** - "configure" compiles this with -DCONFIGURE_TEST to try and determine
**   whether a particular type of locking works.
*/

#ifndef CONFIGURE_TEST
#  include "amanda.h"
#endif

#if defined(USE_POSIX_FCNTL)
   static struct flock lock = {
	F_UNLCK,	/* Lock type, will be set below. */
	SEEK_SET,	/* Offset below starts at beginning, */
	0,		/* thus lock region starts at byte 0 */
	0		/* and goes to EOF. */
   };			/* Don't need other field(s). */
#endif

#if !defined(USE_POSIX_FCNTL) && defined(USE_FLOCK)
#  if !defined(HAVE_FLOCK_DECL) && !defined(CONFIGURE_TEST)
     extern int flock P((int fd, int operation));
#  endif
#endif


#if !defined(USE_POSIX_FCNTL) && !defined(USE_FLOCK) && defined(USE_LOCKF)

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

#if !defined(USE_POSIX_FCNTL) && !defined(USE_FLOCK) && !defined(USE_LOCKF) && defined(USE_MYLOCK)
/* XXX - error checking in this secton needs to be tightened up */

int steal_lock(fn, mypid)	/* can we steal a lock 0=no; 1=yes; 2=not locked; 3=error */
char *fn;
long mypid;
{
	int fd;
	char buff[64];
	long pid;
	int rc;

	fd = open(fn, O_RDONLY);
	if (fd == -1) {
		if (errno == ENOENT) return 2; /* it's not now */
		return 3;
	}

	rc = read(fd, buff, sizeof(buff));
	if (rc < 1) {
		rc = close(fd);
		return 3;
	}

	rc = close(fd);
	if (rc != 0) return 3;

	rc = sscanf(buff, "%ld\n", &pid);
	if (rc != 1) return 3; /* XXX */

	if (pid == mypid) return 1; /* i'm the locker! */

	/* are they still there ? */
	rc = kill((pid_t)pid, 0);
	if (rc != 0) {
		if (errno == ESRCH) return 1; /* locker has gone */
		return 3;
	}

	return 0;
}

int my_lock(res, op)	/* lock or unlock a resource */
char *res; /* name of resource to lock */
int op;    /* true to lock; false to unlock */
{
	int resl;
	long mypid;
	char pidstr[64];
	int pidstrl;
	char *lockf;
	char *tlockf;
	int fd;
	int rc;
	int retry;

	resl = strlen(res);

	lockf = alloc(resl+12+1);
	sprintf(lockf, "/tmp/am%s.lock", res);

	if (!op) {
		/* unlock the resource */
		unlink(lockf);
		free(lockf);
		return 0;
	}

	/* lock the resource */

	mypid = (long)getpid();
	sprintf(pidstr, "%ld", mypid);
	pidstrl = strlen(pidstr);

	tlockf = alloc(resl+8+pidstrl+1);
	sprintf(tlockf, "/tmp/am%s.%s", res, pidstr);

	rc = unlink(tlockf);
	fd = open(tlockf, O_WRONLY|O_CREAT|O_EXCL, 0644);
	if (fd == -1) return 1;
	rc = write(fd, pidstr, pidstrl);
	rc = write(fd, "\n", 1);
	rc = close(fd);
	rc = chmod(tlockf, 0644);

	retry = 1;
	while(retry && (rc = link(tlockf, lockf)) != 0) {
		if (errno != EEXIST) break;

		/* resource is locked by someone else - can we steal it ? */
/* XXX we need to lock this part ... */
		rc = steal_lock(lockf, mypid);
		switch(rc) {
		case 0: /* no - still locked */
			sleep(1);
			break;
		case 1: /* yes - steal it */
			rc = unlink(lockf);
			if (rc != 0 && errno != ENOENT) retry = 0;
			break;
		case 2: /* not locked */
			break;
		case 3: /* error */
			retry = 0;
			break;
		default:
			assert(0);
		}
/* XXX ...down to here some where */
	}

	unlink(tlockf);

	free(tlockf);
	free(lockf);

	return rc;
}
#endif


/* Get a file lock (for read-only files).
*/
int amroflock(fd, resource)
int fd;
char *resource;
{
	int r;

#ifdef USE_POSIX_FCNTL
	lock.l_type = F_RDLCK;
	r = fcntl(fd, F_SETLKW, &lock);
#else
	r = amflock(fd, resource);
#endif

	return r;
}


/* Get a file lock.
*/
int amflock(fd, resource)
int fd;
char *resource;
{
	int r;

#ifdef USE_POSIX_FCNTL
	lock.l_type = F_WRLCK;
	r = fcntl(fd, F_SETLKW, &lock);
#else
#ifdef USE_FLOCK
	r = flock(fd, LOCK_EX);
#else
#ifdef USE_LOCKF
	r = use_flock(fd, 1);
#else
#ifdef USE_MYLOCK
	r = my_lock(resource, 1);
#else
	r = 0;
#endif
#endif
#endif
#endif

	return r;
}


/* Release a file lock.
*/
int amfunlock(fd, resource)
int fd;
char *resource;
{
	int r;

#ifdef USE_POSIX_FCNTL
	lock.l_type = F_UNLCK;
	r = fcntl(fd, F_SETLK, &lock);
#else
#ifdef USE_FLOCK
	r = flock(fd, LOCK_UN);
#else
#ifdef USE_LOCKF
	r = use_flock(fd, 0);
#else
#ifdef USE_MYLOCK
	r = my_lock(resource, 0);
#else
	r = 0;
#endif
#endif
#endif
#endif

	return r;
}


/* Test routine for use by configure.
** (I'm not sure why we use both return and exit!)
** XXX the testing here should be a lot more comprehensive.
**     - lock the file and then try and lock it from another process
**     - lock the file from another process and check that process
**       termination unlocks it.
*/
#ifdef CONFIGURE_TEST
main()
{
    int lockfd;
    FILE *fp;
    char *filen = "/tmp/conftest.lock";
    char *resn = "test";

    if (lockfd = open(filen, O_RDWR|O_CREAT, 0666) == -1)
	return (-1);
    close(lockfd);
    chmod(filen, 0666);

    if (!(fp = fopen(filen, "r")))
	return (-2);
    if (amroflock(fileno(fp), resn) != 0)
	exit(1);
    if (amfunlock(fileno(fp), resn) != 0)
	exit(2);
    fclose(fp);

    if (!(fp = fopen(filen, "w")))
	return (-3);
    if (amflock(fileno(fp), resn) != 0)
	exit(3);
    if (amfunlock(fileno(fp), resn) != 0)
	exit(4);
    fclose(fp);

    exit(0);
}
#endif
