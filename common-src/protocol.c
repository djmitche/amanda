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
 * $Id: protocol.c,v 1.40 2006/04/05 13:24:01 martinea Exp $
 *
 * implements amanda protocol
 */
#include "amanda.h"
#include "event.h"
#include "packet.h"
#include "security.h"
#include "protocol.h"

/*#define	PROTO_DEBUG*/

/*
 * Valid actions that can be passed to the state machine
 */
typedef enum {
    A_START, A_TIMEOUT, A_ERROR, A_RCVDATA, A_CONTPEND, A_PENDING,
    A_CONTINUE, A_FINISH, A_ABORT
} action_t;

/*
 * The current state type.  States are represented as function
 * vectors.
 */
struct proto;
typedef action_t (*pstate_t) P((struct proto *, action_t, pkt_t *));

/*
 * This is a request structure that is wrapped around a packet while it
 * is being passed through amanda.  It holds the timeouts, state, and handles
 * for each request.
 */
typedef struct proto {
    pstate_t state;			/* current state of the request */
    char *hostname;			/* remote host */
    const security_driver_t *security_driver;	/* for connect retries */
    security_handle_t *security_handle;	/* network stream for this req */
    time_t timeout;			/* seconds for this timeout */
    time_t repwait;			/* seconds to wait for reply */
    time_t origtime;			/* orig start time of this request */
    time_t curtime;			/* time when this attempt started */
    int connecttries;			/* times we'll retry a connect */
    int reqtries;			/* times we'll resend a REQ */
    int acktries;			/* times we'll wait for an a ACK */
    pkt_t req;				/* the actual wire request */
    protocol_sendreq_callback continuation; /* call when req dies/finishes */
    void *datap;			/* opaque cookie passed to above */
    char *(*conf_fn) P((char *, void *));/* configuration function */
} proto_t;

#define	CONNECT_TRIES	3	/* num retries after connect errors */
#define	CONNECT_WAIT	5	/* secs between connect attempts */
#define ACK_WAIT	10	/* time (secs) to wait for ACK - keep short */
#define ACK_TRIES	3	/* num retries after ACK_WAIT timeout */
#define REQ_TRIES	2	/* num restarts (reboot/crash) */
#define CURTIME	(time(0) - proto_init_time) /* time relative to start */

/* if no reply in an hour, just forget it */
#define	DROP_DEAD_TIME(t)	(CURTIME - (t) > (60 * 60))

/* get the size of an array */
#define	ASIZE(arr)	(sizeof(arr) / sizeof((arr)[0]))

/*
 * Initialization time
 */
static time_t proto_init_time;

/* local functions */

#ifdef PROTO_DEBUG
static const char *action2str P((action_t));
static const char *pstate2str P((pstate_t));
#endif

static void connect_callback P((void *, security_handle_t *,
    security_status_t));
static void connect_wait_callback P((void *));
static void recvpkt_callback P((void *, pkt_t *, security_status_t));

static action_t s_sendreq P((proto_t *, action_t, pkt_t *));
static action_t s_ackwait P((proto_t *, action_t, pkt_t *));
static action_t s_repwait P((proto_t *, action_t, pkt_t *));
static void state_machine P((proto_t *, action_t, pkt_t *));

/*
 * -------------------
 * Interface functions
 */

/*
 * Initialize globals.
 */
void
protocol_init()
{

    proto_init_time = time(NULL);
}

/*
 * Generate a request packet, and submit it to the state machine
 * for transmission.
 */
void
protocol_sendreq(hostname, security_driver, conf_fn, req, repwait, continuation, datap)
    const char *hostname;
    const security_driver_t *security_driver;
    char *(*conf_fn) P((char *, void *));
    const char *req;
    time_t repwait;
    protocol_sendreq_callback continuation;
    void *datap;
{
    proto_t *p;

    p = alloc(sizeof(proto_t));
    p->state = s_sendreq;
    p->hostname = stralloc(hostname);
    p->security_driver = security_driver;
    /* p->security_handle set in connect_callback */
    p->repwait = repwait;
    p->origtime = CURTIME;
    /* p->curtime set in the sendreq state */
    p->connecttries = CONNECT_TRIES;
    p->reqtries = REQ_TRIES;
    p->acktries = ACK_TRIES;
    p->conf_fn = conf_fn;
    pkt_init(&p->req, P_REQ, req);

    /*
     * These are here for the caller
     * We call the continuation function after processing is complete.
     * We pass the datap on through untouched.  It is here so the caller
     * has a way to keep state with each request.
     */
    p->continuation = continuation;
    p->datap = datap;

#ifdef PROTO_DEBUG
    dbprintf(("%s: security_connect: host %s -> p %X\n", 
	      debug_prefix_time(": protocol"), hostname, (int)p));
#endif

    security_connect(p->security_driver, p->hostname, conf_fn, connect_callback, p,
		     p->datap);
}

