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
 * $Id: ssh-security.c,v 1.8.2.1 2006/04/11 11:11:16 martinea Exp $
 *
 * ssh-security.c - security and transport over ssh or a ssh-like command.
 *
 * XXX still need to check for initial keyword on connect so we can skip
 * over shell garbage and other stuff that ssh might want to spew out.
 */

#include "amanda.h"
#include "event.h"
#include "packet.h"
#include "queue.h"
#include "security.h"
#include "stream.h"
#include "version.h"

#ifdef SSH_SECURITY

/*#define	SSH_DEBUG*/

#ifdef SSH_DEBUG
#define	sshprintf(x)	dbprintf(x)
#else
#define	sshprintf(x)
#endif

/*
 * Path to the ssh binary.  This should be configurable.
 */
#define	SSH_PATH	"/usr/bin/ssh"

/*
 * Arguments to ssh.  This should also be configurable
 */
#define	SSH_ARGS	"-x", "-l", CLIENT_LOGIN

/*
 * Number of seconds ssh has to start up
 */
#define	CONNECT_TIMEOUT	20

/*
 * Magic values for ssh_conn->handle
 */
#define	H_TAKEN	-1		/* ssh_conn->tok was already read */
#define	H_EOF	-2		/* this connection has been shut down */

/*
 * This is a ssh connection to a host.  We should only have
 * one connection per host.
 */
struct ssh_conn {
    int read, write;				/* pipes to ssh */
    pid_t pid;					/* pid of ssh process */
    char pkt[NETWORK_BLOCK_BYTES];		/* last pkt read */
    unsigned long pktlen;			/* len of above */
    struct {					/* buffer read() calls */
	char buf[STREAM_BUFSIZE];		/* buffer */
	size_t left;			/* unread data */
	ssize_t size;			/* size of last read */
    } readbuf;
    event_handle_t *ev_read;			/* read (EV_READFD) handle */
    int ev_read_refcnt;				/* number of readers */
    char hostname[MAX_HOSTNAME_LENGTH+1];	/* host we're talking to */
    char *errmsg;				/* error passed up */
    int refcnt;					/* number of handles using */
    int handle;					/* last proto handle read */
    TAILQ_ENTRY(ssh_conn) tq;			/* queue handle */
};


struct ssh_stream;

/*
 * This is the private handle data.
 */
struct ssh_handle {
    security_handle_t sech;		/* MUST be first */
    char *hostname;			/* ptr to rc->hostname */
    struct ssh_stream *rs;		/* virtual stream we xmit over */

    union {
	void (*recvpkt) P((void *, pkt_t *, security_status_t));
					/* func to call when packet recvd */
	void (*connect) P((void *, security_handle_t *, security_status_t));
					/* func to call when connected */
    } fn;
    void *arg;				/* argument to pass function */
    event_handle_t *ev_timeout;		/* timeout handle for recv */
};

/*
 * This is the internal security_stream data for ssh.
 */
struct ssh_stream {
    security_stream_t secstr;		/* MUST be first */
    struct ssh_conn *rc;		/* physical connection */
    int handle;				/* protocol handle */
    event_handle_t *ev_read;		/* read (EV_WAIT) event handle */
    void (*fn) P((void *, void *, ssize_t));	/* read event fn */
    void *arg;				/* arg for previous */
};

/*
 * Interface functions
 */
static int ssh_sendpkt P((void *, pkt_t *));
static int ssh_stream_accept P((void *));
static int ssh_stream_auth P((void *));
static int ssh_stream_id P((void *));
static int ssh_stream_write P((void *, const void *, size_t));
static void *ssh_stream_client P((void *, int));
static void *ssh_stream_server P((void *));
static void ssh_accept P((int, int,
    void (*)(security_handle_t *, pkt_t *)));
static void ssh_close P((void *));
static void ssh_connect P((const char *,
    char *(*)(char *, void *), 
    void (*)(void *, security_handle_t *, security_status_t), void *));
static void ssh_recvpkt P((void *,
    void (*)(void *, pkt_t *, security_status_t), void *, int));
static void ssh_recvpkt_cancel P((void *));
static void ssh_stream_close P((void *));
static void ssh_stream_read P((void *, void (*)(void *, void *, ssize_t),
    void *));
static void ssh_stream_read_cancel P((void *));

/*
 * This is our interface to the outside world.
 */
const security_driver_t ssh_security_driver = {
    "SSH",
    ssh_connect,
    ssh_accept,
    ssh_close,
    ssh_sendpkt,
    ssh_recvpkt,
    ssh_recvpkt_cancel,
    ssh_stream_server,
    ssh_stream_accept,
    ssh_stream_client,
    ssh_stream_close,
    ssh_stream_auth,
    ssh_stream_id,
    ssh_stream_write,
    ssh_stream_read,
    ssh_stream_read_cancel,
};

