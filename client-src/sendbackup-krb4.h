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
 * sendbackup-krb4.h - those bits of sendbackup defines that deal with 
 *		       encrypting data streams over the network.  Even
 *		       though these just call the underlying DES
 *		       routines, the U.S. government considers this a
 *		       munition.  Go figure.
 */

#if !defined(SENDBACKUP_KRB4_H)
#define	SENDBACKUP_KRB4_H

#include "krb4-security.h"

#define KEY_PIPE	3

int encpid;

void kencrypt_stream();

    /* modification by BIS@BBN 4/25/2003:
     * with the option processing changes in amanda 2.4.4, must change
     * the conditional from kencrypt to options->kencrypt */
#define NAUGHTY_BITS							      \
    if(options->kencrypt) {						      \
	int encinf;							      \
	encpid = pipefork(kencrypt_stream,"kencrypt",&encinf,dataf,mesgf);    \
	dataf = encinf;							      \
    }									      \
    else								      \
	encpid = -1;

#endif