/*
 * This is a callback for security_connect.  After the security layer
 * has initiated a connection to the given host, this will be called
 * with a security_handle_t.
 *
 * On error, the security_status_t arg will reflect errors which can
 * be had via security_geterror on the handle.
 */
static void
connect_callback(cookie, security_handle, status)
    void *cookie;
    security_handle_t *security_handle;
    security_status_t status;
{
    proto_t *p = cookie;

    assert(p != NULL);
    p->security_handle = security_handle;

#ifdef PROTO_DEBUG
    dbprintf(("%s: connect_callback: p %X\n",
	      debug_prefix_time(": protocol"), (int)p));
#endif

    switch (status) {
    case S_OK:
	state_machine(p, A_START, NULL);
	break;

    case S_TIMEOUT:
	security_seterror(p->security_handle, "timeout during connect");
	/* FALLTHROUGH */

    case S_ERROR:
	/*
	 * For timeouts or errors, retry a few times, waiting CONNECT_WAIT
	 * seconds between each attempt.  If they all fail, just return
	 * an error back to the caller.
	 */
	if (--p->connecttries == 0) {
	    state_machine(p, A_ABORT, NULL);
	} else {
#ifdef PROTO_DEBUG
    dbprintf(("%s: connect_callback: p %X: retrying %s\n",
	      debug_prefix_time(": protocol"), (int)p, p->hostname));
#endif
	    security_close(p->security_handle);
	    /* XXX overload p->security handle to hold the event handle */
	    p->security_handle =
		(security_handle_t *)event_register(CONNECT_WAIT, EV_TIME,
		connect_wait_callback, p);
	}
	break;

    default:
	assert(0);
	break;
    }
}

/*
 * This gets called when a host has been put on a wait queue because
 * initial connection attempts failed.
 */
static void
connect_wait_callback(cookie)
    void *cookie;
{
    proto_t *p = cookie;

    event_release((event_handle_t *)p->security_handle);
    security_connect(p->security_driver, p->hostname, p->conf_fn,
	connect_callback, p, p->datap);
}


/*
 * Does a one pass protocol sweep.  Handles any incoming packets that 
 * are waiting to be processed, and then deals with any pending
 * requests that have timed out.
 *
 * Callers should periodically call this after they have submitted
 * requests if they plan on doing a lot of work.
 */
void
protocol_check()
{

    /* arg == 1 means don't block */
    event_loop(1);
}


/*
 * Does an infinite pass protocol sweep.  This doesn't return until all
 * requests have been satisfied or have timed out.
 *
 * Callers should call this after they have finished submitting requests
 * and are just waiting for all of the answers to come back.
 */
void
protocol_run()
{

    /* arg == 0 means block forever until no more events are left */
    event_loop(0);
}


/*
 * ------------------
 * Internal functions
 */

/*
 * The guts of the protocol.  This handles the many paths a request can
 * make, including retrying the request and acknowledgements, and dealing
 * with timeouts and successfull replies.
 */