/*
 * This is a queue of open connections
 */
static struct {
    TAILQ_HEAD(, ssh_conn) tailq;
    int qlength;
} connq = {
    TAILQ_HEAD_INITIALIZER(connq.tailq), 0
};
#define	connq_first()		TAILQ_FIRST(&connq.tailq)
#define	connq_next(rc)		TAILQ_NEXT(rc, tq)
#define	connq_append(rc)	do {					\
    TAILQ_INSERT_TAIL(&connq.tailq, rc, tq);				\
    connq.qlength++;							\
} while (0)
#define	connq_remove(rc)	do {					\
    assert(connq.qlength > 0);						\
    TAILQ_REMOVE(&connq.tailq, rc, tq);					\
    connq.qlength--;							\
} while (0)

static int newhandle = 1;

/*
 * This is a function that should be called if a new security_handle_t is
 * created.  If NULL, no new handles are created.
 * It is passed the new handle and the received pkt
 */
static void (*accept_fn) P((security_handle_t *, pkt_t *));

/*
 * Local functions
 */
static void connect_callback P((void *));
static void connect_timeout P((void *));
static int send_token P((struct ssh_conn *, int, const void *, size_t));
static int recv_token P((struct ssh_conn *, int));
static void recvpkt_callback P((void *, void *, ssize_t));
static void recvpkt_timeout P((void *));
static void stream_read_callback P((void *));

static int runssh P((struct ssh_conn *));
static struct ssh_conn *conn_get P((const char *));
static void conn_put P((struct ssh_conn *));
static void conn_read P((struct ssh_conn *));
static void conn_read_cancel P((struct ssh_conn *));
static void conn_read_callback P((void *));
static int net_writev P((int, struct iovec *, int));
static ssize_t net_read P((struct ssh_conn *, void *, size_t, int));
static int net_read_fillbuf P((struct ssh_conn *, int, int));
static void parse_pkt P((pkt_t *, const void *, size_t));


/*
 * ssh version of a security handle allocator.  Logically sets
 * up a network "connection".
 */
static void
ssh_connect(hostname, conf_fn, fn, arg)
    const char *hostname;
    char *(*conf_fn) P((char *, void *));
    void (*fn) P((void *, security_handle_t *, security_status_t));
    void *arg;
{
    struct ssh_handle *rh;
    struct hostent *he;

    assert(fn != NULL);
    assert(hostname != NULL);

    sshprintf(("%s: ssh: ssh_connect: %s\n", debug_prefix_time(NULL), hostname));

    rh = alloc(sizeof(*rh));
    security_handleinit(&rh->sech, &ssh_security_driver);
    rh->hostname = NULL;
    rh->rs = NULL;
    rh->ev_timeout = NULL;

    if ((he = gethostbyname(hostname)) == NULL) {
	security_seterror(&rh->sech,
	    "%s: could not resolve hostname", hostname);
	(*fn)(arg, &rh->sech, S_ERROR);
	return;
    }
    rh->hostname = he->h_name;	/* will be replaced */
    rh->rs = ssh_stream_client(rh, newhandle++);

    if (rh->rs == NULL)
	goto error;

    rh->hostname = rh->rs->rc->hostname;

    if (rh->rs->rc->pid < 0) {
	/*
	 * We need to open a new connection.
	 *
	 * XXX need to eventually limit number of outgoing connections here.
	 */
	if (runssh(rh->rs->rc) < 0) {
	    security_seterror(&rh->sech,
		"can't connect to %s: %s", hostname, rh->rs->rc->errmsg);
	    goto error;
	}
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
	connect_callback, rh);
    rh->ev_timeout = event_register(CONNECT_TIMEOUT, EV_TIME,
	connect_timeout, rh);

    return;

error:
    (*fn)(arg, &rh->sech, S_ERROR);
}

/*
 * Called when a ssh connection is finished connecting and is ready
 * to be authenticated.
 */
static void
connect_callback(cookie)
    void *cookie;
{
    struct ssh_handle *rh = cookie;

    event_release(rh->rs->ev_read);
    rh->rs->ev_read = NULL;
    event_release(rh->ev_timeout);
    rh->ev_timeout = NULL;

    (*rh->fn.connect)(rh->arg, &rh->sech, S_OK);
}

/*
 * Called if a connection times out before completion.
 */
static void
connect_timeout(cookie)
    void *cookie;
{
    struct ssh_handle *rh = cookie;

    event_release(rh->rs->ev_read);
    rh->rs->ev_read = NULL;
    event_release(rh->ev_timeout);
    rh->ev_timeout = NULL;

    (*rh->fn.connect)(rh->arg, &rh->sech, S_TIMEOUT);
}

