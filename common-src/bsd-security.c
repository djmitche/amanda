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
 * Authors: the Amanda Development Team.  Its members are listed in a
 * file named AUTHORS, in the root directory of this distribution.
 */
/*
 * $Id: bsd-security.c,v 1.10 1998/12/03 19:20:37 kashmir Exp $
 *
 * "BSD" security module
 */

#include "amanda.h"
#include "dgram.h"
#include "event.h"
#include "packet.h"
#include "queue.h"
#include "security.h"
#include "stream.h"
#include "version.h"

#ifdef BSD_SECURITY

/*
 * This is the private handle data
 */
struct bsd_handle {
    /*
     * This must be first.  Instances of bsd_handle will be cast to
     * security_handle_t's.
     */
    security_handle_t security_handle;

    /*
     * protocol handle for this request.  Each request gets its own
     * handle, so we differentiate packets for them with a "handle" header
     * in each packet.
     */
    char proto_handle[32];

    /*
     * sequence number.  historic field in packet header, but not used.
     * Here for compatibility.
     */
    int sequence;

    /*
     * The remote host we're transmitting to
     */
    char hostname[256];
    struct sockaddr_in peer;

    /*
     * Function to call when recvpkt detects new incoming data for this
     * handle
     */
    void (*fn) P((void *, pkt_t *, security_recvpkt_status_t));

    /*
     * Argument for previous function
     */
    void *arg;

    /*
     * Timeout handle for a recv
     */
    event_handle_t *ev_timeout;

    /*
     * Queue handles.  bsd_handle's that are blocked on input get added
     * to a queue.
     */
    TAILQ_ENTRY(bsd_handle) tq;
};

/*
 * This is the internal security_stream data
 */
struct bsd_stream {
    /*
     * This must be first, because instances of this will be cast
     * to security_stream_t's outside of this module.
     */
    security_stream_t security_stream;

    /*
     * For conveinence, this is a pointer to the security handle
     * in the above
     */
    struct bsd_handle *bsd_handle;

    /*
     * This is the file descriptor which we will do io on
     */
    int fd;

    /*
     * This is the file descriptor which we will listen for incoming
     * connections on (for streams which receive connections)
     */
    int socket;

    /*
     * This is the local port this stream is bound to
     */
    int port;

    /*
     * This is the read event handle for this data stream
     */
    event_handle_t *ev_read;

    /*
     * This is the function and argument that is called when this stream
     * is readable.  It is passed a buffer of data read.
     */
    void (*fn) P((void *, void *, int));
    void *arg;

    /*
     * This is the buffer that we read data into that will be passed
     * to the read callback.
     */
    char databuf[TAPE_BLOCK_BYTES];
};

/*
 * This is the tcp stream buffer size
 */
#define	STREAM_BUFSIZE	(TAPE_BLOCK_BYTES * 2)

/*
 * Interface functions
 */
static int bsd_connect P((void *, const char *));
static void bsd_accept P((int, int, void (*)(void *, void *, pkt_t *),
    void *));
static void bsd_close P((void *));
static int bsd_sendpkt P((void *, pkt_t *));
static void bsd_recvpkt P((void *,
    void (*)(void *, pkt_t *, security_recvpkt_status_t), void *, int));
static void bsd_recvpkt_cancel P((void *));

static void *bsd_stream_server P((void *));
static int bsd_stream_accept P((void *));
static void *bsd_stream_client P((void *, int));
static void bsd_stream_close P((void *));
static int bsd_stream_auth P((void *));
static int bsd_stream_id P((void *));
static int bsd_stream_write P((void *, const void *, size_t));
static void bsd_stream_read P((void *, void (*)(void *, void *, int),
    void *));
static void bsd_stream_read_cancel P((void *));

/*
 * This is our interface to the outside world
 */
const security_driver_t bsd_security_driver = {
    "BSD",
    sizeof(struct bsd_handle),
    bsd_connect,
    bsd_accept,
    bsd_close,
    bsd_sendpkt,
    bsd_recvpkt,
    bsd_recvpkt_cancel,
    bsd_stream_server,
    bsd_stream_accept,
    bsd_stream_client,
    bsd_stream_close,
    bsd_stream_auth,
    bsd_stream_id,
    bsd_stream_write,
    bsd_stream_read,
    bsd_stream_read_cancel,
};

