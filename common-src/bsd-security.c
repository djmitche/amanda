/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991-1999 University of Maryland at College Park
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
 * $Id: bsd-security.c,v 1.39 2002/03/31 21:02:00 jrjackson Exp $
 *
 * "BSD" security module
 */

#include "amanda.h"
#include "util.h"
#include "dgram.h"
#include "event.h"
#include "packet.h"
#include "security.h"
#include "stream.h"
#include "version.h"

#ifndef SO_RCVBUF
#undef DUMPER_SOCKET_BUFFERING
#endif

#ifdef BSD_SECURITY						/* { */

/*
 * Change the following from #undef to #define to cause detailed logging
 * of the security steps, e.g. into /tmp/amanda/amandad*debug.
 */
#undef SHOW_SECURITY_DETAIL

#if defined(TEST)						/* { */
#define SHOW_SECURITY_DETAIL
#undef dbprintf
#define dbprintf(p)	printf p
#endif								/* } */

/*
 * This is the private handle data
 */
struct bsd_handle {
    /*
     * This must be first.  Instances of bsd_handle will be cast to
     * security_handle_t's.
     */
    security_handle_t sech;

    /*
     * protocol handle for this request.  Each request gets its own
     * handle, so we differentiate packets for them with a "handle" header
     * in each packet.
     */
    int proto_handle;

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
    void (*fn) P((void *, pkt_t *, security_status_t));

    /*
     * Argument for previous function
     */
    void *arg;

    /*
     * read (EV_WAIT) handle for a recv
     */
    event_handle_t *ev_read;

    /*
     * Timeout handle for a recv
     */
    event_handle_t *ev_timeout;
};

/*
 * This is the internal security_stream data
 */
struct bsd_stream {
    /*
     * This must be first, because instances of this will be cast
     * to security_stream_t's outside of this module.
     */
    security_stream_t secstr;

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
    void (*fn) P((void *, void *, ssize_t));
    void *arg;

    /*
     * This is the buffer that we read data into that will be passed
     * to the read callback.
     */
    char databuf[NETWORK_BLOCK_BYTES];
};

/*
 * Interface functions
 */
static void bsd_connect P((const char *,
    void (*)(void *, security_handle_t *, security_status_t), void *));
static void bsd_accept P((int, int, void (*)(security_handle_t *, pkt_t *)));
static void bsd_close P((void *));
static int bsd_sendpkt P((void *, pkt_t *));
static void bsd_recvpkt P((void *,
    void (*)(void *, pkt_t *, security_status_t), void *, int));
static void bsd_recvpkt_cancel P((void *));

static void *bsd_stream_server P((void *));
static int bsd_stream_accept P((void *));
static void *bsd_stream_client P((void *, int));
static void bsd_stream_close P((void *));
static int bsd_stream_auth P((void *));
static int bsd_stream_id P((void *));
static int bsd_stream_write P((void *, const void *, size_t));
static void bsd_stream_read P((void *, void (*)(void *, void *, ssize_t),
    void *));
static void bsd_stream_read_cancel P((void *));

/*
 * This is our interface to the outside world
 */
const security_driver_t bsd_security_driver = {
    "BSD",
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
 * This is data local to the datagram socket.  We have one datagram
 * per process, so it is global.
 */
static struct {
    dgram_t dgram;		/* datagram to read/write from */
    struct sockaddr_in peer;	/* who sent it to us */
    pkt_t pkt;			/* parsed form of dgram */
    int handle;			/* handle from recvd packet */
    int sequence;		/* seq no of packet */
    event_handle_t *ev_read;	/* read event handle from dgram */
    int refcnt;			/* number of handles blocked for reading */
} netfd;

/* generate new handles from here */
static int newhandle = 0;

/*
 * We register one event handler for our network fd which takes
 * care of all of our async requests.  When all async requests
 * have either been satisfied or cancelled, we unregister our
 * network event handler.
 */
#define	netfd_addref()	do	{					\
    if (netfd.refcnt++ == 0) {						\
	assert(netfd.ev_read == NULL);					\
	netfd.ev_read = event_register(netfd.dgram.socket, EV_READFD,	\
	    netfd_read_callback, NULL);					\
    }									\
    assert(netfd.refcnt > 0);						\
} while (0)

/*
 * If this is the last request to be removed, then remove the
 * reader event from the netfd.
 */