/*
 * Setup to handle new incoming connections
 */
static void
ssh_accept(in, out, fn)
    int in, out;
    void (*fn) P((security_handle_t *, pkt_t *));
{
    struct ssh_conn *rc;

    rc = conn_get("unknown");
    rc->read = in;
    rc->write = out;
    accept_fn = fn;
    conn_read(rc);
}

/*
 * Locate an existing connection to the given host, or create a new,
 * unconnected entry if none exists.  The caller is expected to check
 * for the lack of a connection (rc->read == -1) and set one up.
 */
static struct ssh_conn *
conn_get(hostname)
    const char *hostname;
{
    struct ssh_conn *rc;

    sshprintf(("%s: ssh: conn_get: %s\n", debug_prefix_time(NULL), hostname));

    for (rc = connq_first(); rc != NULL; rc = connq_next(rc)) {
	if (strcasecmp(hostname, rc->hostname) == 0)
	    break;
    }

    if (rc != NULL) {
	rc->refcnt++;
	sshprintf(("%s: ssh: conn_get: exists, refcnt to %s is now %d\n", debug_prefix_time(NULL),
	    rc->hostname, rc->refcnt));
	return (rc);
    }

    sshprintf(("%s: ssh: conn_get: creating new handle\n", debug_prefix_time(NULL)));
    /*
     * We can't be creating a new handle if we are the client
     */
    assert(accept_fn == NULL);
    rc = alloc(sizeof(*rc));
    rc->read = rc->write = -1;
    rc->pid = -1;
    rc->readbuf.left = 0;
    rc->readbuf.size = 0;
    rc->ev_read = NULL;
    strncpy(rc->hostname, hostname, sizeof(rc->hostname) - 1);
    rc->hostname[sizeof(rc->hostname) - 1] = '\0';
    rc->errmsg = NULL;
    rc->refcnt = 1;
    rc->handle = -1;
    connq_append(rc);
    return (rc);
}

/*
 * Delete a reference to a connection, and close it if it is the last
 * reference.
 */
static void
conn_put(rc)
    struct ssh_conn *rc;
{
    amwait_t status;

    assert(rc->refcnt > 0);
    --rc->refcnt;
    sshprintf(("%s: ssh: conn_put: decrementing refcnt for %s to %d\n", debug_prefix_time(NULL),
	rc->hostname, rc->refcnt));
    if (rc->refcnt > 0) {
	return;
    }
    sshprintf(("%s: ssh: conn_put: closing connection to %s\n", debug_prefix_time(NULL), rc->hostname));
    if (rc->read != -1)
	aclose(rc->read);
    if (rc->write != -1)
	aclose(rc->write);
    if (rc->pid != -1) {
	waitpid(rc->pid, &status, WNOHANG);
    }
    if (rc->ev_read != NULL)
	event_release(rc->ev_read);
    if (rc->errmsg != NULL)
	amfree(rc->errmsg);
    connq_remove(rc);
    amfree(rc);
}

/*
 * Turn on read events for a conn.  Or, increase a ev_read_refcnt if we are
 * already receiving read events.
 */
static void
conn_read(rc)
    struct ssh_conn *rc;
{

    if (rc->ev_read != NULL) {
	rc->ev_read_refcnt++;
	sshprintf(("%s: ssh: conn_read: incremented ev_read_refcnt to %d for %s\n", debug_prefix_time(NULL),
	    rc->ev_read_refcnt, rc->hostname));
	return;
    }
    sshprintf(("%s: ssh: conn_read registering event handler for %s\n", debug_prefix_time(NULL),
	rc->hostname));
    rc->ev_read = event_register(rc->read, EV_READFD, conn_read_callback, rc);
    rc->ev_read_refcnt = 1;
}

static void
conn_read_cancel(rc)
    struct ssh_conn *rc;
{

    --rc->ev_read_refcnt;
    sshprintf(("%s: ssh: conn_read_cancel: decremented ev_read_refcnt to %d for %s\n", debug_prefix_time(NULL),
	rc->ev_read_refcnt, rc->hostname));
    if(rc->ev_read_refcnt > 0) {
	return;
    }
    sshprintf(("%s: ssh: conn_read_cancel: releasing event handler for %s\n", debug_prefix_time(NULL),
	rc->hostname));
    event_release(rc->ev_read);
    rc->ev_read = NULL;
}

/*
 * frees a handle allocated by the above
 */