/*
 * This is the dgram socket that is used for all transmissions from this
 * host.
 */
static dgram_t netfd;

/*
 * This is the event handle for read events on netfd
 */
static event_handle_t *ev_netfd;

/*
 * This is a queue of bsd_handles that are blocked waiting for packets
 * to arrive.
 */
static struct {
    TAILQ_HEAD(, bsd_handle) tailq;
    int qlength;
} handleq = {
    TAILQ_HEAD_INITIALIZER(handleq.tailq), 0
};

/*
 * Macros to add or remove bsd_handles from the above queue
 */
#define	handleq_add(kh)	do {			\
    assert(handleq.qlength == 0 ? TAILQ_FIRST(&handleq.tailq) == NULL : 1); \
    TAILQ_INSERT_TAIL(&handleq.tailq, kh, tq);	\
    handleq.qlength++;				\
} while (0)

#define	handleq_remove(kh)	do {		\
    assert(handleq.qlength > 0);		\
    assert(TAILQ_FIRST(&handleq.tailq) != NULL);\
    TAILQ_REMOVE(&handleq.tailq, kh, tq);	\
    handleq.qlength--;				\
    assert(handleq.qlength == 0 ? TAILQ_FIRST(&handleq.tailq) == NULL : 1); \
} while (0)

#define	handleq_first()		TAILQ_FIRST(&handleq.tailq)
#define	handleq_next(kh)	TAILQ_NEXT(kh, tq)

/*
 * This is the function and argument that is called when new requests
 * arrive on the netfd.
 */
static void (*accept_fn) P((void *, void *, pkt_t *));
static void *accept_fn_arg;

/*
 * These are the internal helper functions
 */
static int check_user P((struct bsd_handle *, const char *));
static int inithandle P((struct bsd_handle *, struct hostent *, int,
    const char *));
static const char *pkthdr2str P((const struct bsd_handle *, const pkt_t *));
static int str2pkthdr P((const char *, pkt_t *, char *, size_t, int *));
static void recvpkt_callback P((void *));
static void recvpkt_timeout P((void *));
static int recv_security_ok P((struct bsd_handle *, pkt_t *));
static void stream_read_callback P((void *));

/*
 * Setup and return a handle outgoing to a client
 */
static int
bsd_connect(cookie, hostname)
    void *cookie;
    const char *hostname;
{
    struct bsd_handle *bh = cookie;
    char handle[32];
    struct servent *se;
    struct hostent *he;
    int port;

    assert(hostname != NULL);
    assert(bh != NULL);

    /*
     * Only init the socket once
     */
    if (netfd.socket == 0) {
	dgram_zero(&netfd);
	dgram_bind(&netfd, &port);
	/*
	 * We must have a reserved port.  Bomb if we didn't get one.
	 */
	if (ntohs(port) >= IPPORT_RESERVED) {
	    security_seterror(&bh->security_handle,
		"unable to bind to a reserved port");
	    return (-1);
	}
    }

    if ((he = gethostbyname(hostname)) == NULL) {
	security_seterror(&bh->security_handle,
	    "%s: could not resolve hostname", hostname);
	return (-1);
    }
    if ((se = getservbyname(AMANDA_SERVICE_NAME, "udp")) == NULL)
	port = htons(AMANDA_SERVICE_DEFAULT);
    else
	port = se->s_port;
    ap_snprintf(handle, sizeof(handle), "%ld", (long)time(NULL));
    return (inithandle(bh, he, port, handle));
}

/*
 * Setup to accept new incoming connections
 */
