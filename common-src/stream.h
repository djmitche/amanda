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
 * $Id: stream.h,v 1.4.2.1.2.2 2001/07/31 23:07:29 jrjackson Exp $
 *
 * interface to stream module
 */
#ifndef STREAM_H
#define STREAM_H

#include "amanda.h"

/* Note: This must be kept in sync with the DATABUF_SIZE defined in
 * client-src/sendbackup-krb4.c, or kerberos encryption won't work...
 *	- Chris Ross (cross@uu.net)  4-Jun-1998
 */
#define NETWORK_BLOCK_BYTES	DISK_BLOCK_BYTES
#define STREAM_BUFSIZE		(NETWORK_BLOCK_BYTES * 2)

int stream_server P((int *port, int sendsize, int recvsize));
int stream_accept P((int sock, int timeout, int sendsize, int recvsize));
int stream_client P((char *hostname, int port, int sendsize, int recvsize,
    int *localport));

#endif
