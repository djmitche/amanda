/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991,1993 University of Maryland
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
 * sendbackup-krb4.c - those bits of sendbackup code that deal with encrypting
 *		       data streams over the network.  Even though these just
 *		       call the underlying DES routines, the U.S. government
 *		       considers this a munition.  Go figure.
 */
#include "krb4-security.h"
#include "sendbackup-krb4.h"

/*
 * NOTE:  This symbol must be the same as DATABUF_SIZE in
 * server-src/dumper-krb4.c
 * so that the encrypt/decrypt routines are working on the same sized buffers.
 * Really, this should be moved out of dumper.c so that both programs can use
 * the same symbol.  Hopefully that can be done later.  This is good enough
 * to get encryption working for now...
 *
 *                  - Chris Ross (cross@uu.net)  4-Jun-1998
 */
#define	DATABUF_SIZE	DISK_BLOCK_BYTES

void kencrypt_stream()
{
    char *bp, buffer[DATABUF_SIZE];
    int rdsize, wrsize, left;
    des_key_schedule sched;
    int l, n;

    des_key_sched(session_key, sched);

    while(1) {
	/* read a block, taking into account short reads */
	left = DATABUF_SIZE;
	bp = buffer;
	while(left) {
	    if((rdsize = read(0, bp, left)) == -1)
		error("kencrypt: read error: %s", strerror(errno));
	    if(rdsize == 0) break;
	    left -= rdsize;
	    bp += rdsize;
	}
	if(bp == buffer) break;	/* end of file */

	if(bp < buffer+DATABUF_SIZE)
	    memset(bp,0,left);

	des_pcbc_encrypt(buffer, buffer, DATABUF_SIZE, sched, session_key,
			 DES_ENCRYPT);

	for(l = 0, n = DATABUF_SIZE; l < n; l += wrsize) {
	    if((wrsize = write(1, buffer + l, n - l)) < 0) {
		error("kencrypt: write error: %s", strerror(errno));
	    }
	}
    }
}