static void
bsd_accept(in, out, fn, arg)
    int in, out;
    void (*fn) P((void *, void *, pkt_t *));
    void *arg;
{

    assert(in > 0 && out > 0);
    assert(fn != NULL);

    /*
     * We assume in and out point to the same socket, and just use
     * in.
     */
    dgram_socket(&netfd, in);

    /*
     * Assign the function and return.  When they call recvpkt later,
     * the recvpkt callback will call this function when it discovers
     * new incoming connections
     */
    accept_fn = fn;
    accept_fn_arg = arg;

    if (ev_netfd == NULL)
	ev_netfd = event_register(netfd.socket, EV_READFD, recvpkt_callback,
	    NULL);
}

/*
 * Given a hostname and a port, setup a bsd_handle
 */
static int
inithandle(bh, he, port, handle)
    struct bsd_handle *bh;
    struct hostent *he;
    int port;
    const char *handle;
{
    int i;

    assert(he != NULL);
    assert(port > 0);

    /*
     * Save the hostname and port info
     */
    strncpy(bh->hostname, he->h_name, sizeof(bh->hostname) - 1);
    bh->hostname[sizeof(bh->hostname) - 1] = '\0';
    bh->peer.sin_addr = *(struct in_addr *)he->h_addr;
    bh->peer.sin_port = port;
    bh->peer.sin_family = AF_INET;

    /*
     * Do a forward lookup of the hostname.  This is unnecessary if we
     * are initiating the connection, but is very serious if we are
     * receiving.  We want to make sure the hostname
     * resolves back to the remote ip for security reasons.
     */
    if ((he = gethostbyname(bh->hostname)) == NULL) {
	security_seterror(&bh->security_handle,
	    "%s: could not resolve hostname", bh->hostname);
	return (-1);
    }
    /*
     * Make sure the hostname matches.  This should always work.
     */
    if (strncasecmp(bh->hostname, he->h_name, strlen(bh->hostname)) != 0) {
	security_seterror(&bh->security_handle, "%s: did not resolve to %s",
	    bh->hostname, bh->hostname);
	return (-1);
    }

    /*
     * Now look for a matching ip address.
     */
    for (i = 0; he->h_addr_list[i] != NULL; i++) {
	if (memcmp(&bh->peer.sin_addr, he->h_addr_list[i],
	    sizeof(struct in_addr)) == 0) {
	    break;
	}
    }

    /*
     * If we didn't find it, try the aliases.  This is a workaround for
     * Solaris if DNS goes over NIS.
     */
    if (he->h_addr_list[i] == NULL) {
	const char *ipstr = inet_ntoa(bh->peer.sin_addr);
	for (i = 0; he->h_aliases[i] != NULL; i++) {
	    if (strcmp(he->h_aliases[i], ipstr) == 0)
		break;
	}
	/*
	 * No aliases either.  Failure.  Someone is fooling with us or
	 * DNS is messed up.
	 */
	if (he->h_aliases[i] == NULL) {
	    security_seterror(&bh->security_handle,
		"DNS check failed: no matching ip address for %s",
		bh->hostname);
	    return (-1);
	}
    }

    /*
     * No sequence number yet
     */
    bh->sequence = 0;
    strncpy(bh->proto_handle, handle, sizeof(bh->proto_handle) - 1);
    bh->proto_handle[sizeof(bh->proto_handle) - 1] = '\0';
    bh->fn = NULL;
    bh->arg = NULL;
    bh->ev_timeout = NULL;

    return (0);
}

/*
 * Frees a handle allocated by the above
 */
static void
bsd_close(bh)
    void *bh;
{

    /* nothing */
}

/*
 * Transmit a packet.  Add security information first.
 */
