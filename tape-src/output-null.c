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
 * Author: James da Silva, Systems Design and Analysis Group
 *			   Computer Science Department
 *			   University of Maryland at College Park
 */

/*
 * $Id: output-null.c,v 1.1.4.3 2001/07/31 23:07:30 jrjackson Exp $
 *
 * tapeio.c virtual tape interface for a null device.
 */

#include "amanda.h"

#include "tapeio.h"
#include "output-null.h"
#include "fileheader.h"
#ifndef R_OK
#define R_OK 4
#define W_OK 2
#endif

static long *amount_written = NULL;
static int open_count = 0;

int
null_tape_open(filename, flags, mask)
    char *filename;
    int flags;
    int mask;
{
    int fd;

    if ((flags & 3) != O_RDONLY) {
	flags &= ~3;
	flags |= O_RDWR;
    }
    if ((fd = open("/dev/null", flags, mask)) >= 0) {
	tapefd_setinfo_fake_label(fd, 1);
	amtable_alloc((void **)&amount_written,
		      &open_count,
		      sizeof(*amount_written),
		      fd + 1,
		      10,
		      NULL);
	amount_written[fd] = 0;
    }
    return fd;
}

int
null_tapefd_read(fd, buffer, count)
    int fd, count;
    void *buffer;
{
    return read(fd, buffer, count);
}

int
null_tapefd_write(fd, buffer, count)
    int fd, count;
    const void *buffer;
{
    int write_count = count;
    long length;
    long kbytes_left;
    int r;

    if (write_count <= 0) {
	return 0;				/* special case */
    }

    if ((length = tapefd_getinfo_length(fd)) > 0) {
	kbytes_left = length - amount_written[fd];
	if (write_count / 1024 > kbytes_left) {
	    write_count = kbytes_left * 1024;
	}
    }
    amount_written[fd] += (write_count + 1023) / 1024;
    if (write_count <= 0) {
	errno = ENOSPC;
	r = -1;
    } else {
	r = write(fd, buffer, write_count);
    }
    return r;
}

int
null_tapefd_close(fd)
    int fd;
{
    return close(fd);
}

void
null_tapefd_resetofs(fd)
    int fd;
{
}

int
null_tapefd_status(fd, stat)
    int fd;
    struct am_mt_status *stat;
{
    memset((void *)stat, 0, sizeof(*stat));
    stat->online_valid = 1;
    stat->online = 1;
    return 0;
}

int
null_tape_stat(filename, buf)
     char *filename;
     struct stat *buf;
{
     return stat(filename, buf);
}

int
null_tape_access(filename, mode)
     char *filename;
     int mode;
{
     return access(filename, mode);
}

int
null_tapefd_rewind(fd)
    int fd;
{
    amount_written[fd] = 0;
    return 0;
}

int
null_tapefd_unload(fd)
    int fd;
{
    amount_written[fd] = 0;
    return 0;
}

int
null_tapefd_fsf(fd, count)
    int fd, count;
{
    return 0;
}

int
null_tapefd_weof(fd, count)
    int fd, count;
{
    return 0;
}