#define	netfd_delref()	do	{					\
    assert(netfd.refcnt > 0);						\
    if (--netfd.refcnt == 0) {						\
	assert(netfd.ev_read != NULL);					\
	event_release(netfd.ev_read);					\
	netfd.ev_read = NULL;						\
    }									\
} while (0)

/*
 * This is the function and argument that is called when new requests
 * arrive on the netfd.
 */
static void (*accept_fn) P((security_handle_t *, pkt_t *));

/*
 * These are the internal helper functions
 */
static char *check_user P((struct bsd_handle *, const char *));
static int inithandle P((struct bsd_handle *, struct hostent *, int, int));
static const char *pkthdr2str P((const struct bsd_handle *, const pkt_t *));
static int str2pkthdr P((void));
static void netfd_read_callback P((void *));
static void recvpkt_callback P((void *));
static void recvpkt_timeout P((void *));
static int recv_security_ok P((struct bsd_handle *));
static void stream_read_callback P((void *));


#if defined(SHOW_SECURITY_DETAIL)				/* { */
/*
 * Display stat() information about a file.
 */
void show_stat_info(a, b)
    char *a, *b;
{
    char *name = vstralloc(a, b, NULL);
    struct stat sbuf;
    struct passwd *pwptr;
    char *owner;
    struct group *grptr;
    char *group;

    if (stat(name, &sbuf) != 0) {
	dbprintf(("%s: cannot stat %s: %s\n",
		  debug_prefix_time(NULL), name, strerror(errno)));
	amfree(name);
	return;
    }
    if ((pwptr = getpwuid(sbuf.st_uid)) == NULL) {
	owner = alloc(NUM_STR_SIZE + 1);
	snprintf(owner, NUM_STR_SIZE, "%ld", (long)sbuf.st_uid);
    } else {
	owner = stralloc(pwptr->pw_name);
    }
    if ((grptr = getgrgid(sbuf.st_gid)) == NULL) {
	group = alloc(NUM_STR_SIZE + 1);
	snprintf(owner, NUM_STR_SIZE, "%ld", (long)sbuf.st_gid);
    } else {
	group = stralloc(grptr->gr_name);
    }
    dbprintf(("%s: processing file: %s\n", debug_prefix(NULL), name));
    dbprintf(("%s:                  owner=%s group=%s mode=%03o\n",
	      debug_prefix(NULL), owner, group, (int) (sbuf.st_mode & 0777)));
    amfree(name);
    amfree(owner);
    amfree(group);
}
#endif								/* } */

/*
 * Setup and return a handle outgoing to a client
 */
static void
bsd_connect(hostname, fn, arg)
    const char *hostname;
    void (*fn) P((void *, security_handle_t *, security_status_t));
    void *arg;
{
    struct bsd_handle *bh;
    struct servent *se;
    struct hostent *he;
    int port;

    assert(hostname != NULL);

    bh = alloc(sizeof(*bh));
    security_handleinit(&bh->sech, &bsd_security_driver);

    /*
     * Only init the socket once
     */
    if (netfd.dgram.socket == 0) {
	uid_t euid;
	dgram_zero(&netfd.dgram);
	
	euid = geteuid();
	seteuid(0);
	dgram_bind(&netfd.dgram, &port);
	seteuid(euid);
	/*
	 * We must have a reserved port.  Bomb if we didn't get one.
	 */
	if (port >= IPPORT_RESERVED) {
	    security_seterror(&bh->sech,
		"unable to bind to a reserved port (got port %d)",
		port);
	    (*fn)(arg, &bh->sech, S_ERROR);
	    return;
	}
    }

    if ((he = gethostbyname(hostname)) == NULL) {
	security_seterror(&bh->sech,
	    "%s: could not resolve hostname", hostname);
	(*fn)(arg, &bh->sech, S_ERROR);
	return;
    }
    if ((se = getservbyname(AMANDA_SERVICE_NAME, "udp")) == NULL)
	port = htons(AMANDA_SERVICE_DEFAULT);
    else
	port = se->s_port;
    if (inithandle(bh, he, port, newhandle++) < 0)
	(*fn)(arg, &bh->sech, S_ERROR);
    else
	(*fn)(arg, &bh->sech, S_OK);
}

/*
 * Setup to accept new incoming connections
 */