static int
bsd_sendpkt(cookie, pkt)
    void *cookie;
    pkt_t *pkt;
{
    struct bsd_handle *bh = cookie;
    char *p;
    struct passwd *pwd;

    assert(bh != NULL);
    assert(pkt != NULL);

    /*
     * Initialize this datagram, and add the header
     */
    dgram_zero(&netfd);
    dgram_cat(&netfd, pkthdr2str(bh, pkt));

    /*
     * Add the security info.  This depends on which kind of packet we're
     * sending.
     */
    switch (pkt->type) {
    case P_REQ:
	/*
	 * Requests get sent with our username in the body
	 */
	if ((pwd = getpwuid(getuid())) == NULL) {
	    security_seterror(&bh->security_handle,
		"can't get login name for my uid %ld", (long)getuid());
	    return (-1);
	}
	p = vstralloc("SECURITY USER ", pwd->pw_name, "\n", NULL);
	dgram_cat(&netfd, p);
	amfree(p);
	break;

    default:
	break;
    }

    /*
     * Add the body, and send it
     */
    dgram_cat(&netfd, pkt->body);
    if (dgram_send_addr(bh->peer, &netfd) != 0) {
	security_seterror(&bh->security_handle, "send %s to %s failed: %s",
	    pkt_type2str(pkt->type), bh->hostname, strerror(errno));
	return (-1);
    }
    return (0);
}

/*
 * Set up to receive a packet asynchronously, and call back when it has
 * been read.
 */
static void
bsd_recvpkt(cookie, fn, arg, timeout)
    void *cookie, *arg;
    void (*fn) P((void *, pkt_t *, security_recvpkt_status_t));
    int timeout;
{
    struct bsd_handle *bh = cookie;

    assert(bh != NULL);
    assert(fn != NULL);

    /*
     * We register one event handler for our network fd which takes
     * case of all of our async requests.  When all async requests
     * have either been satisfied or cancelled, we unregister
     * our handler.
     */
    if (ev_netfd == NULL) {
	assert(handleq.qlength == 0);
	ev_netfd = event_register(netfd.socket, EV_READFD, recvpkt_callback,
	    NULL);
    }

    /*
     * Subsequent recvpkt calls override previous ones
     */
    if (bh->fn == NULL)
	handleq_add(bh);
    if (bh->ev_timeout != NULL)
	event_release(bh->ev_timeout);
    if (timeout < 0)
	bh->ev_timeout = NULL;
    else
	bh->ev_timeout = event_register(timeout, EV_TIME, recvpkt_timeout, bh);
    bh->fn = fn;
    bh->arg = arg;
}

/*
 * Remove a async receive request on this handle from the queue.
 * If it is the last one to be removed, then remove the event
 * handler for our network fd
 */
static void
bsd_recvpkt_cancel(cookie)
    void *cookie;
{
    struct bsd_handle *bh = cookie;

    assert(bh != NULL);

    if (bh->fn != NULL) {
	handleq_remove(bh);
	bh->fn = NULL;
	bh->arg = NULL;
    }
    if (bh->ev_timeout != NULL)
	event_release(bh->ev_timeout);
    bh->ev_timeout = NULL;

    if (handleq.qlength == 0 && accept_fn == NULL &&
	ev_netfd != NULL) {
	event_release(ev_netfd);
	ev_netfd = NULL;
    }
}

/*
 * Callback for received packets.  This is the function bsd_recvpkt
 * registers with the event handler.  It is called when the event handler
 * realizes that data is waiting to be read on the network socket.
 */
static void
recvpkt_callback(cookie)
    void *cookie;
{
    char handle[32];
    struct sockaddr_in peer;
    pkt_t pkt;
    int sequence;
    struct bsd_handle *bh;
    struct hostent *he;
    void (*fn) P((void *, pkt_t *, security_recvpkt_status_t));
    void *arg;

    assert(cookie == NULL);

    /*
     * Receive the packet.
     */
    dgram_zero(&netfd);
    if (dgram_recv(&netfd, 0, &peer) < 0)
	return;

    /*
     * Parse the packet.
     */
    if (str2pkthdr(netfd.cur, &pkt, handle, sizeof(handle), &sequence) < 0)
	return;

    /*
     * Find the handle that this is associated with.
     */
    for (bh = handleq_first(); bh != NULL; bh = handleq_next(bh)) {
	if (strcmp(bh->proto_handle, handle) == 0 &&
	    memcmp(&bh->peer.sin_addr, &peer.sin_addr,
	    sizeof(peer.sin_addr)) == 0 &&
	    bh->peer.sin_port == peer.sin_port) {
	    bh->sequence = sequence;

	    /*
	     * We need to cancel the recvpkt request before calling
	     * the callback because the callback may reschedule us.
	     */
	    fn = bh->fn;
	    arg = bh->arg;
	    bsd_recvpkt_cancel(bh);
	    if (recv_security_ok(bh, &pkt) < 0)
		(*fn)(arg, NULL, RECV_ERROR);
	    else
		(*fn)(arg, &pkt, RECV_OK);
	    return;
	}
    }

    /*
     * If we didn't find a handle, then check for a new incoming packet.
     * If no accept handler was setup, then just return.
     */
    if (accept_fn == NULL)
	return;

    he = gethostbyaddr((void *)&peer.sin_addr, sizeof(peer.sin_addr), AF_INET);
    if (he == NULL)
	return;
    bh = alloc(sizeof(*bh));
    bh->security_handle.error = NULL;
    if (inithandle(bh, he, peer.sin_port, handle) < 0) {
	amfree(bh);
	return;
    }
    /*
     * Check the security of the packet.  If it is bad, then pass NULL
     * to the accept function instead of a packet.
     */
    if (recv_security_ok(bh, &pkt) < 0)
	(*accept_fn)(accept_fn_arg, bh, NULL);
    else
	(*accept_fn)(accept_fn_arg, bh, &pkt);
}

