/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1993 University of Maryland
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
 * krb4-security.h - definitions for kerberos helper module
 */
#ifndef KRB4_SECURITY_H
#define KRB4_SECURITY_H

#include "amanda.h"
#include "protocol.h"

/* KerberosIV function prototypes (which should be in krb.h) */
#ifdef KERBEROSIV_MISSING_PROTOTYPES /* XXX autoconf */
char *krb_get_phost P((char *hostname));
#endif

/* Amanda krb4 function prototypes */
void kerberos_service_init P((void));
u_int32_t kerberos_cksum P((char *str));
struct hostent *host2krbname P((char *alias, char *inst, char *realm));
char *bin2astr P((unsigned char *buf, int len));
void astr2bin P((char *astr, unsigned char *buf, int  *lenp));
void encrypt_data P((void *data, int length, des_cblock key));
void decrypt_data P((void *data, int length, des_cblock key));
int kerberos_handshake P((int fd, des_cblock key));
des_cblock *host2key P((char *hostp));
int check_mutual_authenticator P((des_cblock *key, pkt_t *pkt, proto_t *p));
extern char *get_krb_security P((char *str,
				 char *host_inst, char *realm,
				 u_int32_t *cksum));

extern int krb4_auth;
extern int kencrypt;
extern des_cblock session_key;
extern u_int32_t auth_cksum;

#endif
