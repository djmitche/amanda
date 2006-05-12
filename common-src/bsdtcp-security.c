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
 * $Id: bsdtcp-security.c,v 1.2 2006/05/12 23:39:09 martinea Exp $
 *
 * bsdtcp-security.c - security and transport over bsdtcp or a bsdtcp-like command.
 *
 * XXX still need to check for initial keyword on connect so we can skip
 * over shell garbage and other stuff that bsdtcp might want to spew out.
 */

#include "amanda.h"
#include "util.h"
#include "event.h"
#include "packet.h"
#include "queue.h"
#include "security.h"
#include "security-util.h"
#include "stream.h"
#include "version.h"

#ifdef BSDTCP_SECURITY

/*#define	BSDTCP_DEBUG*/

#ifdef BSDTCP_DEBUG
#define	bsdtcpprintf(x)	dbprintf(x)
#else
#define	bsdtcpprintf(x)
#endif


/*
 * Number of seconds bsdtcp has to start up
 */
#define	CONNECT_TIMEOUT	20

/*
 * Interface functions
 */
static void bsdtcp_accept P((const struct security_driver *, int, int,
    void (*)(security_handle_t *, pkt_t *)));
static void bsdtcp_connect P((const char *,
    char *(*)(char *, void *), 
    void (*)(void *, security_handle_t *, security_status_t), void *, void *));

/*
 * This is our interface to the outside world.
 */
const security_driver_t bsdtcp_security_driver = {
    "BSDTCP",
    bsdtcp_connect,
    bsdtcp_accept,
    sec_close,
    stream_sendpkt,
    stream_recvpkt,
    stream_recvpkt_cancel,
    tcpma_stream_server,
    tcpma_stream_accept,
    tcpma_stream_client,
    tcpma_stream_close,
    sec_stream_auth,
    sec_stream_id,
    tcpm_stream_write,
    tcpm_stream_read,
    tcpm_stream_read_sync,
    tcpm_stream_read_cancel,
};

static int newhandle = 1;

/*
 * Local functions
 */
static int runbsdtcp P((struct sec_handle *));


/*
 * bsdtcp version of a security handle allocator.  Logically sets
 * up a network "connection".
 */
static void
bsdtcp_connect(hostname, conf_fn, fn, arg, datap)
    const char *hostname;
    char *(*conf_fn) P((char *, void *));
    void (*fn) P((void *, security_handle_t *, security_status_t));
    void *arg;
    void *datap;
{
    struct sec_handle *rh;
    struct hostent *he;

    assert(fn != NULL);
    assert(hostname != NULL);

    bsdtcpprintf(("%s: bsdtcp: bsdtcp_connect: %s\n", debug_prefix_time(NULL),
	       hostname));

    rh = alloc(sizeof(*rh));
    security_handleinit(&rh->sech, &bsdtcp_security_driver);
    rh->hostname = NULL;
    rh->rs = NULL;
    rh->ev_timeout = NULL;
    rh->rc = NULL;

    if ((he = gethostbyname(hostname)) == NULL) {
	security_seterror(&rh->sech,
	    "%s: could not resolve hostname", hostname);
	(*fn)(arg, &rh->sech, S_ERROR);
	return;
    }
    rh->hostname = he->h_name;	/* will be replaced */
    rh->rc = sec_tcp_conn_get(rh->hostname, 1);
    rh->rc->recv_security_ok = &bsd_recv_security_ok;
    rh->rc->prefix_packet = &bsd_prefix_packet;
    rh->rs = tcpma_stream_client(rh, newhandle++);

    if (rh->rs == NULL)
	goto error;

    rh->hostname = rh->rs->rc->hostname;

    /*
     * We need to open a new connection.
     *
     * XXX need to eventually limit number of outgoing connections here.
     */
    if (runbsdtcp(rh) < 0) {
//	security_seterror(&rh->sech, "Can't connect to %s: %s",
//			  hostname, rh->rs->rc->errmsg);
	goto error;
    }
    /*
     * The socket will be opened async so hosts that are down won't
     * block everything.  We need to register a write event
     * so we will know when the socket comes alive.
     *
     * Overload rh->rs->ev_read to provide a write event handle.
     * We also register a timeout.
     */
    rh->fn.connect = fn;
    rh->arg = arg;
    rh->rs->ev_read = event_register(rh->rs->rc->write, EV_WRITEFD,
	sec_connect_callback, rh);
    rh->ev_timeout = event_register(CONNECT_TIMEOUT, EV_TIME,
	sec_connect_timeout, rh);

    return;

error:
    (*fn)(arg, &rh->sech, S_ERROR);
}

/*
 * Setup to handle new incoming connections
 */
static void
bsdtcp_accept(driver, in, out, fn)
    const struct security_driver *driver;
    int in, out;
    void (*fn) P((security_handle_t *, pkt_t *));
{
    struct sockaddr_in sin;
    socklen_t len;
    struct tcp_conn *rc;
    struct hostent *he;

    len = sizeof(sin);
    if (getpeername(in, (struct sockaddr *)&sin, &len) < 0)
	return;
    he = gethostbyaddr((void *)&sin.sin_addr, sizeof(sin.sin_addr), AF_INET);
    if (he == NULL)
	return;

    rc = sec_tcp_conn_get(he->h_name, 0);
    rc->recv_security_ok = &bsd_recv_security_ok;
    rc->prefix_packet = &bsd_prefix_packet;
    rc->peer.sin_addr = *(struct in_addr *)he->h_addr;
    rc->peer.sin_port = sin.sin_port;
    rc->read = in;
    rc->write = out;
    rc->accept_fn = fn;
    rc->driver = driver;
    sec_tcp_conn_read(rc);
}

/*
 * Forks a bsdtcp to the host listed in rc->hostname
 * Returns negative on error, with an errmsg in rc->errmsg.
 */
static int
runbsdtcp(rh)
    struct sec_handle *rh;
{
    struct servent *sp;
    int server_socket, my_port;
    int euid;
    struct tcp_conn *rc = rh->rc;

    if ((sp = getservbyname("amanda", "tcp")) == NULL) {
	error("%s/tcp unknown protocol", "amanda");
    }

    euid = geteuid();
    seteuid(0);

    server_socket = stream_client_privileged(rc->hostname,
					     ntohs(sp->s_port),
					     -1,
					     -1,
					     &my_port,
					     0);

    if(server_socket < 0) {
	security_seterror(&rh->sech,
	    "%s", strerror(errno));
	
	return -1;
    }
    seteuid(euid);

    if(my_port >= IPPORT_RESERVED) {
	security_seterror(&rh->sech,
			  "did not get a reserved port: %d", my_port);
    }

    rc->read = rc->write = server_socket;
    return 0;
}

#endif /* BSDTCP_SECURITY */