/*
 * This is called when a handle times out before receiving a packet.
 */
static void
recvpkt_timeout(cookie)
    void *cookie;
{
    struct bsd_handle *bh = cookie;
    void (*fn) P((void *, pkt_t *, security_recvpkt_status_t));
    void *arg;

    assert(bh != NULL);

    assert(bh->ev_timeout != NULL);
    fn = bh->fn;
    arg = bh->arg;
    bsd_recvpkt_cancel(bh);
    (*fn)(arg, NULL, RECV_TIMEOUT);

}

/*
 * Check the security of a received packet.  Returns negative on security
 * violation, or returns 0 if ok.  Removes the security info from the pkt_t.
 */
static int
recv_security_ok(bh, pkt)
    struct bsd_handle *bh;
    pkt_t *pkt;
{
    char *tok, *security, *body;

    /*
     * First, make sure the remote port is a "reserved" one
     */
    if (ntohs(bh->peer.sin_port) >= IPPORT_RESERVED) {
	security_seterror(&bh->security_handle, "host %s: port %d not secure",
	    bh->hostname, ntohs(bh->peer.sin_port));
	return (-1);
    }

    /*
     * Set this preempively before we mangle the body.  
     */
    security_seterror(&bh->security_handle, "bad SECURITY line: '%s'",
	pkt->body);

    /*
     * Now, find the SECURITY line in the body, and parse it out
     * into an argv.
     */
    if (strncmp(pkt->body, "SECURITY", sizeof("SECURITY") - 1) == 0) {
	tok = strtok(pkt->body, " ");
	assert(strcmp(tok, "SECURITY") == 0);
	/* security info goes until the newline */
	security = strtok(NULL, "\n");
	body = strtok(NULL, "");
	/*
	 * If the body is f-ked, then try to recover
	 */
	if (body == NULL) {
	    if (security != NULL)
		body = security + strlen(security) + 2;
	    else
		body = pkt->body;
	}
    } else {
	security = NULL;
	body = pkt->body;
    }

    /*
     * We need to do different things depending on which type of packet
     * this is.
     */
    switch (pkt->type) {
    case P_REQ:
	/*
	 * Request packets contain a remote username.  We need to check
	 * that we allow it in.
	 *
	 * They will look like:
	 *	SECURITY USER [username]
	 */

	/* there must be some security info */
	if (security == NULL) {
	    security_seterror(&bh->security_handle,
		"no bsd SECURITY for P_REQ");
	    return (-1);
	}

	/* second word must be USER */
	if ((tok = strtok(security, " ")) == NULL)
	    return (-1);	/* default errmsg */
	if (strcmp(tok, "USER") != 0) {
	    security_seterror(&bh->security_handle,
		"REQ SECURITY line parse error, expecting USER, got %s", tok);
	    return (-1);
	}

	/* the third word is the username */
	if ((tok = strtok(NULL, "")) == NULL)
	    return (-1);	/* default errmsg */
	if (check_user(bh, tok) < 0) {
	    security_seterror(&bh->security_handle, "%s not allowed access",
		tok);
	    return (-1);
	}

	/* we're good to go */
	break;
    default:
	break;
    }

    /*
     * If there is security info at the front of the packet, we need to
     * shift the rest of the data up and nuke it.
     */
    if (body != pkt->body)
	memmove(pkt->body, body, strlen(body) + 1);
    return (0);
}

