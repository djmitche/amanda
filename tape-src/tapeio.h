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
 * $Id: tapeio.h,v 1.9.2.2.4.3.2.2 2003/03/06 21:44:21 martinea Exp $
 *
 * interface for tapeio.c
 */
#ifndef TAPEIO_H
#define TAPEIO_H

#include "amanda.h"

/*
 * Tape drive status structure.  This abstracts the things we are
 * interested in from the free-for-all of what the various drivers
 * supply.
 */

struct am_mt_status {
    char online_valid;			/* is the online flag valid? */
    char bot_valid;			/* is the BOT flag valid? */
    char eot_valid;			/* is the EOT flag valid? */
    char protected_valid;		/* is the protected flag valid? */
    char flags_valid;			/* is the flags field valid? */
    char fileno_valid;			/* is the fileno field valid? */
    char blkno_valid;			/* is the blkno field valid? */
    char device_status_valid;		/* is the device status field valid? */
    char error_status_valid;		/* is the device status field valid? */

    char online;			/* true if device is online/ready */
    char bot;				/* true if tape is at the beginning */
    char eot;				/* true if tape is at end of medium */
    char protected;			/* true if tape is write protected */
    long flags;				/* device flags, whatever that is */
    long fileno;			/* tape file number */
    long blkno;				/* block within file */
    int device_status_size;		/* size of orig device status field */
    unsigned long device_status;	/* "device status", whatever that is */
    int error_status_size;		/* size of orig error status field */
    unsigned long error_status;		/* "error status", whatever that is */
};

#define	FAKE_LABEL	"[fake-label]"

int tape_open ();

int tapefd_rewind P((int tapefd));
int tapefd_unload P((int tapefd));
int tapefd_fsf P((int tapefd, int count));
int tapefd_weof P((int tapefd, int count));

int tapefd_status P((int tapefd, struct am_mt_status *));

void tapefd_resetofs P((int tapefd));

ssize_t tapefd_read P((int tapefd, void *buffer, size_t count));
ssize_t tapefd_write P((int tapefd, const void *buffer, size_t count));

char *tapefd_rdlabel P((int tapefd, char **datestamp, char **label));
char *tapefd_wrlabel P((int tapefd,
			char  *datestamp,
			char  *label,
			unsigned int s));
char *tapefd_wrendmark P((int tapefd, char *datestamp, unsigned int s));

int tapefd_eof P((int tapefd));		/* just used in tapeio-test */
int tapefd_close P((int tapefd));
int tapefd_can_fork P((int tapefd));

char *tape_unload P((char *dev));
char *tape_rewind P((char *dev));
char *tape_fsf P((char *dev, int count));
char *tape_rdlabel P((char *dev, char **datestamp, char **label));
char *tape_wrlabel P((char *dev,
		      char  *datestamp,
		      char  *label,
		      unsigned int size));
char *tape_wrendmark P((char *dev,
			char *datestamp,
			unsigned int size));
char *tape_writable P((char *dev));

int tape_access P((char *dev, int mode));
int tape_stat P((char *filename, struct stat *buf));

char *tapefd_getinfo_label P((int fd));
void tapefd_setinfo_label P((int fd, char *v));
char *tapefd_getinfo_host P((int fd));
void tapefd_setinfo_host P((int fd, char *v));
char *tapefd_getinfo_disk P((int fd));
void tapefd_setinfo_disk P((int fd, char *v));
int tapefd_getinfo_level P((int fd));
void tapefd_setinfo_level P((int fd, int v));
char *tapefd_getinfo_datestamp P((int fd));
void tapefd_setinfo_datestamp P((int fd, char *v));
long tapefd_getinfo_length P((int fd));
void tapefd_setinfo_length P((int fd, long v));
char *tapefd_getinfo_tapetype P((int fd));
void tapefd_setinfo_tapetype P((int fd, char *v));
int tapefd_getinfo_fake_label P((int fd));
void tapefd_setinfo_fake_label P((int fd, int v));
int tapefd_getinfo_ioctl_fork P((int fd));
void tapefd_setinfo_ioctl_fork P((int fd, int v));

#ifdef HAVE_LINUX_ZFTAPE_H
int is_zftape P((const char *filename));
#endif

int tapeio_init_devname P((char * dev,
			   char **dev_left,
			   char **dev_right,
			   char **dev_next));
char *tapeio_next_devname P((char * dev_left,
			     char * dev_right,
			     char **dev_next));

#endif /* ! TAPEIO_H */
