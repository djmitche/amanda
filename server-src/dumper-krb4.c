/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991,1994 University of Maryland
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
 * dumper-krb4.c - those bits of dumper code that deal with encrypting
 *		   data streams over the network.  Even though these just
 *		   call the underlying DES routines, the U.S. government
 *		   considers this a munition.  Go figure.
 */
#include "krb4-security.h"

/*
 * NOTE:  This symbol must be the same as DATABUF_SIZE in
 * client-src/sendbackup-krb4.c
 * so that the encrypt/decrypt routines are working on the same sized buffers.
 * Really, this should be moved out of dumper.c so that both programs can use
 * the same symbol.  Hopefully that can be done later.  This is good enough
 * to get encryption working for now...
 *
 *                  - Chris Ross (cross@uu.net)  4-Jun-1998
 */
#define	DATABUF_SIZE	DISK_BLOCK_BYTES

int kamanda_port;
CREDENTIALS cred;

des_key_schedule sched;

void decrypt_initialize()
{
    des_key_sched(cred.session, sched);
}

decrypt_buffer(buffer, size)
char *buffer;
int size;
{
    des_pcbc_encrypt((des_cblock *)buffer, 
		     (des_cblock *)buffer, 
	              size, sched, (des_cblock *)cred.session, DES_DECRYPT);
}

#define NAUGHTY_BITS_INITIALIZE		\
    if(kencrypt) decrypt_initialize()

#define NAUGHTY_BITS			\
    if(kencrypt) decrypt_buffer(databuf, DATABUF_SIZE)