#ifndef USE_AMANDAHOSTS
/*
 * See if a remote user is allowed in.  This version uses ruserok()
 * and friends.
 *
 * Returns 0 on success, or negative on error.
 */
static int
check_user(bh, remoteuser)
    struct bsd_handle *bh;
    const char *remoteuser;
{
    uid_t uid;
    struct passwd *pwd;
    int saved_stderr;
    char *localuser;

    /* lookup our local user name */
    uid = getuid();
    if ((pwd = getpwuid(uid)) == NULL)
        error("error [getpwuid(%ld) fails]", uid);
    localuser = stralloc(pwd->pw_name);

    /*
     * note that some versions of ruserok (eg SunOS 3.2) look in
     * "./.rhosts" rather than "~localuser/.rhosts", so we have to
     * chdir ourselves.  Sigh.
     *
     * And, believe it or not, some ruserok()'s try an initgroup just
     * for the hell of it.  Since we probably aren't root at this point
     * it'll fail, and initgroup "helpfully" will blatt "Setgroups: Not owner"
     * into our stderr output even though the initgroup failure is not a
     * problem and is expected.  Thanks a lot.  Not.
     */
    chdir(pwd->pw_dir);       /* pamper braindead ruserok's */
    saved_stderr = dup(2);
    close(2);

    if (ruserok(bh->hostname, uid == 0, remoteuser, localuser) < 0) {
	dup2(saved_stderr,2);
	close(saved_stderr);
	amfree(localuser);
	return (-1);
    }

    /*
     * Restore stderr
     */
    dup2(saved_stderr, 2);
    close(saved_stderr);

    /*
     * Get out of the homedir and go somewhere where we can't drop core :)
     */
    chdir("/");	

    amfree(localuser);
    return (0);
}

#else	/* USE_AMANDAHOSTS */

/*
 * Check to see if a user is allowed in.  This version uses .amandahosts
 * Returns -1 on failure, or 0 on success.
 */
static int
check_user(bh, remoteuser)
    struct bsd_handle *bh;
    const char *remoteuser;
{
    char buf[256], *localuser, *filehost;
    const char *fileuser;
    uid_t uid;
    struct passwd *pwd;
    FILE *fp;
    int rval;

    /* lookup our local user name */

    uid = getuid();
    if ((pwd = getpwuid(uid)) == NULL)
        error("error [getpwuid(%d) fails]", uid);
    localuser = pwd->pw_name;

    chdir(pwd->pw_dir);
    if ((fp = fopen(".amandahosts", "r")) == NULL)
	return (-1);

    rval = -1;	/* assume failure */
    while (fgets(buf, sizeof(buf), fp) != NULL) {
	/* get the host out of the file */
	if ((filehost = strtok(buf, " \t")) == NULL)
	    continue;

	/* get the username.  If no user specified, then use the remote user */
	if ((fileuser = strtok(NULL, "\n")) == NULL)
	    fileuser = remoteuser;

	/* compare */
	if (strcasecmp(filehost, bh->hostname) == 0 &&
	    strcasecmp(fileuser, remoteuser) == 0) {
		/* success */
		rval = 0;
		break;
	}
    }
    afclose(fp);
    chdir("/");      /* now go someplace where I can't drop core :-) */
    return (rval);
}
#endif	/* USE_AMANDAHOSTS */

/*
 * Create the server end of a stream.  For bsd, this means setup a tcp
 * socket for receiving a connection.
 */
