/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991, 1996 University of Maryland at College Park
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
 * $Id: amindexd.h,v 1.5 1997/12/16 18:02:18 jrj Exp $
 *
 * interface for amindexd variables
 */

#ifndef AMINDEXD
#define AMINDEXD

#define LONG_LINE 256

#include "disk_history.h"

/* state */
extern char local_hostname[MAX_HOSTNAME_LENGTH];	/* me! */
extern char remote_hostname[LONG_LINE];	/* the client */
extern char dump_hostname[LONG_LINE];	/* the machine we are restoring */
extern char disk_name[LONG_LINE];	/* the disk we are restoring */
extern char config[LONG_LINE];		/* the config we are restoring */
extern char date[LONG_LINE];

extern void reply P((int n, char *fmt, ...));
extern void lreply P((int n, char *fmt, ...));
extern void fast_lreply P((int n, char *fmt, ...));

extern int opaque_ls P((char *dir, int recursive));
extern int translucent_ls P((char *dir));

extern int uncompress_file P((char *filename_gz, char *filename, int len));

#endif /* AMINDEXD */
