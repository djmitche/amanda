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
 * $Id: tapeio.c,v 1.14 1998/02/19 10:35:23 amcore Exp $
 *
 * implements tape I/O functions
 */
#include "amanda.h"

#include "tapeio.h"
#include "fileheader.h"

#ifndef R_OK
#define R_OK 4
#define W_OK 2
#endif

static char *errstr = NULL;

#ifdef AIX_TAPEIO

#include <sys/tape.h>

int tapefd_rewind(tapefd)
int tapefd;
{
    struct stop st;

    st.st_op = STREW;
    st.st_count = 1;

    return ioctl(tapefd, STIOCTOP, &st);
}

int tapefd_fsf(tapefd, count)
int tapefd, count;
/*
 * fast-forwards the tape device count files.
 */
{
    struct stop st;

    st.st_op = STFSF;
    st.st_count = count;

    return ioctl(tapefd, STIOCTOP, &st);
}

int tapefd_weof(tapefd, count)
int tapefd, count;
/*
 * write <count> filemarks on the tape.
 */
{
    struct stop st;

    st.st_op = STWEOF;
    st.st_count = count;

    return ioctl(tapefd, STIOCTOP, &st);
}

#else /* AIX_TAPEIO */
#ifdef XENIX_TAPEIO

#include <sys/tape.h>

int tapefd_rewind(tapefd)
int tapefd;
{
    int st;
    return ioctl(tapefd, MT_REWIND, &st);
}

int tapefd_fsf(tapefd, count)
int tapefd, count;
/*
 * fast-forwards the tape device count files.
 */
{
    int st;
    int c;
    int status;

    for ( c = count; c ; c--)
	if (status = ioctl(tapefd, MT_RFM, &st))
	    break;

    return status;
}

int tapefd_weof(tapefd, count)
int tapefd, count;
/*
 * write <count> filemarks on the tape.
 */
{
    int st;
    int c;
    int status;

    for ( c = count; c ; c--)
	if (status = ioctl(tapefd, MT_WFM, &st))
	    break;

    return status;
}


#else	/* ! AIX_TAPEIO && !XENIX_TAPEIO */


#include <sys/mtio.h>

int tapefd_rewind(tapefd)
int tapefd;
{
    struct mtop mt;
    int rc, cnt;

    mt.mt_op = MTREW;
    mt.mt_count = 1;

    /* EXB-8200 drive on FreeBSD can fail to rewind, but retrying
     * won't hurt, and it will usually even work! */
    for(cnt = 0; cnt < 10; ++cnt) {
	rc = ioctl(tapefd, MTIOCTOP, &mt);
	if (rc == 0)
	    break;
	sleep(3);
    }
    return rc;
}

int tapefd_fsf(tapefd, count)
int tapefd, count;
/*
 * fast-forwards the tape device count files.
 */
{
    struct mtop mt;

    mt.mt_op = MTFSF;
    mt.mt_count = count;

    return ioctl(tapefd, MTIOCTOP, &mt);
}

int tapefd_weof(tapefd, count)
int tapefd, count;
/*
 * write <count> filemarks on the tape.
 */
{
    struct mtop mt;

    mt.mt_op = MTWEOF;
    mt.mt_count = count;

    return ioctl(tapefd, MTIOCTOP, &mt);
}




#endif /* !XENIX_TAPEIO */
#endif /* !AIX_TAPEIO */



int tape_open(filename, mode)
char *filename;
int mode;
{
    int ret = 0, delay = 2, timeout = 200;
    if (mode == 0 || mode == O_RDONLY)
	mode = O_RDONLY;
    else
	mode = O_RDWR;
    do {
	ret = open(filename, mode);
	/* if tape open fails with errno==EAGAIN, it is worth retrying
	 * a few seconds later.  */
	if (ret == 0 || errno != EAGAIN)
	    break;
	sleep(delay);
	timeout -= delay;
	if (delay < 16)
	    delay *= 2;
    } while (timeout > 0);
    return ret;
}

int tapefd_read(tapefd, buffer, count)
int tapefd, count;
void *buffer;
{
    return read(tapefd, buffer, count);
}

int tapefd_write(tapefd, buffer, count)
int tapefd, count;
void *buffer;
{
    return write(tapefd, buffer, count);
}

int tapefd_close(tapefd)
int tapefd;
{
    return close(tapefd);
}

void tapefd_resetofs(tapefd)
int tapefd;
{
    /* 
     * this *should* be a no-op on the tape, but resets the kernel's view
     * of the file offset, preventing it from barfing should we pass the
     * filesize limit (eg OSes with 2 GB filesize limits) on a long tape.
     */
    lseek(tapefd, (off_t) 0L, SEEK_SET);
}

char *tape_rewind(devname)
char *devname;
{
    int fd;

    if((fd = tape_open(devname, O_RDONLY)) == -1) {
	errstr = newstralloc(errstr, "no tape online");
	return errstr;
    }

    if(tapefd_rewind(fd) == -1) {
	errstr = newstralloc2(errstr, "rewinding tape: ", strerror(errno));
	tapefd_close(fd);
	return errstr;
    }

    tapefd_close(fd);
    return NULL;
}