static void
ssh_close(inst)
    void *inst;
{
    struct ssh_handle *rh = inst;

    assert(rh != NULL);

    sshprintf(("%s: ssh: closing handle to %s\n", debug_prefix_time(NULL), rh->hostname));

    if (rh->rs != NULL) {
	/* This may be null if we get here on an error */
	ssh_recvpkt_cancel(rh);
	security_stream_close(&rh->rs->secstr);
    }
    /* keep us from getting here again */
    rh->sech.driver = NULL;
    amfree(rh);
}

/*
 * Forks a ssh to the host listed in rc->hostname
 * Returns negative on error, with an errmsg in rc->errmsg.
 */
static int
runssh(rc)
    struct ssh_conn *rc;
{
    int rpipe[2], wpipe[2];
    char *amandad_path;

    if (pipe(rpipe) < 0 || pipe(wpipe) < 0) {
	rc->errmsg = newvstralloc("pipe: ", strerror(errno), NULL);
	return (-1);
    }
    switch (rc->pid = fork()) {
    case -1:
	rc->errmsg = newvstralloc("fork: ", strerror(errno), NULL);
	aclose(rpipe[0]);
	aclose(rpipe[1]);
	aclose(wpipe[0]);
	aclose(wpipe[1]);
	return (-1);
    case 0:
	dup2(wpipe[0], 0);
	dup2(rpipe[1], 1);
	dup2(rpipe[1], 2);
	break;
    default:
	rc->read = rpipe[0];
	aclose(rpipe[1]);
	rc->write = wpipe[1];
	aclose(wpipe[0]);
	return (0);
    }

    safe_fd(-1, 0);

    amandad_path = vstralloc(libexecdir, "/", "amandad", versionsuffix(),
	NULL);
    execlp(SSH_PATH, SSH_PATH, SSH_ARGS, rc->hostname, amandad_path,
	"-auth=ssh", NULL);
    error("error: couldn't exec %s: %s", SSH_PATH, strerror(errno));

    /* should nerver go here, shut up compiler warning */
    return(-1);
}

/*
 * Transmit a packet.
 */
static int
ssh_sendpkt(cookie, pkt)
    void *cookie;
    pkt_t *pkt;
{
    char buf[sizeof(pkt_t)];
    struct ssh_handle *rh = cookie;
    size_t len;

    assert(rh != NULL);
    assert(pkt != NULL);

    sshprintf(("%s: ssh: sendpkt: enter\n", debug_prefix_time(NULL)));

    len = strlen(pkt->body) + 2;
    buf[0] = (char)pkt->type;
    strcpy(&buf[1], pkt->body);

    sshprintf(("%s: ssh: sendpkt: %s (%d) pkt_t (len %d) contains:\n\n\"%s\"\n\n", debug_prefix_time(NULL),
	pkt_type2str(pkt->type), pkt->type, strlen(pkt->body), pkt->body));

    if (ssh_stream_write(rh->rs, buf, len) < 0) {
	security_seterror(&rh->sech, security_stream_geterror(&rh->rs->secstr));
	return (-1);
    }
    return (0);
}

/*
 * Set up to receive a packet asyncronously, and call back when
 * it has been read.
 */
static void
ssh_recvpkt(cookie, fn, arg, timeout)
    void *cookie, *arg;
    void (*fn) P((void *, pkt_t *, security_status_t));
    int timeout;
{
    struct ssh_handle *rh = cookie;

    assert(rh != NULL);

    sshprintf(("%s: ssh: recvpkt registered for %s\n", debug_prefix_time(NULL), rh->hostname));

    /*
     * Reset any pending timeout on this handle
     */
    if (rh->ev_timeout != NULL)
	event_release(rh->ev_timeout);

    /*
     * Negative timeouts mean no timeout
     */
    if (timeout < 0)
	rh->ev_timeout = NULL;
    else
	rh->ev_timeout = event_register(timeout, EV_TIME, recvpkt_timeout, rh);

    rh->fn.recvpkt = fn;
    rh->arg = arg;
    ssh_stream_read(rh->rs, recvpkt_callback, rh);
}

/*
 * Remove a async receive request from the queue
 */
static void
ssh_recvpkt_cancel(cookie)
    void *cookie;
{
    struct ssh_handle *rh = cookie;

    sshprintf(("%s: ssh: cancelling recvpkt for %s\n", debug_prefix_time(NULL), rh->hostname));

    assert(rh != NULL);

    ssh_stream_read_cancel(rh->rs);
    if (rh->ev_timeout != NULL) {
	event_release(rh->ev_timeout);
	rh->ev_timeout = NULL;
    }
}

/*
 * This is called when a handle is woken up because data read off of the
 * net is for it.
 */
