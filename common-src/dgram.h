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
 * dgram.h - interface for dgram.c 
 */
#ifndef DGRAM_H
#define DGRAM_H

#include "amanda.h"

#define MAX_DGRAM	8192

typedef struct dgram_s {
    char *cur;
    int socket;
    int len;
    char data[MAX_DGRAM+1];
} dgram_t;

int dgram_bind P((dgram_t *dgram, int *portp));
void dgram_socket P((dgram_t *dgram, int sock));
int dgram_send P((char *hostname, int port, dgram_t *dgram));
int dgram_send_addr P((struct sockaddr_in addr, dgram_t *dgram));
int dgram_recv P((dgram_t *dgram, int timeout, struct sockaddr_in *fromaddr));
dgram_t *dgram_alloc P((void));
void dgram_zero P((dgram_t *dgram));
void dgram_cat P((dgram_t *dgram, char *str));
void dgram_eatline P((dgram_t *dgram));
int bind_reserved P((int sock, struct sockaddr_in *addrp));

#endif /* ! DGRAM_H */