char *tape_fsf(devname, count)
char *devname;
int count;
{
    int fd;
    char count_str[NUM_STR_SIZE];

    if((fd = tape_open(devname, O_RDONLY)) == -1) {
	errstr = newstralloc(errstr, "no tape online");
	return errstr;
    }

    if(tapefd_fsf(fd, count) == -1) {
	ap_snprintf(count_str, sizeof(count_str), "%d", count);
	errstr = newvstralloc(errstr,
			      "fast-forward ", count_str, "files: ",
			      strerror(errno),
			      NULL);
	tapefd_close(fd);
	return errstr;
    }

    tapefd_close(fd);
    return NULL;
}

char *tapefd_rdlabel(tapefd, datestamp, label)
int tapefd;
char **datestamp, **label;
{
    int rc;
    char buffer[TAPE_BLOCK_BYTES];
    dumpfile_t file;

    *datestamp = *label = NULL;

    if(tapefd_rewind(tapefd) == -1) {
	errstr = newstralloc2(errstr, "rewinding tape: ", strerror(errno));
	return errstr;
    }

    if((rc = tapefd_read(tapefd, buffer, sizeof(buffer))) == -1) {
	errstr = newstralloc2(errstr, "reading label: ", strerror(errno));
	return errstr;
    }

    /* make sure buffer is null-terminated */
    if(rc == sizeof(buffer)) rc--;
    buffer[rc] = '\0';

    parse_file_header(buffer, &file, sizeof(buffer));
    if(file.type != F_TAPESTART) {
	errstr = newstralloc(errstr, "not an amanda tape");
	return errstr;
    }
    *datestamp = newstralloc(*datestamp, file.datestamp);
    *label = newstralloc(*label, file.name);

    return NULL;
}


char *tape_rdlabel(devname, datestamp, label)
char *devname, **datestamp, **label;
{
    int fd;

    if((fd = tape_open(devname, O_RDONLY)) == -1) {
	errstr = newstralloc(errstr, "no tape online");
	return errstr;
    }

    if(tapefd_rdlabel(fd, datestamp, label) != NULL) {
	tapefd_close(fd);
	return errstr;
    }

    tapefd_close(fd);
    return NULL;
}


char *tapefd_wrlabel(tapefd, datestamp, label)
int tapefd;
char *datestamp, *label;
{
    int rc;
    char buffer[TAPE_BLOCK_BYTES];
    dumpfile_t file;

    if(tapefd_rewind(tapefd) == -1) {
	errstr = newstralloc2(errstr, "rewinding tape: ", strerror(errno));
	return errstr;
    }

    fh_init(&file);
    file.type=F_TAPESTART;
    strncpy(file.datestamp, datestamp, sizeof(file.datestamp)-1);
    file.datestamp[sizeof(file.datestamp)-1] = '\0';
    strncpy(file.name, label, sizeof(file.name)-1);
    file.name[sizeof(file.name)-1] = '\0';
    write_header(buffer,&file,sizeof(buffer));

    if((rc = tapefd_write(tapefd, buffer, sizeof(buffer))) != sizeof(buffer)) {
	errstr = newstralloc2(errstr, "writing label: ",
			      (rc != -1) ? "short write" : strerror(errno));
	return errstr;
    }

    return NULL;
}


char *tape_wrlabel(devname, datestamp, label)
char *devname, *datestamp, *label;
{
    int fd;

    if((fd = tape_open(devname, O_WRONLY)) == -1) {
	if(errno == EACCES) {
	    errstr = newstralloc(errstr,
				 "writing label: tape is write-protected");
	} else {
	    errstr = newstralloc2(errstr, "writing label: ", strerror(errno));
	}
	tapefd_close(fd);
	return errstr;
    }

    if(tapefd_wrlabel(fd, datestamp, label) != NULL) {
	tapefd_close(fd);
	return errstr;
    }

    tapefd_close(fd);
    return NULL;
}


char *tapefd_wrendmark(tapefd, datestamp)
int tapefd;
char *datestamp;
{
    int rc;
    char buffer[TAPE_BLOCK_BYTES];
    dumpfile_t file;

    fh_init(&file);
    file.type=F_TAPEEND;
    strncpy(file.datestamp, datestamp, sizeof(file.datestamp)-1);
    file.datestamp[sizeof(file.datestamp)-1] = '\0';
    write_header(buffer, &file,sizeof(buffer));

    if((rc = tapefd_write(tapefd, buffer, sizeof(buffer))) != sizeof(buffer)) {
	errstr = newstralloc2(errstr, "writing endmark: ",
			      (rc != -1) ? "short write" : strerror(errno));
	return errstr;
    }

    return NULL;
}


char *tape_wrendmark(devname, datestamp)
char *devname, *datestamp;
{
    int fd;

    if((fd = tape_open(devname, O_WRONLY)) == -1) {
	errstr = newstralloc2(errstr, "writing endmark: ",
			      (errno == EACCES) ? "tape is write-protected"
						: strerror(errno));
	return errstr;
    }

    if(tapefd_wrendmark(fd, datestamp) != NULL) {
	tapefd_close(fd);
	return errstr;
    }

    return NULL;
}


char *tape_writable(devname)
char *devname;
{
    int fd;

    /* first, make sure the file exists and the permissions are right */

    if(access(devname, R_OK|W_OK) == -1) {
	errstr = newstralloc(errstr, strerror(errno));
	return errstr;
    }

    if((fd = tape_open(devname, O_WRONLY)) == -1) {
	errstr = newstralloc(errstr,
			     (errno == EACCES) ? "tape write-protected"
					       : strerror(errno));
	return errstr;
    }

    if(tapefd_close(fd) == -1) {
	errstr = newstralloc(errstr, strerror(errno));
	return errstr;
    }

    return NULL;
}