static void
state_machine(p, action, pkt)
    proto_t *p;
    action_t action;
    pkt_t *pkt;
{
    pstate_t curstate;
    action_t retaction;

#ifdef PROTO_DEBUG
	dbprintf(("%s: state_machine: initial: p %X action %s pkt %X\n",
		debug_prefix_time(": protocol"),
		(int)p, action2str(action), NULL));
#endif

    assert(p != NULL);
    assert(action == A_RCVDATA || pkt == NULL);
    assert(p->state != NULL);

    for (;;) {
#ifdef PROTO_DEBUG
	dbprintf(("%s: state_machine: p %X state %s action %s\n",
		  debug_prefix_time(": protocol"),
		  (int)p, pstate2str(p->state), action2str(action)));
	if (pkt != NULL) {
	    dbprintf(("%s: pkt: %s (t %d) orig REQ (t %d cur %d)\n",
		      debug_prefix(": protocol"),
		      pkt_type2str(pkt->type), (int)CURTIME,
		      (int)p->origtime, (int)p->curtime));
	    dbprintf(("%s: pkt contents:\n-----\n%s-----\n",
		      debug_prefix(": protocol"), pkt->body));
	}
#endif

	/*
	 * p->state is a function pointer to the current state a request
	 * is in.
	 *
	 * We keep track of the last state we were in so we can make
	 * sure states which return A_CONTINUE really have transitioned
	 * the request to a new state.
	 */
	curstate = p->state;

	if (action == A_ABORT)
	    /*
	     * If the passed action indicates a terminal error, then we
	     * need to move to abort right away.
	     */
	    retaction = A_ABORT;
	else
	    /*
	     * Else we run the state and perform the action it
	     * requests.
	     */
	    retaction = (*curstate)(p, action, pkt);

#ifdef PROTO_DEBUG
	dbprintf(("%s: state_machine: p %X state %s returned %s\n",
		  debug_prefix_time(": protocol"),
		  (int)p, pstate2str(p->state), action2str(retaction)));
#endif

	/*
	 * The state function is expected to return one of the following
	 * action_t's.
	 */
	switch (retaction) {

	/*
	 * Request is still waiting for more data off of the network.
	 * Setup to receive another pkt, and wait for the recv event
	 * to occur.
	 */
	case A_CONTPEND:
	    (*p->continuation)(p->datap, pkt, p->security_handle);
	    /* FALLTHROUGH */

	case A_PENDING:
#ifdef PROTO_DEBUG
	    dbprintf(("%s: state_machine: p %X state %s: timeout %d\n",
		      debug_prefix_time(": protocol"),
		      (int)p, pstate2str(p->state), (int)p->timeout));
#endif
	    /*
	     * Get the security layer to register a receive event for this
	     * security handle on our behalf.  Have it timeout in p->timeout
	     * seconds.
	     */
	    security_recvpkt(p->security_handle, recvpkt_callback, p,
		p->timeout);

	    return;

	/*
	 * Request has moved to another state.  Loop and run it again.
	 */
	case A_CONTINUE:
	    assert(p->state != curstate);
#ifdef PROTO_DEBUG
	    dbprintf(("%s: state_machine: p %X: moved from %s to %s\n",
		      debug_prefix_time(": protocol"),
		      (unsigned int)p, pstate2str(curstate),
		      pstate2str(p->state)));
#endif
	    continue;

	/*
	 * Request has failed in some way locally.  The security_handle will
	 * contain an appropriate error message via security_geterror().  Set
	 * pkt to NULL to indicate failure to the callback, and then
	 * fall through to the common finish code.
	 *
	 * Note that remote failures finish via A_FINISH, because they did
	 * complete successfully locally.
	 */
	case A_ABORT:
	    pkt = NULL;
	    /* FALLTHROUGH */

	/*
	 * Request has completed successfully.
	 * Free up resources the request has used, call the continuation
	 * function specified by the caller and quit.
	 */
	case A_FINISH:
	    (*p->continuation)(p->datap, pkt, p->security_handle);
	    security_close(p->security_handle);
	    amfree(p->hostname);
	    amfree(p);
	    return;

	default:
	    assert(0);
	    break;	/* in case asserts are turned off */
	}
	/* NOTREACHED */
    }
    /* NOTREACHED */
}

/*
 * The request send state.  Here, the packet is actually transmitted
 * across the network.  After setting up timeouts, the request
 * moves to the acknowledgement wait state.  We return from the state
 * machine at this point, and let the request be received from the network.
 */
static action_t
s_sendreq(p, action, pkt)
    proto_t *p;
    action_t action;
    pkt_t *pkt;
{

    assert(p != NULL);

    if (security_sendpkt(p->security_handle, &p->req) < 0) {
	/* XXX should retry */
	security_seterror(p->security_handle, "error sending REQ: %s",
	    security_geterror(p->security_handle));
	return (A_ABORT);
    }

    /*
     * Remember when this request was first sent
     */
    p->curtime = CURTIME;

    /*
     * Move to the ackwait state
     */
    p->state = s_ackwait;
    p->timeout = ACK_WAIT;
    return (A_PENDING);
}

/*
 * The acknowledge wait state.  We can enter here two ways:
 *
 *  - the caller has received a packet, located the request for
 *    that packet, and called us with an action of A_RCVDATA.
 *    
 *  - the caller has determined that a request has timed out,
 *    and has called us with A_TIMEOUT.
 *
 * Here we process the acknowledgment, which usually means that
 * the client has agreed to our request and is working on it.
 * It will later send a reply when finished.
 */
