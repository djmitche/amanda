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
 * amandad-krb4.c - The Kerberos4 support bits for amandad.c.
 */

#include "krb4-security.h"

void transfer_session_key P((void));
void add_mutual_authenticator P((dgram_t *msg));
int krb4_security_ok P((struct sockaddr_in *addr,
			char *str, unsigned long cksum, char **errstr));

#define KEY_PIPE	3

void transfer_session_key()
{
    int rc, key_pipe[2];
    int l, n, s;
    char *k;

    if(pipe(key_pipe) == -1)
	error("could not open key pipe: %s", strerror(errno));

    k = (char *)session_key;
    for(l = 0, n = sizeof(session_key); l < n; l += s) {
	if ((s = write(key_pipe[1], k + l, n - l)) < 0) {
	    error("error writing to key pipe: %s", strerror(errno));
	}
    }

    /* modification by BIS@BBN 4/25/2003:
     * check that key_pipe[0] is not KEY_PIPE before doing dup2 and
     * close; otherwise we may inadvertently close KEY_PIPE */
    aclose(key_pipe[1]);
    if (key_pipe[0] != KEY_PIPE) {
	dup2(key_pipe[0], KEY_PIPE);
	close(key_pipe[0]);
    }
}

void add_mutual_authenticator(msg)
dgram_t *msg;
{
    union {
	char pad[8];		/* minimum size for encryption */
	u_int32_t i;		/* "long" on 32-bit machines */
    } mutual;
    int alen, blen;
    char *s;

    blen = sizeof(mutual);
    memset(&mutual, 0, blen);
    mutual.i = htonl(auth_cksum+1);

    encrypt_data(&mutual, blen, session_key);

    s = vstralloc("SECURITY MUTUAL-AUTH ",
		  bin2astr((unsigned char *)mutual.pad, blen),
		  "\n", NULL);
    dgram_cat(msg, s);
    amfree(s);
}
