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
 * $Id: tapeio.h,v 1.9.2.2.2.1 2000/12/13 19:25:29 jrjackson Exp $
 *
 * interface for tapeio.c
 */
#ifndef TAPEIO_H
#define TAPEIO_H

#include "amanda.h"

int tape_open P((char *filename, int mode));
int tape_stat P((char *filename, struct stat *buf));
int tape_access P((char *filename, int mode));

int tapefd_rewind P((int tapefd));
int tapefd_fsf P((int tapefd, int count));
int tapefd_weof P((int tapefd, int count));
void tapefd_resetofs P((int tapefd));

int tapefd_read P((int tapefd, void *buffer, int count));
int tapefd_write P((int tapefd, const void *buffer, int count));

char *tapefd_rdlabel P((int tapefd, char **datestamp, char **label));
char *tapefd_wrlabel P((int tapefd, char  *datestamp, char  *label));
char *tapefd_wrendmark P((int tapefd, char *datestamp));

int tapefd_eof P((int tapefd));		/* just used in tapeio-test */
int tapefd_close P((int tapefd));

char *tape_rewind P((char *dev));
char *tape_fsf P((char *dev, int count));
char *tape_rdlabel P((char *dev, char **datestamp, char **label));
char *tape_wrlabel P((char *dev, char  *datestamp, char  *label));
char *tape_wrendmark P((char *dev, char *datestamp));
char *tape_writable P((char *dev));

#ifdef HAVE_LINUX_ZFTAPE_H
int is_zftape P((const char *filename));
#endif

#endif /* ! TAPEIO_H */
