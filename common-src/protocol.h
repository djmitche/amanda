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
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "amanda.h"
#include "dgram.h"

typedef enum {
    S_BOGUS,
    S_STARTUP, S_SENDREQ, S_ACKWAIT, S_REPWAIT, S_SUCCEEDED, S_FAILED
} pstate_t;

typedef enum { A_BOGUS, A_START, A_TIMEOUT, A_RCVDATA } action_t;

typedef enum { P_BOGUS, P_REQ, P_REP, P_ACK, P_NAK } pktype_t;

typedef struct {			/* a predigested datagram */
    pktype_t type;
    struct sockaddr_in peer;
    unsigned long cksum;
    int version_major, version_minor;
    int sequence;
    char *handle;
    char *hostname;
    char *service;
    char *program;
    char *security;
    char *body;
    dgram_t dgram;
} pkt_t;

typedef struct proto_s {
    pstate_t state;
    struct sockaddr_in peer;
    time_t timeout;
    time_t repwait;
    time_t origtime, curtime;
    int reqtries, acktries;
    int origseq, curseq;
    int handleofs;
    char *security;
    int auth_cksum;
    char *hostname;        /* hostname to be added to a request packet */
    char *req;					/* body of request msg */
    void (*continuation) P((struct proto_s *, pkt_t *));
    void *datap;
    struct proto_s *prev,*next;
} proto_t;

void proto_init P((int sock, int startseq, int handles));
int make_request P((char *hostname, int port, char *req, void *datap,
		    time_t repwait, 
		    void (*continuation) P((proto_t *p, pkt_t *pkt))
		    ));

void check_protocol P((void));
void run_protocol P((void));

void parse_pkt_header P((pkt_t *pkt));

#ifdef KRB4_SECURITY
int make_krb_request P((char *hostname, int port, char *req,
			void *datap, time_t repwait,
			void (*continuation) P((proto_t *p, pkt_t *pkt))
			));
#endif

extern char parse_errmsg[];

#endif /* PROTOCOL_H */