static void
recvpkt_callback(cookie, buf, bufsize)
    void *cookie, *buf;
    ssize_t bufsize;
{
    pkt_t pkt;
    struct ssh_handle *rh = cookie;

    assert(rh != NULL);

    /*
     * We need to cancel the recvpkt request before calling
     * the callback because the callback may reschedule us.
     */
    ssh_recvpkt_cancel(rh);

    switch (bufsize) {
    case 0:
	security_seterror(&rh->sech,
	    "EOF on read from %s", rh->hostname);
	(*rh->fn.recvpkt)(rh->arg, NULL, S_ERROR);
	return;
    case -1:
	security_seterror(&rh->sech, security_stream_geterror(&rh->rs->secstr));
	(*rh->fn.recvpkt)(rh->arg, NULL, S_ERROR);
	return;
    default:
	break;
    }

    parse_pkt(&pkt, buf, bufsize);
    sshprintf(("%s: ssh: received %s packet (%d) from %s, contains:\n\n\"%s\"\n\n", debug_prefix_time(NULL),
	pkt_type2str(pkt.type), pkt.type, rh->hostname, pkt.body));
    (*rh->fn.recvpkt)(rh->arg, &pkt, S_OK);
}

/*
 * This is called when a handle times out before receiving a packet.
 */
static void
recvpkt_timeout(cookie)
    void *cookie;
{
    struct ssh_handle *rh = cookie;

    assert(rh != NULL);

    sshprintf(("%s: ssh: recvpkt timeout for %s\n", debug_prefix_time(NULL), rh->hostname));

    ssh_recvpkt_cancel(rh);
    (*rh->fn.recvpkt)(rh->arg, NULL, S_TIMEOUT);
}

/*
 * Create the server end of a stream.  For ssh, this means setup a stream
 * object and allocate a new handle for it.
 */
static void *
ssh_stream_server(h)
    void *h;
{
    struct ssh_handle *rh = h;
    struct ssh_stream *rs;

    assert(rh != NULL);

    rs = alloc(sizeof(*rs));
    security_streaminit(&rs->secstr, &ssh_security_driver);
    rs->rc = conn_get(rh->hostname);
    /*
     * Stream should already be setup!
     */
    if (rs->rc->read < 0) {
	conn_put(rs->rc);
	amfree(rs);
	security_seterror(&rh->sech, "lost connection to %s", rh->hostname);
	return (NULL);
    }
    rh->hostname = rs->rc->hostname;
    /*
     * so as not to conflict with the amanda server's handle numbers,
     * we start at 5000 and work down
     */
    rs->handle = 5000 - newhandle++;
    rs->ev_read = NULL;
    sshprintf(("%s: ssh: stream_server: created stream %d\n", debug_prefix_time(NULL), rs->handle));
    return (rs);
}

/*
 * Accept an incoming connection on a stream_server socket
 * Nothing needed for ssh.
 */
static int
ssh_stream_accept(s)
    void *s;
{

    return (0);
}

/*
 * Return a connected stream.  For ssh, this means setup a stream
 * with the supplied handle.
 */
static void *
ssh_stream_client(h, id)
    void *h;
    int id;
{
    struct ssh_handle *rh = h;
    struct ssh_stream *rs;

    assert(rh != NULL);

    if (id <= 0) {
	security_seterror(&rh->sech,
	    "%d: invalid security stream id", id);
	return (NULL);
    }

    rs = alloc(sizeof(*rs));
    security_streaminit(&rs->secstr, &ssh_security_driver);
    rs->handle = id;
    rs->ev_read = NULL;
    rs->rc = conn_get(rh->hostname);

    sshprintf(("%s: ssh: stream_client: connected to stream %d\n", debug_prefix_time(NULL), id));

    return (rs);
}

/*
 * Close and unallocate resources for a stream.
 */
static void
ssh_stream_close(s)
    void *s;
{
    struct ssh_stream *rs = s;

    assert(rs != NULL);

    sshprintf(("%s: ssh: stream_close: closing stream %d\n", debug_prefix_time(NULL), rs->handle));

    ssh_stream_read_cancel(rs);
    conn_put(rs->rc);
    amfree(rs);
}

/*
 * Authenticate a stream
 * Nothing needed for ssh.  The connection is authenticated by sshd
 * on startup.
 */
static int
ssh_stream_auth(s)
    void *s;
{

    return (0);
}

/*
 * Returns the stream id for this stream.  This is just the local
 * port.
 */
static int
ssh_stream_id(s)
    void *s;
{
    struct ssh_stream *rs = s;

    assert(rs != NULL);

    return (rs->handle);
}

/*
 * Write a chunk of data to a stream.  Blocks until completion.
 */
