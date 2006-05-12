/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1999 University of Maryland
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
 * Authors: the Amanda Development Team.  Its members are listed in a
 * file named AUTHORS, in the root directory of this distribution.
 */

/*
 * $Id: security-util.h,v 1.1 2006/05/12 23:11:50 martinea Exp $
 *
 */

#include "stream.h"
#include "dgram.h"
#include "queue.h"

struct sec_handle;

/*
 * This is a sec connection to a host.  We should only have
 * one connection per host.
 */
struct tcp_conn {
    const struct security_driver *driver;	/* MUST be first */
    int read, write;				/* pipes to sec */
    pid_t pid;					/* pid of sec process */
    char *pkt;					/* last pkt read */
    unsigned long pktlen;			/* len of above */
    event_handle_t *ev_read;			/* read (EV_READFD) handle */
    int ev_read_refcnt;				/* number of readers */
    char hostname[MAX_HOSTNAME_LENGTH+1];	/* host we're talking to */
    char *errmsg;				/* error passed up */
    int refcnt;					/* number of handles using */
    int handle;					/* last proto handle read */
    void (*accept_fn) P((security_handle_t *, pkt_t *));
    struct sockaddr_in peer;
    TAILQ_ENTRY(tcp_conn) tq;			/* queue handle */
    int (*recv_security_ok) P((struct sec_handle *, pkt_t *));
    char *(*prefix_packet) P((void *, pkt_t *));
};


struct sec_stream;

/*
 * This is the private handle data.
 */
struct sec_handle {
    security_handle_t sech;		/* MUST be first */
    char *hostname;			/* ptr to rc->hostname */
    struct sec_stream *rs;		/* virtual stream we xmit over */
    struct tcp_conn *rc;		/* */

    union {
	void (*recvpkt) P((void *, pkt_t *, security_status_t));
					/* func to call when packet recvd */
	void (*connect) P((void *, security_handle_t *, security_status_t));
					/* func to call when connected */
    } fn;
    void *arg;				/* argument to pass function */
    event_handle_t *ev_timeout;		/* timeout handle for recv */
    struct sockaddr_in peer;
    int sequence;
    int event_id;
    char *proto_handle;
    event_handle_t *ev_read;
    struct sec_handle *prev, *next;
    struct udp_handle *udp;
    void (*accept_fn) P((security_handle_t *, pkt_t *));
    int (*recv_security_ok) P((struct sec_handle *, pkt_t *));
};

/*
 * This is the internal security_stream data for sec.
 */
struct sec_stream {
    security_stream_t secstr;		/* MUST be first */
    struct tcp_conn *rc;		/* physical connection */
    int handle;				/* protocol handle */
    event_handle_t *ev_read;		/* read (EV_WAIT) event handle */
    void (*fn) P((void *, void *, ssize_t));	/* read event fn */
    void *arg;				/* arg for previous */
    int fd;
    char databuf[NETWORK_BLOCK_BYTES];
    int len;
    int socket;
    int port;
};

struct connq_s {
    TAILQ_HEAD(, tcp_conn) tailq;
    int qlength;
};
extern struct connq_s connq;

#define connq_first()           TAILQ_FIRST(&connq.tailq)
#define connq_next(rc)          TAILQ_NEXT(rc, tq)
#define connq_append(rc)        do {                                    \
    TAILQ_INSERT_TAIL(&connq.tailq, rc, tq);                            \
    connq.qlength++;                                                    \
} while (0)
#define connq_remove(rc)        do {                                    \
    assert(connq.qlength > 0);                                          \
    TAILQ_REMOVE(&connq.tailq, rc, tq);                                 \
    connq.qlength--;                                                    \
} while (0)

/*
 * This is data local to the datagram socket.  We have one datagram
 * per process per auth.
 */
typedef struct udp_handle {
    const struct security_driver *driver;	/* MUST be first */
    dgram_t dgram;		/* datagram to read/write from */
    struct sockaddr_in peer;	/* who sent it to us */
    pkt_t pkt;			/* parsed form of dgram */
    char *handle;		/* handle from recvd packet */
    int sequence;		/* seq no of packet */
    event_handle_t *ev_read;	/* read event handle from dgram */
    int refcnt;			/* number of handles blocked for reading */
    struct sec_handle *bh_first, *bh_last;
    void (*accept_fn) P((security_handle_t *, pkt_t *));
    int (*recv_security_ok) P((struct sec_handle *, pkt_t *));
    char *(*prefix_packet) P((void *, pkt_t *));
} udp_handle_t;

