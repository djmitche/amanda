/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991 University of Maryland
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
 * flock.c - simple BSD flock(2) substitute for System V machines.  
 *	     Only handles advisory, exclusive, blocking file locks as
 *	     used by Amanda.
 */
#include "amanda.h"

#ifndef NEED_POSIX_FLOCK

/* consts from BSDish <sys/file.h> */
#ifndef LOCK_EX
#  define LOCK_EX 2
#  define LOCK_UN 8
#endif

/* sgi irix reportedly has F_ULOCK instead of F_UNLOCK */
#ifdef F_UNLOCK
#  define OUR_UNLOCK F_UNLOCK
#else
#  ifdef F_ULOCK
#    define OUR_UNLOCK F_ULOCK
#  else
     !!! error neither F_ULOCK or F_UNLOCK defined: cannot deal with this
#  endif
#endif

int flock(fd, operation)
int fd, operation;
{
    off_t prevpos;

    assert(operation == LOCK_EX || operation == LOCK_UN);

    /* save our current position */
    if((prevpos = lseek(fd, (off_t)0, SEEK_CUR)) == -1) return -1;

    /* a lock on the first byte of the file serves as our advisory file lock */
    if(lseek(fd, (off_t)0, SEEK_SET) == -1) return -1;
    if(operation == LOCK_EX) {
	if(lockf(fd, F_LOCK, 1) == -1) return -1;
    }
    else {
	if(lockf(fd, OUR_UNLOCK, 1) == -1) return -1;
    }

    /* restore our current position */
    if(lseek(fd, prevpos, SEEK_SET) == -1) return -1;
    return 0;
}

#endif