static int
ssh_stream_write(s, buf, size)
    void *s;
    const void *buf;
    size_t size;
{
    struct ssh_stream *rs = s;

    assert(rs != NULL);

    sshprintf(("%s: ssh: stream_write: writing %d bytes to %s:%d\n", debug_prefix_time(NULL), size,
	rs->rc->hostname, rs->handle));

    if (send_token(rs->rc, rs->handle, buf, size) < 0) {
	security_stream_seterror(&rs->secstr, rs->rc->errmsg);
	return (-1);
    }
    return (0);
}

/*
 * Submit a request to read some data.  Calls back with the given
 * function and arg when completed.
 */
static void
ssh_stream_read(s, fn, arg)
    void *s, *arg;
    void (*fn) P((void *, void *, ssize_t));
{
    struct ssh_stream *rs = s;

    assert(rs != NULL);

    /*
     * Only one read request can be active per stream.
     */
    if (rs->ev_read == NULL) {
	rs->ev_read = event_register((event_id_t)rs->rc, EV_WAIT,
	    stream_read_callback, rs);
	conn_read(rs->rc);
    }
    rs->fn = fn;
    rs->arg = arg;
}

/*
 * Cancel a previous stream read request.  It's ok if we didn't have a read
 * scheduled.
 */
static void
ssh_stream_read_cancel(s)
    void *s;
{
    struct ssh_stream *rs = s;

    assert(rs != NULL);

    if (rs->ev_read != NULL) {
	event_release(rs->ev_read);
	rs->ev_read = NULL;
	conn_read_cancel(rs->rc);
    }
}

/*
 * Callback for ssh_stream_read
 */
static void
stream_read_callback(arg)
    void *arg;
{
    struct ssh_stream *rs = arg;
    assert(rs != NULL);

    sshprintf(("%s: ssh: stream_read_callback: handle %d\n", debug_prefix_time(NULL), rs->handle));

    /*
     * Make sure this was for us.  If it was, then blow away the handle
     * so it doesn't get claimed twice.  Otherwise, leave it alone.
     *
     * If the handle is EOF, pass that up to our callback.
     */
    if (rs->rc->handle == rs->handle) {
	sshprintf(("%s: ssh: stream_read_callback: it was for us\n", debug_prefix_time(NULL)));
	rs->rc->handle = H_TAKEN;
    } else if (rs->rc->handle != H_EOF) {
	sshprintf(("%s: ssh: stream_read_callback: not for us\n", debug_prefix_time(NULL)));
	return;
    }

    /*
     * Remove the event first, and then call the callback.
     * We remove it first because we don't want to get in their
     * way if they reschedule it.
     */
    ssh_stream_read_cancel(rs);

    if (rs->rc->pktlen == 0) {
	sshprintf(("%s: ssh: stream_read_callback: EOF\n", debug_prefix_time(NULL)));
	(*rs->fn)(rs->arg, NULL, 0);
	return;
    }
    sshprintf(("%s: ssh: stream_read_callback: read %ld bytes from %s:%d\n", debug_prefix_time(NULL),
	rs->rc->pktlen, rs->rc->hostname, rs->handle));
    (*rs->fn)(rs->arg, rs->rc->pkt, rs->rc->pktlen);
}

/*
 * The callback for the netfd for the event handler
 * Determines if this packet is for this security handle,
 * and does the real callback if so.
 */
static void
conn_read_callback(cookie)
    void *cookie;
{
    struct ssh_conn *rc = cookie;
    struct ssh_handle *rh;
    pkt_t pkt;
    int rval;

    assert(cookie != NULL);

    sshprintf(("%s: ssh: conn_read_callback\n",debug_prefix_time(NULL)));

    /* Read the data off the wire.  If we get errors, shut down. */
    rval = recv_token(rc, 60);
    sshprintf(("%s: ssh: conn_read_callback: recv_token returned %d\n", debug_prefix_time(NULL), rval));
    if (rval <= 0) {
	rc->pktlen = 0;
	rc->handle = H_EOF;
	rval = event_wakeup((event_id_t)rc);
	sshprintf(("%s: ssh: conn_read_callback: event_wakeup return %d\n", debug_prefix_time(NULL), rval));
	/* delete our 'accept' reference */
	if (accept_fn != NULL)
	    conn_put(rc);
	accept_fn = NULL;
	return;
    }

    /* If there are events waiting on this handle, we're done */
    rval = event_wakeup((event_id_t)rc);
    sshprintf(("%s: ssh: conn_read_callback: event_wakeup return %d\n", debug_prefix_time(NULL), rval));
    if (rval > 0)
	return;

    /* If there is no accept fn registered, then drop the packet */
    if (accept_fn == NULL)
	return;

    rh = alloc(sizeof(*rh));
    security_handleinit(&rh->sech, &ssh_security_driver);
    rh->hostname = rc->hostname;
    rh->rs = ssh_stream_client(rh, rc->handle);
    rh->ev_timeout = NULL;

    sshprintf(("%s: ssh: new connection\n", debug_prefix_time(NULL)));
    parse_pkt(&pkt, rc->pkt, rc->pktlen);
    sshprintf(("%s: ssh: calling accept_fn\n", debug_prefix_time(NULL)));
    (*accept_fn)(&rh->sech, &pkt);
}