static action_t
s_ackwait(p, action, pkt)
    proto_t *p;
    action_t action;
    pkt_t *pkt;
{

    assert(p != NULL);

    /*
     * The timeout case.  If our retry count has gone to zero
     * fail this request.  Otherwise, move to the send state
     * to retry the request.
     */
    if (action == A_TIMEOUT) {
	assert(pkt == NULL);

	if (--p->acktries == 0) {
	    security_seterror(p->security_handle, "timeout waiting for ACK");
	    return (A_ABORT);
	}

	p->state = s_sendreq;
	return (A_CONTINUE);
    }

    assert(action == A_RCVDATA);
    assert(pkt != NULL);

    /*
     * The packet-received state.  Determine what kind of
     * packet we received, and act based on the reply type.
     */
    switch (pkt->type) {

    /*
     * Received an ACK.  Everything's good.  The client is
     * now working on the request.  We queue up again and
     * wait for the reply.
     */
    case P_ACK:
	p->state = s_repwait;
	p->timeout = p->repwait;
	return (A_PENDING);

    /*
     * Received a NAK.  The request failed, so free up the
     * resources associated with it and return.
     *
     * This should NOT return A_ABORT because it is not a local failure.
     */
    case P_NAK:
	return (A_FINISH);

    /*
     * The client skipped the ACK, and replied right away.
     * Move to the reply state to handle it.
     */
    case P_REP:
    case P_PREP:
	p->state = s_repwait;
	return (A_CONTINUE);

    /*
     * Unexpected packet.  Requeue this request and hope
     * we get what we want later.
     */
    default:
	return (A_PENDING);
    }
}

/*
 * The reply wait state.  We enter here much like we do with s_ackwait.
 */
static action_t
s_repwait(p, action, pkt)
    proto_t *p;
    action_t action;
    pkt_t *pkt;
{
    pkt_t ack;

    /*
     * Timeout waiting for a reply.
     */
    if (action == A_TIMEOUT) {
	assert(pkt == NULL);

	/*
	 * If we've blown our timeout limit, free up this packet and
	 * return.
	 */
	if (p->reqtries == 0 || DROP_DEAD_TIME(p->origtime)) {
	    security_seterror(p->security_handle, "timeout waiting for REP");
	    return (A_ABORT);
	}

	/*
	 * We still have some tries left.  Resend the request.
	 */
	p->reqtries--;
	p->state = s_sendreq;
	p->acktries = ACK_TRIES;
	return (A_CONTINUE);
    }

    assert(action == A_RCVDATA);

    /*
     * We've received some data.  If we didn't get a reply,
     * requeue the packet and retry.  Otherwise, acknowledge
     * the reply, cleanup this packet, and return.
     */
    if (pkt->type != P_REP && pkt->type != P_PREP)
	return (A_PENDING);

    if(pkt->type == P_REP) {
	pkt_init(&ack, P_ACK, "");
	if (security_sendpkt(p->security_handle, &ack) < 0) {
	    /* XXX should retry */
	    security_seterror(p->security_handle, "error sending ACK: %s",
		security_geterror(p->security_handle));
	    return (A_ABORT);
	}
	return (A_FINISH);
    }
    else if(pkt->type == P_PREP) {
	p->timeout = p->repwait - CURTIME + p->curtime + 1;
	return (A_CONTPEND);
    }

    /* should never go here, shut up compiler warning */
    return (A_FINISH);
}

/*
 * event callback that receives a packet
 */
static void
recvpkt_callback(cookie, pkt, status)
    void *cookie;
    pkt_t *pkt;
    security_status_t status;
{
    proto_t *p = cookie;

    assert(p != NULL);

    switch (status) {
    case S_OK:
	state_machine(p, A_RCVDATA, pkt);
	break;
    case S_TIMEOUT:
	state_machine(p, A_TIMEOUT, NULL);
	break;
    case S_ERROR:
	state_machine(p, A_ABORT, NULL);
	break;
    default:
	assert(0);
	break;
    }
}

/*
 * --------------
 * Misc functions
 */

#ifdef PROTO_DEBUG
/*
 * Convert a pstate_t into a printable form.
 */
static const char *
pstate2str(pstate)
    pstate_t pstate;
{
    static const struct {
	pstate_t type;
	const char name[12];
    } pstates[] = {
#define	X(s)	{ s, stringize(s) }
	X(s_sendreq),
	X(s_ackwait),
	X(s_repwait),
#undef X
    };
    int i;

    for (i = 0; i < ASIZE(pstates); i++)
	if (pstate == pstates[i].type)
	    return (pstates[i].name);
    return ("BOGUS PSTATE");
}

/*
 * Convert an action_t into a printable form
 */
static const char *
action2str(action)
    action_t action;
{
    static const struct {
	action_t type;
	const char name[12];
    } actions[] = {
#define	X(s)	{ s, stringize(s) }
	X(A_START),
	X(A_TIMEOUT),
	X(A_ERROR),
	X(A_RCVDATA),
	X(A_CONTPEND),
	X(A_PENDING),
	X(A_CONTINUE),
	X(A_FINISH),
	X(A_ABORT),
#undef X
    };
    int i;

    for (i = 0; i < ASIZE(actions); i++)
	if (action == actions[i].type)
	    return (actions[i].name);
    return ("BOGUS ACTION");
}
#endif	/* PROTO_DEBUG */