static void *
bsd_stream_server(h)
    void *h;
{
    struct bsd_handle *bh = h;
    struct bsd_stream *bs;

    assert(bh != NULL);

    bs = alloc(sizeof(*bs));
    bs->socket = stream_server(&bs->port, STREAM_BUFSIZE, STREAM_BUFSIZE);
    if (bs->socket < 0) {
	security_seterror(&bh->security_handle,
	    "can't create server stream: %s", strerror(errno));
	amfree(bs);
	return (NULL);
    }
    bs->fd = -1;
    bs->bsd_handle = bh;
    bs->ev_read = NULL;
    return (bs);
}

/*
 * Accepts a new connection on unconnected streams.  Assumes it is ok to
 * block on accept()
 */
static int
bsd_stream_accept(s)
    void *s;
{
    struct bsd_stream *bs = s;

    assert(bs != NULL);
    assert(bs->socket != -1);
    assert(bs->fd < 0);

    bs->fd = stream_accept(bs->socket, 30, DEFAULT_SIZE, DEFAULT_SIZE);
    if (bs->fd < 0) {
	security_seterror(&bs->bsd_handle->security_handle,
	    "can't accept new stream connection: %s", strerror(errno));
	return (-1);
    }
    return (0);
}

/*
 * Return a connected stream
 */
static void *
bsd_stream_client(h, id)
    void *h;
    int id;
{
    struct bsd_handle *bh = h;
    struct bsd_stream *bs;

    assert(bh != NULL);

    if (id < 0) {
	security_seterror(&bh->security_handle,
	    "%d: invalid security stream id", id);
	return (NULL);
    }

    bs = alloc(sizeof(*bs));
    bs->fd = stream_client(bh->hostname, id, STREAM_BUFSIZE, STREAM_BUFSIZE,
	&bs->port);
    if (bs->fd < 0) {
	security_seterror(&bh->security_handle,
	    "can't connect stream to %s port %d: %s", bh->hostname, id,
	    strerror(errno));
	amfree(bs);
	return (NULL);
    }
    bs->socket = -1;	/* we're a client */
    bs->bsd_handle = bh;
    bs->ev_read = NULL;
    return (bs);
}

/*
 * Close and unallocate resources for a stream
 */
static void
bsd_stream_close(s)
    void *s;
{
    struct bsd_stream *bs = s;

    assert(bs != NULL);

    if (bs->fd != -1)
	aclose(bs->fd);
    if (bs->socket != -1)
	aclose(bs->socket);
    bsd_stream_read_cancel(bs);
    amfree(bs);
}

/*
 * Authenticate a stream.  bsd streams have no authentication
 */
static int
bsd_stream_auth(s)
    void *s;
{

    return (0);	/* success */
}

/*
 * Returns the stream id for this stream.  This is just the local port.
 */
static int
bsd_stream_id(s)
    void *s;
{
    struct bsd_stream *bs = s;

    assert(bs != NULL);

    return (bs->port);
}

/*
 * Write a chunk of data to a stream.  Blocks until completion.
 */
static int
bsd_stream_write(s, vbuf, size)
    void *s;
    const void *vbuf;
    size_t size;
{
    struct bsd_stream *bs = s;
    const char *buf = vbuf;
    int n;

    assert(bs != NULL);

    /*
     * Write out all the data
     */
    while (size > 0) {
	n = write(bs->fd, buf, size);
	if (n < 0) {
	    security_seterror(&bs->bsd_handle->security_handle,
		"write error on stream %d: %s", bs->port, strerror(errno));
	    return (-1);
	}
	buf += n;
	size -= n;
    }
    return (0);
}

/*
 * Submit a request to read some data.  Calls back with the given function
 * and arg when completed.
 */
static void
bsd_stream_read(s, fn, arg)
    void *s, *arg;
    void (*fn) P((void *, void *, int));
{
    struct bsd_stream *bs = s;
    int fd;

    /*
     * Only one read request can be active per stream.
     */
    if (bs->ev_read != NULL)
	event_release(bs->ev_read);

    /*
     * If we haven't accepted a connection yet, just select on the socket.
     */
    if (bs->fd < 0)
	fd = bs->socket;
    else
	fd = bs->fd;

    bs->ev_read = event_register(fd, EV_READFD, stream_read_callback, bs);
    bs->fn = fn;
    bs->arg = arg;
}