static void
parse_pkt(pkt, buf, bufsize)
    pkt_t *pkt;
    const void *buf;
    size_t bufsize;
{
    const unsigned char *bufp = buf;

    sshprintf(("%s: ssh: parse_pkt: parsing buffer of %d bytes\n", debug_prefix_time(NULL), bufsize));

    pkt->type = (pktype_t)*bufp++;
    bufsize--;

    if (bufsize == 0) {
	pkt->body[0] = '\0';
    } else {
	if (bufsize > sizeof(pkt->body) - 1)
	    bufsize = sizeof(pkt->body) - 1;
	memcpy(pkt->body, bufp, bufsize);
	pkt->body[sizeof(pkt->body) - 1] = '\0';
    }

    sshprintf(("%s: ssh: parse_pkt: %s (%d): \"%s\"\n", debug_prefix_time(NULL), pkt_type2str(pkt->type),
	pkt->type, pkt->body));
}


/*
 * Transmits a chunk of data over a ssh_handle, adding
 * the necessary headers to allow the remote end to decode it.
 */
static int
send_token(rc, handle, buf, len)
    struct ssh_conn *rc;
    int handle;
    const void *buf;
    size_t len;
{
    unsigned int netlength, nethandle;
    struct iovec iov[3];

    sshprintf(("%s: ssh: send_token: handle %d writing %d bytes to %s\n", debug_prefix_time(NULL), handle, len,
	rc->hostname));

    assert(sizeof(netlength) == 4);

    /*
     * Format is:
     *   32 bit length (network byte order)
     *   32 bit handle (network byte order)
     *   data
     */
    netlength = htonl(len);
    iov[0].iov_base = (void *)&netlength;
    iov[0].iov_len = sizeof(netlength);

    nethandle = htonl(handle);
    iov[1].iov_base = (void *)&nethandle;
    iov[1].iov_len = sizeof(nethandle);

    iov[2].iov_base = (void *)buf;
    iov[2].iov_len = len;

    if (net_writev(rc->write, iov, 3) < 0) {
	rc->errmsg = newvstralloc(rc->errmsg, "ssh write error to ",
	    rc->hostname, ": ", strerror(errno), NULL);
	return (-1);
    }
    return (0);
}

static int
recv_token(rc, timeout)
    struct ssh_conn *rc;
    int timeout;
{
    unsigned int netint;

    assert(sizeof(netint) == 4);

    assert(rc->read >= 0);

    sshprintf(("%s: ssh: recv_token: reading from %s\n", debug_prefix_time(NULL), rc->hostname));

    switch (net_read(rc, &netint, sizeof(netint), timeout)) {
    case -1:
	rc->errmsg = newvstralloc(rc->errmsg, "recv error: ", strerror(errno),
	    NULL);
	sshprintf(("%s: ssh: recv_token: A return(-1)\n", debug_prefix_time(NULL)));
	return (-1);
    case 0:
	rc->pktlen = 0;
	sshprintf(("%s: ssh: recv_token: A return(0)\n", debug_prefix_time(NULL)));
	return (0);
    default:
	break;
    }
    rc->pktlen = ntohl(netint);
    if (rc->pktlen > sizeof(rc->pkt)) {
	rc->errmsg = newstralloc(rc->errmsg, "recv error: huge packet");
	sshprintf(("%s: ssh: recv_token: B return(-1)\n", debug_prefix_time(NULL)));
	return (-1);
    }

    switch (net_read(rc, &netint, sizeof(netint), timeout)) {
    case -1:
	rc->errmsg = newvstralloc(rc->errmsg, "recv error: ", strerror(errno),
	    NULL);
	sshprintf(("%s: ssh: recv_token: C return(-1)\n", debug_prefix_time(NULL)));
	return (-1);
    case 0:
	rc->pktlen = 0;
	sshprintf(("%s: ssh: recv_token: D return(0)\n", debug_prefix_time(NULL)));
	return (0);
    default:
	break;
    }
    rc->handle = ntohl(netint);

    switch (net_read(rc, rc->pkt, rc->pktlen, timeout)) {
    case -1:
	rc->errmsg = newvstralloc(rc->errmsg, "recv error: ", strerror(errno),
	    NULL);
	sshprintf(("%s: ssh: recv_token: E return(-1)\n", debug_prefix_time(NULL)));
	return (-1);
    case 0:
	rc->pktlen = 0;
	break;
    default:
	break;
    }

    sshprintf(("%s: ssh: recv_token: read %ld bytes from %s\n", debug_prefix_time(NULL), rc->pktlen,
	rc->hostname));
    sshprintf(("%s: ssh: recv_token: end %d\n", debug_prefix_time(NULL),rc->pktlen));
    return (rc->pktlen);
}