static void
bsd_accept(in, out, fn)
    int in, out;
    void (*fn) P((security_handle_t *, pkt_t *));
{

    assert(in >= 0 && out >= 0);
    assert(fn != NULL);

    /*
     * We assume in and out point to the same socket, and just use
     * in.
     */
    dgram_socket(&netfd.dgram, in);

    /*
     * Assign the function and return.  When they call recvpkt later,
     * the recvpkt callback will call this function when it discovers
     * new incoming connections
     */
    accept_fn = fn;

    netfd_addref();
}

/*
 * Given a hostname and a port, setup a bsd_handle
 */
static int
inithandle(bh, he, port, handle)
    struct bsd_handle *bh;
    struct hostent *he;
    int port, handle;
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
	security_seterror(&bh->sech,
	    "%s: could not resolve hostname", bh->hostname);
	return (-1);
    }
    /*
     * Make sure the hostname matches.  This should always work.
     */
    if (strncasecmp(bh->hostname, he->h_name, strlen(bh->hostname)) != 0) {
	security_seterror(&bh->sech,
	    "%s: did not resolve to %s", bh->hostname, bh->hostname);
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
	    security_seterror(&bh->sech,
		"DNS check failed: no matching ip address for %s",
		bh->hostname);
	    return (-1);
	}
    }

    /*
     * No sequence number yet
     */
    bh->sequence = 0;
    bh->proto_handle = handle;
    bh->fn = NULL;
    bh->arg = NULL;
    bh->ev_read = NULL;
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

    bsd_recvpkt_cancel(bh);
    amfree(bh);
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
    struct passwd *pwd;

    assert(bh != NULL);
    assert(pkt != NULL);

    /*
     * Initialize this datagram, and add the header
     */
    dgram_zero(&netfd.dgram);
    dgram_cat(&netfd.dgram, pkthdr2str(bh, pkt));

    /*
     * Add the security info.  This depends on which kind of packet we're
     * sending.
     */
    switch (pkt->type) {
    case P_REQ:
	/*
	 * Requests get sent with our username in the body
	 */
	if ((pwd = getpwuid(geteuid())) == NULL) {
	    security_seterror(&bh->sech,
		"can't get login name for my uid %ld", (long)getuid());
	    return (-1);
	}
	dgram_cat(&netfd.dgram, "SECURITY USER %s\n", pwd->pw_name);
	break;

    default:
	break;
    }

    /*
     * Add the body, and send it
     */
    dgram_cat(&netfd.dgram, pkt->body);
    if (dgram_send_addr(bh->peer, &netfd.dgram) != 0) {
	security_seterror(&bh->sech,
	    "send %s to %s failed: %s", pkt_type2str(pkt->type),
	    bh->hostname, strerror(errno));
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
    void (*fn) P((void *, pkt_t *, security_status_t));
    int timeout;
{
    struct bsd_handle *bh = cookie;

    assert(bh != NULL);
    assert(fn != NULL);


    /*
     * Subsequent recvpkt calls override previous ones
     */
    if (bh->ev_read == NULL) {
	netfd_addref();
	bh->ev_read = event_register(bh->proto_handle, EV_WAIT,
	    recvpkt_callback, bh);
    }
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

    if (bh->ev_read != NULL) {
	netfd_delref();
	event_release(bh->ev_read);
	bh->ev_read = NULL;
    }

    if (bh->ev_timeout != NULL) {
	event_release(bh->ev_timeout);
	bh->ev_timeout = NULL;
    }
}

/*
 * Callback for received packets.  This is the function bsd_recvpkt
 * registers with the event handler.  It is called when the event handler
 * realizes that data is waiting to be read on the network socket.
 */
static void
netfd_read_callback(cookie)
    void *cookie;
{
    struct bsd_handle *bh;
    struct hostent *he;

    assert(cookie == NULL);

#ifndef TEST							/* { */
    /*
     * Receive the packet.
     */
    dgram_zero(&netfd.dgram);
    if (dgram_recv(&netfd.dgram, 0, &netfd.peer) < 0)
	return;
#endif /* !TEST */						/* } */

    /*
     * Parse the packet.
     */
    if (str2pkthdr() < 0)
	return;

    /*
     * If there are events waiting on this handle, we're done
     */
    if (event_wakeup(netfd.handle) > 0)
	return;

    /*
     * If we didn't find a handle, then check for a new incoming packet.
     * If no accept handler was setup, then just return.
     */
    if (accept_fn == NULL)
	return;

    he = gethostbyaddr((void *)&netfd.peer.sin_addr,
	(int)sizeof(netfd.peer.sin_addr), AF_INET);
    if (he == NULL)
	return;
    bh = alloc(sizeof(*bh));
    security_handleinit(&bh->sech, &bsd_security_driver);
    if (inithandle(bh, he, netfd.peer.sin_port, netfd.handle) < 0) {
	amfree(bh);
	return;
    }
    /*
     * Check the security of the packet.  If it is bad, then pass NULL
     * to the accept function instead of a packet.
     */
    if (recv_security_ok(bh) < 0)
	(*accept_fn)(&bh->sech, NULL);
    else
	(*accept_fn)(&bh->sech, &netfd.pkt);
}

/*
 * This is called when a handle is woken up because data read off of the
 * net is for it.
 */
static void
recvpkt_callback(cookie)
    void *cookie;
{
    struct bsd_handle *bh = cookie;
    void (*fn) P((void *, pkt_t *, security_status_t));
    void *arg;

    assert(bh != NULL);
    assert(bh->proto_handle == netfd.handle);

    /* if it didn't come from the same host/port, forget it */
    if (memcmp(&bh->peer.sin_addr, &netfd.peer.sin_addr,
	sizeof(netfd.peer.sin_addr)) != 0 ||
	bh->peer.sin_port != netfd.peer.sin_port) {
	netfd.handle = -1;
	return;
    }

    /*
     * We need to cancel the recvpkt request before calling the callback
     * because the callback may reschedule us.
     */
    fn = bh->fn;
    arg = bh->arg;
    bsd_recvpkt_cancel(bh);

    /*
     * Check the security of the packet.  If it is bad, then pass NULL
     * to the packet handling function instead of a packet.
     */
    if (recv_security_ok(bh) < 0)
	(*fn)(arg, NULL, S_ERROR);
    else
	(*fn)(arg, &netfd.pkt, S_OK);
}

/*
 * This is called when a handle times out before receiving a packet.
 */
static void
recvpkt_timeout(cookie)
    void *cookie;
{
    struct bsd_handle *bh = cookie;
    void (*fn) P((void *, pkt_t *, security_status_t));
    void *arg;

    assert(bh != NULL);

    assert(bh->ev_timeout != NULL);
    fn = bh->fn;
    arg = bh->arg;
    bsd_recvpkt_cancel(bh);
    (*fn)(arg, NULL, S_TIMEOUT);

}

/*
 * Check the security of a received packet.  Returns negative on security
 * violation, or returns 0 if ok.  Removes the security info from the pkt_t.
 */
static int
recv_security_ok(bh)
    struct bsd_handle *bh;
{
    char *tok, *security, *body, *result;
    pkt_t *pkt = &netfd.pkt;

    /*
     * Set this preempively before we mangle the body.  
     */
    security_seterror(&bh->sech,
	"bad SECURITY line: '%s'", pkt->body);

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
	 * Request packets must come from a reserved port
	 */
	if (ntohs(bh->peer.sin_port) >= IPPORT_RESERVED) {
	    security_seterror(&bh->sech,
		"host %s: port %d not secure", bh->hostname,
		ntohs(bh->peer.sin_port));
	    return (-1);
	}

	/*
	 * Request packets contain a remote username.  We need to check
	 * that we allow it in.
	 *
	 * They will look like:
	 *	SECURITY USER [username]
	 */

	/* there must be some security info */
	if (security == NULL) {
	    security_seterror(&bh->sech,
		"no bsd SECURITY for P_REQ");
	    return (-1);
	}

	/* second word must be USER */
	if ((tok = strtok(security, " ")) == NULL)
	    return (-1);	/* default errmsg */
	if (strcmp(tok, "USER") != 0) {
	    security_seterror(&bh->sech,
		"REQ SECURITY line parse error, expecting USER, got %s", tok);
	    return (-1);
	}

	/* the third word is the username */
	if ((tok = strtok(NULL, "")) == NULL)
	    return (-1);	/* default errmsg */
	if ((result = check_user(bh, tok)) != NULL) {
	    security_seterror(&bh->sech, "%s", result);
	    amfree(result);
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

static char *
check_user(bh, remoteuser)
    struct bsd_handle *bh;
    const char *remoteuser;
{
    struct passwd *pwd;
    char *r;
    char *result = NULL;
    char *localuser = NULL;

    /* lookup our local user name */
    if ((pwd = getpwnam(CLIENT_LOGIN)) == NULL) {
	return vstralloc("getpwnam(", CLIENT_LOGIN, ") fails", NULL);
    }

    /*
     * Make a copy of the user name in case getpw* is called by
     * any of the lower level routines.
     */
    localuser = stralloc(pwd->pw_name);

#ifndef USE_AMANDAHOSTS
    r = check_user_ruserok(bh->hostname, pwd, remoteuser);
#else
    r = check_user_amandahosts(bh->hostname, pwd, remoteuser);
#endif
    if (r != NULL) {
	result = vstralloc("access as ", localuser, " not allowed",
			   " from ", remoteuser, "@", bh->hostname,
			   ": ", r,
			   NULL);
	amfree(r);
    }
    amfree(localuser);
    return result;
}

/*
 * See if a remote user is allowed in.  This version uses ruserok()
 * and friends.
 *
 * Returns 0 on success, or negative on error.
 */
char *
check_user_ruserok(host, pwd, remoteuser)
    const char *host;
    struct passwd *pwd;
    const char *remoteuser;
{
    int saved_stderr;
    int fd[2];
    FILE *fError;
    amwait_t exitcode;
    pid_t ruserok_pid;
    pid_t pid;
    char *es;
    char *result;
    int ok;
    char number[NUM_STR_SIZE];
    uid_t myuid = getuid();

    /*
     * note that some versions of ruserok (eg SunOS 3.2) look in
     * "./.rhosts" rather than "~CLIENT_LOGIN/.rhosts", so we have to
     * chdir ourselves.  Sigh.
     *
     * And, believe it or not, some ruserok()'s try an initgroup just
     * for the hell of it.  Since we probably aren't root at this point
     * it'll fail, and initgroup "helpfully" will blatt "Setgroups: Not owner"
     * into our stderr output even though the initgroup failure is not a
     * problem and is expected.  Thanks a lot.  Not.
     */
    if (pipe(fd) != 0) {
	return stralloc2("pipe() fails: ", strerror(errno));
    }
    if ((ruserok_pid = fork()) < 0) {
	return stralloc2("fork() fails: ", strerror(errno));
    } else if (ruserok_pid == 0) {
	int ec;

	close(fd[0]);
	fError = fdopen(fd[1], "w");
	/* pamper braindead ruserok's */
	if (chdir(pwd->pw_dir) != 0) {
	    fprintf(fError, "chdir(%s) failed: %s",
		    pwd->pw_dir, strerror(errno));
	    fclose(fError);
	    exit(1);
	}

#if defined(SHOW_SECURITY_DETAIL)				/* { */
	{
	char *dir = stralloc(pwd->pw_dir);

	dbprintf(("%s: calling ruserok(%s, %d, %s, %s)\n",
		  debug_prefix_time(NULL),
	          host, myuid == 0, remoteuser, pwd->pw_name));
	if (myuid == 0) {
	    dbprintf(("%s: because you are running as root, "
		      debug_prefix(NULL)));
	    dbprintf(("/etc/hosts.equiv will not be used\n"));
	} else {
	    show_stat_info("/etc/hosts.equiv", NULL);
	}
	show_stat_info(dir, "/.rhosts");
	amfree(dir);
	}
#endif								/* } */

	saved_stderr = dup(2);
	close(2);
	(void) open("/dev/null", 2);

	ok = ruserok(host, myuid == 0, remoteuser, CLIENT_LOGIN);
	if (ok < 0) {
	    ec = 1;
	} else {
	    ec = 0;
	}
	dup2(saved_stderr,2);
	close(saved_stderr);
	exit(ec);
    }
    close(fd[1]);
    fError = fdopen(fd[0], "r");

    result = NULL;
    while ((es = agets(fError)) != NULL) {
	if (result == NULL) {
	    result = stralloc("");
	} else {
	    strappend(result, ": ");
	}
	strappend(result, es);
    }
    close(fd[0]);

    while (1) {
	if ((pid = wait(&exitcode)) == (pid_t) -1) {
	    if (errno == EINTR) {
		continue;
	    }
	    amfree(result);
	    return stralloc2("ruserok wait failed: %s", strerror(errno));
	}
	if (pid == ruserok_pid) {
	    break;
	}
    }
    if (WIFSIGNALED(exitcode)) {
	amfree(result);
	snprintf(number, sizeof(number), "%d", WTERMSIG(exitcode));
	return stralloc2("ruserok child got signal ", number);
    }
    if (WEXITSTATUS(exitcode) == 0) {
	amfree(result);
    } else if (result == NULL) {
	result = stralloc("ruserok failed");
    }

    return result;
}

/*
 * Check to see if a user is allowed in.  This version uses .amandahosts
 * Returns -1 on failure, or 0 on success.
 */
char *
check_user_amandahosts(host, pwd, remoteuser)
    const char *host;
    struct passwd *pwd;
    const char *remoteuser;
{
    char *line = NULL;
    char *filehost;
    const char *fileuser;
    char *ptmp = NULL;
    char *result = NULL;
    FILE *fp = NULL;
    int found;
    struct stat sbuf;
    char n1[NUM_STR_SIZE];
    char n2[NUM_STR_SIZE];
    int hostmatch;
    int usermatch;
    uid_t localuid;
    char *localuser = NULL;

    /*
     * Save copies of what we need from the passwd structure in case
     * any other code calls getpw*.
     */
    localuid = pwd->pw_uid;
    localuser = stralloc(pwd->pw_name);

    ptmp = stralloc2(pwd->pw_dir, "/.amandahosts");
#if defined(SHOW_SECURITY_DETAIL)				/* { */
    show_stat_info(ptmp, "");;
#endif								/* } */
    if ((fp = fopen(ptmp, "r")) == NULL) {
	result = vstralloc("cannot open ", ptmp, ": ", strerror(errno), NULL);
	amfree(ptmp);
	return result;
    }

    /*
     * Make sure the file is owned by the Amanda user and does not
     * have any group/other access allowed.
     */
    if (fstat(fileno(fp), &sbuf) != 0) {
	result = vstralloc("cannot fstat ", ptmp, ": ", strerror(errno), NULL);
	goto common_exit;
    }
    if (sbuf.st_uid != localuid) {
	snprintf(n1, sizeof(n1), "%ld", (long)sbuf.st_uid);
	snprintf(n2, sizeof(n2), "%ld", (long)localuid);
	result = vstralloc(ptmp, ": ",
			   "owned by id ", n1,
			   ", should be ", n2,
			   NULL);
	goto common_exit;
    }
    if ((sbuf.st_mode & 077) != 0) {
	result = stralloc2(ptmp,
	  ": incorrect permissions; file must be accessible only by its owner");
	goto common_exit;
    }

    /*
     * Now, scan the file for the host/user.
     */
    found = 0;
    while ((line = agets(fp)) != NULL) {
#if defined(SHOW_SECURITY_DETAIL)				/* { */
	dbprintf(("%s: processing line: <%s>\n", debug_prefix(NULL), line));
#endif								/* } */
	/* get the host out of the file */
	if ((filehost = strtok(line, " \t")) == NULL) {
	    amfree(line);
	    continue;
	}

	/* get the username.  If no user specified, then use the local user */
	if ((fileuser = strtok(NULL, " \t")) == NULL) {
	    fileuser = localuser;
	}

	hostmatch = (strcasecmp(filehost, host) == 0);
	usermatch = (strcasecmp(fileuser, remoteuser) == 0);
#if defined(SHOW_SECURITY_DETAIL)				/* { */
	dbprintf(("%s: comparing \"%s\" with\n", debug_prefix(NULL), filehost));
	dbprintf(("%s:           \"%s\" (%s)\n", host,
		  debug_prefix(NULL), hostmatch ? "match" : "no match"));
	dbprintf(("%s:       and \"%s\" with\n", fileuser, debug_prefix(NULL)));
	dbprintf(("%s:           \"%s\" (%s)\n", remoteuser,
		  debug_prefix(NULL), usermatch ? "match" : "no match"));
#endif								/* } */
	/* compare */
	if (hostmatch && usermatch) {
	    /* success */
	    found = 1;
	    break;
	}
    }
    if (! found) {
	result = vstralloc(ptmp, ": ",
			   "\"", host, " ", remoteuser, "\"",
			   " entry not found",
			   NULL);
    }

common_exit:

    afclose(fp);
    amfree(ptmp);
    amfree(line);
    amfree(localuser);

    return result;
}

/*
 * Create the server end of a stream.  For bsd, this means setup a tcp
 * socket for receiving a connection.
 */
static void *
bsd_stream_server(h)
    void *h;
{
    struct bsd_stream *bs;
#ifndef TEST							/* { */
    struct bsd_handle *bh = h;

    assert(bh != NULL);

    bs = alloc(sizeof(*bs));
    security_streaminit(&bs->secstr, &bsd_security_driver);
    bs->socket = stream_server(&bs->port, STREAM_BUFSIZE, STREAM_BUFSIZE);
    if (bs->socket < 0) {
	security_seterror(&bh->sech,
	    "can't create server stream: %s", strerror(errno));
	amfree(bs);
	return (NULL);
    }
    bs->fd = -1;
    bs->ev_read = NULL;
#endif /* !TEST */						/* } */
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
#ifndef TEST							/* { */
    struct bsd_stream *bs = s;

    assert(bs != NULL);
    assert(bs->socket != -1);
    assert(bs->fd < 0);

    bs->fd = stream_accept(bs->socket, 30, -1, -1);
    if (bs->fd < 0) {
	security_stream_seterror(&bs->secstr,
	    "can't accept new stream connection: %s", strerror(errno));
	return (-1);
    }
#endif /* !TEST */						/* } */
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
    struct bsd_stream *bs;
#ifndef TEST							/* { */
    struct bsd_handle *bh = h;
#ifdef DUMPER_SOCKET_BUFFERING
    int rcvbuf = sizeof(bs->databuf) * 2;
#endif

    assert(bh != NULL);

    if (id < 0) {
	security_seterror(&bh->sech,
	    "%d: invalid security stream id", id);
	return (NULL);
    }

    bs = alloc(sizeof(*bs));
    security_streaminit(&bs->secstr, &bsd_security_driver);
    bs->fd = stream_client(bh->hostname, id, STREAM_BUFSIZE, STREAM_BUFSIZE,
	&bs->port, 0);
    if (bs->fd < 0) {
	security_seterror(&bh->sech,
	    "can't connect stream to %s port %d: %s", bh->hostname,
	    id, strerror(errno));
	amfree(bs);
	return (NULL);
    }
    bs->socket = -1;	/* we're a client */
    bs->ev_read = NULL;
#ifdef DUMPER_SOCKET_BUFFERING
    setsockopt(bs->fd, SOL_SOCKET, SO_RCVBUF, (void *)&rcvbuf, sizeof(rcvbuf));
#endif
#endif /* !TEST */						/* } */
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
bsd_stream_write(s, buf, size)
    void *s;
    const void *buf;
    size_t size;
{
#ifndef TEST							/* { */
    struct bsd_stream *bs = s;

    assert(bs != NULL);

    if (fullwrite(bs->fd, buf, size) < 0) {
	security_stream_seterror(&bs->secstr,
	    "write error on stream %d: %s", bs->port, strerror(errno));
	return (-1);
    }
#endif /* !TEST */						/* } */
    return (0);
}

/*
 * Submit a request to read some data.  Calls back with the given function
 * and arg when completed.
 */
static void
bsd_stream_read(s, fn, arg)
    void *s, *arg;
    void (*fn) P((void *, void *, ssize_t));
{
    struct bsd_stream *bs = s;

    /*
     * Only one read request can be active per stream.
     */
    if (bs->ev_read != NULL)
	event_release(bs->ev_read);

    bs->ev_read = event_register(bs->fd, EV_READFD, stream_read_callback, bs);
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
    ssize_t n;

    assert(bs != NULL);

    /*
     * Remove the event first, in case they reschedule it in the callback.
     */
    bsd_stream_read_cancel(bs);
    n = read(bs->fd, bs->databuf, sizeof(bs->databuf));
    if (n < 0)
	security_stream_seterror(&bs->secstr, strerror(errno));
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

    snprintf(retbuf, sizeof(retbuf), "Amanda %d.%d %s HANDLE %d SEQ %d\n",
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
str2pkthdr()
{
    char *str;
    const char *tok;
    pkt_t *pkt;

    pkt = &netfd.pkt;

    assert(netfd.dgram.cur != NULL);
    str = stralloc(netfd.dgram.cur);

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
    netfd.handle = atoi(tok);

    /* Read in "SEQ" */
    if ((tok = strtok(NULL, " ")) == NULL || strcmp(tok, "SEQ") != 0)   
	goto parse_error;

    /* parse the sequence number */   
    if ((tok = strtok(NULL, "\n")) == NULL)
	goto parse_error;
    netfd.sequence = atoi(tok);

    /* Save the body, if any */       
    if ((tok = strtok(NULL, "")) != NULL)
	pkt_cat(pkt, tok);

    amfree(str);
    return (0);

parse_error:
#if 0 /* XXX we have no way of passing this back up */
    security_seterror(&bh->sech,
	"parse error in packet header : '%s'", origstr);
#endif
    amfree(str);
    return (-1);
}

#endif	/* BSD_SECURITY */					/* } */

#if defined(TEST)						/* { */

/*
 * The following dummy bind_portrange function is so we do not need to
 * drag in util.o just for the test program.
 */
int
bind_portrange(s, addrp, first_port, last_port)
    int s;
    struct sockaddr_in *addrp;
    int first_port, last_port;
{
    return 0;
}

/*
 * The following are so we can include security.o but not all the rest
 * of the security modules.
 */
const security_driver_t krb4_security_driver = {};
const security_driver_t krb5_security_driver = {};
const security_driver_t rsh_security_driver = {};

/*
 * This function will be called to accept the connection and is used
 * to report success or failure.
 */
static void fake_accept_function(handle, pkt)
    security_handle_t *handle;
    pkt_t *pkt;
{
    if (pkt == NULL) {
	fputs(handle->error, stdout);
	fputc('\n', stdout);
    } else {
	fputs("access is allowed\n", stdout);
    }
}

int
main (argc, argv)
{
    char *remoteuser;
    char *remotehost;
    struct hostent *hp;
    struct bsd_handle *bh;
    void *save_cur;
    struct passwd *pwent;

    /*
     * The following is stolen from amandad to emulate what it would
     * do on startup.
     */
    if(client_uid == (uid_t) -1 && (pwent = getpwnam(CLIENT_LOGIN)) != NULL) {
	client_uid = pwent->pw_uid;
	client_gid = pwent->pw_gid;
	endpwent();
    }

#ifdef FORCE_USERID
    /* we'd rather not run as root */
    if (geteuid() == 0) {
	if(client_uid == (uid_t) -1) {
	    error("error [cannot find user %s in passwd file]\n", CLIENT_LOGIN);
	}
	initgroups(CLIENT_LOGIN, client_gid);
	setgid(client_gid);
	setegid(client_gid);
	seteuid(client_uid);
    }
#endif	/* FORCE_USERID */

    if (isatty(0)) {
	fputs("Remote user: ", stdout);
	fflush(stdout);
    }
    if ((remoteuser = agets(stdin)) == NULL) {
	return 0;
    }

    if (isatty(0)) {
	fputs("Remote host: ", stdout);
	fflush(stdout);
    }
    if ((remotehost = agets(stdin)) == NULL) {
	return 0;
    }

    set_pname("security");
    startclock();

    if ((hp = gethostbyname(remotehost)) == NULL) {
	fprintf(stderr, "cannot look up remote host %s\n", remotehost);
	return 1;
    }
    memcpy((char *)&netfd.peer.sin_addr,
	   (char *)hp->h_addr,
	   sizeof(hp->h_addr));
    /*
     * Fake that it is coming from a reserved port.
     */
    netfd.peer.sin_port = htons(IPPORT_RESERVED - 1);

    bh = alloc(sizeof(*bh));
    netfd.pkt.type = P_REQ;
    dgram_zero(&netfd.dgram);
    save_cur = netfd.dgram.cur;				/* cheating */
    dgram_cat(&netfd.dgram, "%s", pkthdr2str(bh, &netfd.pkt));
    dgram_cat(&netfd.dgram, "SECURITY USER %s\n", remoteuser);
    netfd.dgram.cur = save_cur;				/* cheating */

    accept_fn = fake_accept_function;
    netfd_read_callback(NULL);

    return 0;
}

#endif								/* } */