/*
 * Cancel a previous stream read request.  It's ok if we didn't
 * have a read scheduled.
 */
static void
bsd_stream_read_cancel(s)
    void *s;
{
    struct bsd_stream *bs = s;

    assert(bs != NULL);

    if (bs->ev_read != NULL) {
	event_release(bs->ev_read);
	bs->ev_read = NULL;
    }
}

/*
 * Callback for bsd_stream_read
 */
static void
stream_read_callback(arg)
    void *arg;
{
    struct bsd_stream *bs = arg;
    size_t n;

    assert(bs != NULL);

    /*
     * Remove the event first, in case they reschedule it in the callback.
     */
    bsd_stream_read_cancel(bs);
    n = read(bs->fd, bs->databuf, sizeof(bs->databuf));
    if (n < 0)
	security_seterror(&bs->bsd_handle->security_handle,
	    strerror(errno));
    (*bs->fn)(bs->arg, bs->databuf, n);
}

/*
 * Convert a packet header into a string
 */
static const char *
pkthdr2str(bh, pkt)
    const struct bsd_handle *bh;
    const pkt_t *pkt;
{
    static char retbuf[256];

    assert(bh != NULL);
    assert(pkt != NULL);

    ap_snprintf(retbuf, sizeof(retbuf), "Amanda %d.%d %s HANDLE %s SEQ %d\n",
	VERSION_MAJOR, VERSION_MINOR, pkt_type2str(pkt->type),
	bh->proto_handle, bh->sequence);

    /* check for truncation.  If only we had asprintf()... */
    assert(retbuf[strlen(retbuf) - 1] == '\n');

    return (retbuf);
}

/*
 * Parses out the header line in 'str' into the pkt and handle
 * Returns negative on parse error.
 */
static int
str2pkthdr(origstr, pkt, handle, handlesize, sequence)
    const char *origstr;
    pkt_t *pkt;
    char *handle;
    size_t handlesize;
    int *sequence;
{
    char *str;
    const char *tok;

    assert(origstr != NULL);
    assert(pkt != NULL);

    str = stralloc(origstr);

    /* "Amanda %d.%d <ACK,NAK,...> HANDLE %s SEQ %d\n" */

    /* Read in "Amanda" */
    if ((tok = strtok(str, " ")) == NULL || strcmp(tok, "Amanda") != 0)
	goto parse_error;

    /* nothing is done with the major/minor numbers currently */
    if ((tok = strtok(NULL, " ")) == NULL || strchr(tok, '.') == NULL)
	goto parse_error;

    /* Read in the packet type */
    if ((tok = strtok(NULL, " ")) == NULL)
	goto parse_error;
    pkt_init(pkt, pkt_str2type(tok), "");
    if (pkt->type == (pktype_t)-1)    
	goto parse_error;

    /* Read in "HANDLE" */
    if ((tok = strtok(NULL, " ")) == NULL || strcmp(tok, "HANDLE") != 0)
	goto parse_error;

    /* parse the handle */
    if ((tok = strtok(NULL, " ")) == NULL)
	goto parse_error;
    strncpy(handle, tok, handlesize - 1);
    handle[handlesize - 1] = '\0';    

    /* Read in "SEQ" */
    if ((tok = strtok(NULL, " ")) == NULL || strcmp(tok, "SEQ") != 0)   
	goto parse_error;

    /* parse the sequence number */   
    if ((tok = strtok(NULL, "\n")) == NULL)
	goto parse_error;
    *sequence = atoi(tok);

    /* Save the body, if any */       
    if ((tok = strtok(NULL, "")) != NULL)
	pkt_cat(pkt, tok);

    amfree(str);
    return (0);

parse_error:
#if 0 /* XXX we have no way of passing this back up */
    security_seterror(&bh->security_handle,
	"parse error in packet header : '%s'", origstr);
#endif
    amfree(str);
    return (-1);
}

#endif	/* BSD_SECURITY */
