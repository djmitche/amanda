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
 * $Id: tapeio.c,v 1.4 1997/12/01 01:06:13 amcore Exp $
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

#define MAX_LINE 1024
static char errstr[MAX_LINE];

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

    mt.mt_op = MTREW;
    mt.mt_count = 1;
    return ioctl(tapefd, MTIOCTOP, &mt);
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
    return open(filename, mode);
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
	sprintf(errstr, "no tape online");
	return errstr;
    }

    if(tapefd_rewind(fd) == -1) {
	sprintf(errstr, "rewinding tape: %s", strerror(errno));
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

    if((fd = tape_open(devname, O_RDONLY)) == -1) {
	sprintf(errstr, "no tape online");
	return errstr;
    }

    if(tapefd_fsf(fd, count) == -1) {
	sprintf(errstr, "fast-forward %d files: %s", count, strerror(errno));
	tapefd_close(fd);
	return errstr;
    }

    tapefd_close(fd);
    return NULL;
}

char *tapefd_rdlabel(tapefd, datestamp, label)
int tapefd;
char *datestamp, *label;
{
    int rc;
    char buffer[TAPE_BLOCK_BYTES];
    dumpfile_t file;

    if(tapefd_rewind(tapefd) == -1) {
	sprintf(errstr, "rewinding tape: %s", strerror(errno));
	return errstr;
    }

    if((rc = tapefd_read(tapefd, buffer, sizeof(buffer))) == -1) {
	sprintf(errstr, "reading label: %s", strerror(errno));
	return errstr;
    }

    /* make sure buffer is null-terminated */
    if(rc == sizeof(buffer)) rc--;
    buffer[rc] = '\0';

    parse_file_header(buffer, &file, sizeof(buffer));
    if(file.type != F_TAPESTART) {
	sprintf(errstr, "not an amanda tape");
	return errstr;
    }
    strcpy(datestamp, file.datestamp);
    strcpy(label, file.name);

    return NULL;
}


char *tape_rdlabel(devname, datestamp, label)
char *devname, *datestamp, *label;
{
    int fd;

    if((fd = tape_open(devname, O_RDONLY)) == -1) {
	sprintf(errstr, "no tape online");
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
	sprintf(errstr, "rewinding tape: %s", strerror(errno));
	return errstr;
    }

    fh_init(&file);
    file.type=F_TAPESTART;
    strcpy(file.datestamp,datestamp);
    strcpy(file.name,label);
    write_header(buffer,&file,sizeof(buffer));

    if((rc = tapefd_write(tapefd, buffer, sizeof(buffer))) != sizeof(buffer)) {
	sprintf(errstr, "writing label: %s",
		rc != -1? "short write" : strerror(errno));
	return errstr;
    }

    return NULL;
}


char *tape_wrlabel(devname, datestamp, label)
char *devname, *datestamp, *label;
{
    int fd;

    if((fd = tape_open(devname, O_WRONLY)) == -1) {
	if(errno == EACCES) 
	    sprintf(errstr, "writing label: tape is write-protected");
	else
	    sprintf(errstr, "writing label: %s", strerror(errno));
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
    strcpy(file.datestamp,datestamp);
    write_header(buffer, &file,sizeof(buffer));

    if((rc = tapefd_write(tapefd, buffer, sizeof(buffer))) != sizeof(buffer)) {
	sprintf(errstr, "writing endmark: %s", 
		rc != -1? "short write" : strerror(errno));
	return errstr;
    }

    return NULL;
}


char *tape_wrendmark(devname, datestamp)
char *devname, *datestamp;
{
    int fd;

    if((fd = tape_open(devname, O_WRONLY)) == -1) {
	sprintf(errstr, "writing endmark: %s",
		errno == EACCES? "tape is write-protected" : strerror(errno));
	return errstr;
    }

    if(tapefd_wrendmark(fd, datestamp) != NULL) {
	tapefd_close(fd);
	return errstr;
    }

    return NULL;
}


char *tape_writeable(devname)
char *devname;
{
    int fd;

    /* first, make sure the file exists and the permissions are right */

    if(access(devname, R_OK|W_OK) == -1) {
	sprintf(errstr, "%s", strerror(errno));
	return errstr;
    }

    if((fd = tape_open(devname, O_WRONLY)) == -1) {
	sprintf(errstr, "%s",
		errno == EACCES? "tape write-protected" : strerror(errno));
	return errstr;
    }

    if(tapefd_close(fd) == -1) {
	sprintf(errstr, "%s", strerror(errno));
	return errstr;
    }

    return NULL;
}