/*
 * Writes out the entire iovec
 */
static int
net_writev(fd, iov, iovcnt)
    int fd, iovcnt;
    struct iovec *iov;
{
    int delta, n, total;

    assert(iov != NULL);

    total = 0;
    while (iovcnt > 0) {
	/*
	 * Write the iovec
	 */
	total += n = writev(fd, iov, iovcnt);
	if (n < 0)
	    return (-1);
	if (n == 0) {
	    errno = EIO;
	    return (-1);
	}
	/*
	 * Iterate through each iov.  Figure out what we still need
	 * to write out.
	 */
	for (; n > 0; iovcnt--, iov++) {
	    /* 'delta' is the bytes written from this iovec */
	    delta = n < iov->iov_len ? n : iov->iov_len;
	    /* subtract from the total num bytes written */
	    n -= delta;
	    assert(n >= 0);
	    /* subtract from this iovec */
	    iov->iov_len -= delta;
	    iov->iov_base = (char *)iov->iov_base + delta;
	    /* if this iovec isn't empty, run the writev again */
	    if (iov->iov_len > 0)
		break;
	}
    }
    return (total);
}

/*
 * Like read(), but waits until the entire buffer has been filled.
 */
static ssize_t
net_read(rc, vbuf, origsize, timeout)
    struct ssh_conn *rc;
    void *vbuf;
    size_t origsize;
    int timeout;
{
    char *buf = vbuf, *off;	/* ptr arith */
    int nread;
    size_t size = origsize;

    sshprintf(("%s: ssh: net_read: begin %d\n", debug_prefix_time(NULL), origsize));
    while (size > 0) {
	sshprintf(("%s: ssh: net_read: while %d\n", debug_prefix_time(NULL), size));
	if (rc->readbuf.left == 0) {
	    if (net_read_fillbuf(rc, timeout, size) < 0) {
    		sshprintf(("%s: ssh: net_read: end retrun(-1)\n", debug_prefix_time(NULL)));
		return (-1);
	    }
	    if (rc->readbuf.size == 0) {
    		sshprintf(("%s: ssh: net_read: end retrun(0)\n", debug_prefix_time(NULL)));
		return (0);
	    }
	}
	nread = min(rc->readbuf.left, size);
	off = rc->readbuf.buf + rc->readbuf.size - rc->readbuf.left;
	memcpy(buf, off, nread);

	buf += nread;
	size -= nread;
	rc->readbuf.left -= nread;
    }
    sshprintf(("%s: ssh: net_read: end %d\n", debug_prefix_time(NULL), origsize));
    return ((ssize_t)origsize);
}

/*
 * net_read likes to do a lot of little reads.  Buffer it.
 */
static int
net_read_fillbuf(rc, timeout, size)
    struct ssh_conn *rc;
    int timeout;
    int size;
{
    fd_set readfds;
    struct timeval tv;
    if(size > sizeof(rc->readbuf.buf)) size = sizeof(rc->readbuf.buf);

    sshprintf(("%s: ssh: net_read_fillbuf: begin\n", debug_prefix_time(NULL)));
    FD_ZERO(&readfds);
    FD_SET(rc->read, &readfds);
    tv.tv_sec = timeout;
    tv.tv_usec = 0;
    switch (select(rc->read + 1, &readfds, NULL, NULL, &tv)) {
    case 0:
	errno = ETIMEDOUT;
	/* FALLTHROUGH */
    case -1:
	sshprintf(("%s: ssh: net_read_fillbuf: case -1\n", debug_prefix_time(NULL)));
	return (-1);
    case 1:
	sshprintf(("%s: ssh: net_read_fillbuf: case 1\n", debug_prefix_time(NULL)));
	assert(FD_ISSET(rc->read, &readfds));
	break;
    default:
	sshprintf(("%s: ssh: net_read_fillbuf: case default\n", debug_prefix_time(NULL)));
	assert(0);
	break;
    }
    rc->readbuf.left = 0;
    rc->readbuf.size = read(rc->read, rc->readbuf.buf, size);
    if (rc->readbuf.size < 0)
	return (-1);
    rc->readbuf.left = rc->readbuf.size;
    sshprintf(("%s: ssh: net_read_fillbuf: end %d\n", debug_prefix_time(NULL),rc->readbuf.size));
    return (0);
}

#endif	/* SSH_SECURITY */