/*
 * We register one event handler for our network fd which takes
 * care of all of our async requests.  When all async requests
 * have either been satisfied or cancelled, we unregister our
 * network event handler.
 */
#define	udp_addref(udp, netfd_read_callback) do {			\
    if ((udp)->refcnt++ == 0) {						\
	assert((udp)->ev_read == NULL);					\
	(udp)->ev_read = event_register((udp)->dgram.socket, EV_READFD,	\
	    netfd_read_callback, (udp));				\
    }									\
    assert((udp)->refcnt > 0);						\
} while (0)

/*
 * If this is the last request to be removed, then remove the
 * reader event from the netfd.
 */
#define	udp_delref(udp) do {						\
    assert((udp)->refcnt > 0);						\
    if (--(udp)->refcnt == 0) {						\
	assert((udp)->ev_read != NULL);					\
	event_release((udp)->ev_read);					\
	(udp)->ev_read = NULL;						\
    }									\
} while (0)


int   sec_stream_auth P((void *));
int   sec_stream_id P((void *));
void  sec_accept P((const security_driver_t *, int, int,
		    void (*)(security_handle_t *, pkt_t *)));
void  sec_close P((void *));
void  sec_connect_callback P((void *));
void  sec_connect_timeout P((void *));

int   stream_sendpkt P((void *, pkt_t *));
void  stream_recvpkt P((void *,
		        void (*)(void *, pkt_t *, security_status_t),
		        void *, int));
void  stream_recvpkt_timeout P((void *));
void  stream_recvpkt_cancel P((void *));

int   tcpm_stream_write P((void *, const void *, size_t));
void  tcpm_stream_read P((void *, void (*)(void *, void *, ssize_t), void *));
int   tcpm_stream_read_sync P((void *, void **));
void  tcpm_stream_read_cancel P((void *));
int   tcpm_send_token P((int, int, char **, const void *, int));
int   tcpm_recv_token P((int, int *, char **, char **, unsigned long *, int));

int   tcpma_stream_accept P((void *));
void *tcpma_stream_client P((void *, int));
void *tcpma_stream_server P((void *));
void  tcpma_stream_close P((void *));

void *tcp1_stream_server P((void *));
int   tcp1_stream_accept P((void *));
void *tcp1_stream_client P((void *, int));

int   tcp_stream_write P((void *, const void *, size_t));

char *bsd_prefix_packet P((void *, pkt_t *));
int   bsd_recv_security_ok P((struct sec_handle *, pkt_t *));

int   udpbsd_sendpkt P((void *, pkt_t *));
void  udp_close P((void *));
void  udp_recvpkt P((void *, void (*)(void *, pkt_t *, security_status_t),
		     void *, int));
void  udp_recvpkt_cancel P((void *));
void  udp_recvpkt_callback P((void *));
void  udp_recvpkt_timeout P((void *));
int   udp_inithandle P((udp_handle_t *, struct sec_handle *, struct hostent *,
		        int, char *, int));
void  udp_netfd_read_callback P((void *));

struct tcp_conn *sec_tcp_conn_get P((const char *, int));
void  sec_tcp_conn_put P((struct tcp_conn *));
void  sec_tcp_conn_read P((struct tcp_conn *));
void  parse_pkt P((pkt_t *, const void *, size_t));
const char *pkthdr2str P((const struct sec_handle *, const pkt_t *));
int   str2pkthdr P((udp_handle_t *));
char *check_user P((struct sec_handle *, const char *, const char *));

char *check_user_ruserok     P((const char *host,
				struct passwd *pwd,
				const char *user));
char *check_user_amandahosts P((const char *host,
				struct passwd *pwd,
				const char *user,
				const char *service));

int net_writev P((int, struct iovec *, int));
ssize_t net_read P((int, void *, size_t, int));
int net_read_fillbuf P((int, int, void *, int));
