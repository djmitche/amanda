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
 * $Id: event.c,v 1.7 1999/03/01 21:37:37 kashmir Exp $
 *
 * Event handler.  Serializes different kinds of events to allow for
 * a uniform interface, central state storage, and localized
 * interdependency logic.
 */

/*#define	EVENT_DEBUG */

#include "amanda.h"
#include "event.h"
#include "queue.h"

/*
 * The opaque handle passed back to the caller.  This is typedefed to
 * event_handle_t in our header file.
 */
struct event_handle {
    event_fn_t fn;		/* function to call when this fires */
    void *arg;			/* argument to pass to previous function */
    event_type_t type;		/* type of event */
    int data;			/* type data */
    time_t lastfired;		/* timestamp when this last fired */
    int wakeup;			/* if nonzero, and EV_WAIT, then fire */
    TAILQ_ENTRY(event_handle) tq;	/* queue handle */
};

/*
 * Our queue of currently active events.
 */
static struct {
    TAILQ_HEAD(, event_handle) tailq;
    int qlength;
} eventq = {
    TAILQ_HEAD_INITIALIZER(eventq.tailq), 0
};
#define	eventq_first()		TAILQ_FIRST(&eventq.tailq)
#define	eventq_next(eh)		TAILQ_NEXT(eh, tq)
#define	eventq_append(eh)	TAILQ_INSERT_TAIL(&eventq.tailq, eh, tq);
#define	eventq_remove(eh)	TAILQ_REMOVE(&eventq.tailq, eh, tq);

/*
 * A table of currently set signal handlers.
 */
static struct sigtabent {
    event_handle_t *handle;	/* handle for this signal */
    int scoreboard;		/* number of signals recvd since last checked */
    void (*oldhandler) P((int));/* old handler (for unsetting) */
} sigtable[NSIG];

#ifdef EVENT_DEBUG
static const char *event_type2str P((event_type_t));
#endif
static void release P((event_handle_t *));
static void signal_handler P((int));

#undef min
#define	min(a, b)	((a) < (b) ? (a) : (b))
#undef max
#define	max(a, b)	((a) > (b) ? (a) : (b))

/*
 * Add a new event.  See the comment in event.h for what the arguments
 * mean.
 */
event_handle_t *
event_register(data, type, fn, arg)
    int data;
    event_type_t type;
    event_fn_t fn;
    void *arg;
{
    event_handle_t *handle;

    assert(data >= 0);
    /* make sure signals are within range */
    assert(type != EV_SIG || data < NSIG);
    /* make sure we don't double-register a signal */
    assert(type != EV_SIG || sigtable[data].handle == NULL);
    /* callers can't register EV_DEAD */
    assert(type != EV_DEAD);

    handle = alloc(sizeof(*handle));
    handle->fn = fn;
    handle->arg = arg;
    handle->type = type;
    handle->data = data;
    handle->lastfired = -1;
    handle->wakeup = 0;
    eventq_append(handle);
    eventq.qlength++;

#ifdef EVENT_DEBUG
    fprintf(stderr, "event: register: %X data=%d, type=%s\n", (int)handle,
	handle->data, event_type2str(handle->type));
#endif
    return (handle);
}

/*
 * Mark an event to be released.  Because we may be traversing the queue
 * when this is called, we must wait until later to actually remove
 * the event.
 */
void
event_release(handle)
    event_handle_t *handle;
{

    assert(handle != NULL);

#ifdef EVENT_DEBUG
    fprintf(stderr, "event: release (mark): %X data=%d, type=%s\n",
	(int)handle, handle->data, event_type2str(handle->type));
#endif
    assert(handle->type != EV_DEAD);

    /*
     * For signal events, we need to specially remove then from the
     * signal event table.
     */
    if (handle->type == EV_SIG) {
	struct sigtabent *se = &sigtable[handle->data];

	assert(se->handle == handle);
	signal(handle->data, se->oldhandler);
	se->handle = NULL;
	se->scoreboard = 0;
    }

    /*
     * Decrement the qlength now since this is no longer a real
     * event.
     */
    eventq.qlength--;

    /*
     * Mark it as dead and leave it for the loop to remove.
     */
    handle->type = EV_DEAD;
}

/*
 * Release an event.
 */
static void
release(handle)
    event_handle_t *handle;
{

    assert(handle != NULL);
    assert(handle->type == EV_DEAD);

#ifdef EVENT_DEBUG
    fprintf(stderr, "event: release (actual): %X data=%d, type=%s\n",
	(int)handle, handle->data, event_type2str(handle->type));
#endif

    eventq_remove(handle);
    amfree(handle);
}

/*
 * Wakeup all EV_WAIT events waiting on the specified id.
 */
int
event_wakeup(id)
    int id;
{
    event_handle_t *eh;
    int nwaken = 0;

#ifdef EVENT_DEBUG
	fprintf(stderr, "event: wakeup: enter (%d)\n", id);
#endif

    assert(id >= 0);

    for (eh = eventq_first(); eh != NULL; eh = eventq_next(eh)) {

	if (eh->type == EV_WAIT && eh->data == id) {
#ifdef EVENT_DEBUG
	    fprintf(stderr, "event: wakeup: %X id=%d\n", (int)eh, id);
#endif
	    eh->wakeup = 1;
	    nwaken++;
	}
    }
    return (nwaken);
}


/*
 * The event loop.  We need to be specially careful here with adds and
 * deletes.  Since adds and deletes will often happen while this is running,
 * we need to make sure we don't end up referencing a dead event handle.
 */
void
event_loop(dontblock)
    const int dontblock;
{
    static int entry = 0;
    fd_set readfds, writefds, errfds, werrfds;
    struct timeval timeout, *tvptr;
    int ntries, maxfd, rc, interval, fired;
    time_t curtime;
    event_handle_t *eh, *nexteh;
    struct sigtabent *se;

#ifdef EVENT_DEBUG
	fprintf(stderr, "event: loop: enter: dontblock=%d, qlength=%d\n",
	    dontblock, eventq.qlength);
#endif

    /*
     * If we have no events, we have nothing to do
     */
    if (eventq.qlength == 0)
	return;

    /*
     * We must not be entered twice
     */
    assert(++entry == 1);

    ntries = 0;

    /*
     * Save a copy of the current time once, to reduce syscall load
     * slightly.
     */
    curtime = time(NULL);

    do {
#ifdef EVENT_DEBUG
	fprintf(stderr, "event: loop: dontblock=%d, qlength=%d\n", dontblock,
	    eventq.qlength);
	for (eh = eventq_first(); eh != NULL; eh = eventq_next(eh)) {
	    fprintf(stderr, "%X: %s data=%d fn=0x%x arg=0x%x\n", (int)eh,
		event_type2str(eh->type), eh->data, (int)eh->fn, (int)eh->arg);
	}
#endif
	/*
	 * Set ourselves up with no timeout initially.
	 */
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;

	/*
	 * If we can block, initially set the tvptr to NULL.  If
	 * we come across timeout events in the loop below, they
	 * will set it to an appropriate buffer.
	 *
	 * If we can't block, set it to point to the timeout buf above.
	 */
	if (dontblock)
	    tvptr = &timeout;
	else
	    tvptr = NULL;

	/*
	 * Rebuild the select bitmasks each time.
	 */
	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	FD_ZERO(&errfds);
	maxfd = 0;

	/*
	 * Run through each event handle and setup the events.
	 * We save our next pointer early in case we GC some dead
	 * events.  We also fire EV_WAIT events that have arisen.
	 */
	for (eh = eventq_first(); eh != NULL; eh = nexteh) {
	    nexteh = eventq_next(eh);

	    switch (eh->type) {

	    /*
	     * Read fds just get set into the select bitmask
	     */
	    case EV_READFD:
		FD_SET(eh->data, &readfds);
		FD_SET(eh->data, &errfds);
		maxfd = max(maxfd, eh->data);
		break;

	    /*
	     * Likewise with write fds
	     */
	    case EV_WRITEFD:
		FD_SET(eh->data, &writefds);
		FD_SET(eh->data, &errfds);
		maxfd = max(maxfd, eh->data);
		break;

	    /*
	     * Only set signals that aren't already set to avoid unnecessary
	     * syscall overhead.
	     */
	    case EV_SIG:
		se = &sigtable[eh->data];

		if (se->handle == eh)
		    break;

		/* no previous handle */
		assert(se->handle == NULL);
		se->handle = eh;
		se->scoreboard = 0;
		se->oldhandler = signal(eh->data, signal_handler);
		break;

	    /*
	     * Compute the timeout for this select
	     */
	    case EV_TIME:
		/* if we're not supposed to block, then leave it at 0 */
		if (dontblock)
		    break;

		if (eh->lastfired == -1)
		    eh->lastfired = curtime;

		interval = eh->data - (curtime - eh->lastfired);
		if (interval < 0)
		    interval = 0;

		if (tvptr != NULL)
		    timeout.tv_sec = min(timeout.tv_sec, interval);
		else {
		    /* this is the first timeout */
		    tvptr = &timeout;
		    timeout.tv_sec = interval;
		}
		break;

	    /*
	     * Do nothing with these events right now.
	     */
	    case EV_WAIT:
	    case EV_DEAD:
		break;

	    default:
		assert(0);
		break;
	    }
	}

	/*
	 * Let 'er rip
	 */
#ifdef EVENT_DEBUG
	fprintf(stderr, "event: select: dontblock=%d, maxfd=%d, timeout=%ld\n",
	    dontblock, maxfd, tvptr != NULL ? timeout.tv_sec : -1);
#endif
	rc = select(maxfd + 1, &readfds, &writefds, &errfds, tvptr);
#ifdef EVENT_DEBUG
	fprintf(stderr, "event: select returns %d\n", rc);
#endif

	/*
	 * Select errors can mean many things.  Interrupted events should
	 * not be fatal, since they could be delivered signals which still
	 * need to have their events fired.
	 */
	if (rc < 0) {
	    if (errno != EINTR) {
		if (++ntries > 5)
		    error("select failed: %s", strerror(errno));
		continue;
	    }
	    /* proceed if errno == EINTR, we may have caught a signal */

	    /* contents cannot be trusted */
	    FD_ZERO(&readfds);
	    FD_ZERO(&writefds);
	    FD_ZERO(&errfds);
	}

	/*
	 * Grab the current time again for use in timed events.
	 */
	curtime = time(NULL);

	/*
	 * We need to copy the errfds into werrfds, so file descriptors
	 * that are being polled for both reading and writing have
	 * both of their poll events 'see' the error.
	 */
	memcpy(&werrfds, &errfds, sizeof(werrfds));

	/*
	 * Now run through the events and fire the ones that are ready.
	 * Don't handle file descriptor events if the select failed.
	 */
	for (eh = eventq_first(); eh != NULL; eh = eventq_next(eh)) {
	    fired = 0;

	    switch (eh->type) {

	    /*
	     * Read fds: just fire the event if set in the bitmask
	     */
	    case EV_READFD:
		if (FD_ISSET(eh->data, &readfds) ||
		    FD_ISSET(eh->data, &errfds)) {
		    FD_CLR(eh->data, &readfds);
		    FD_CLR(eh->data, &errfds);
		    fired = 1;
		}
		break;

	    /*
	     * Write fds: same as Read fds
	     */
	    case EV_WRITEFD:
		if (FD_ISSET(eh->data, &writefds) ||
		    FD_ISSET(eh->data, &werrfds)) {
		    FD_CLR(eh->data, &writefds);
		    FD_CLR(eh->data, &werrfds);
		    fired = 1;
		}
		break;

	    /*
	     * Signal events: check the scoreboard for fires, and run the
	     * event if we got one.
	     */
	    case EV_SIG:
		se = &sigtable[eh->data];
		if (se->scoreboard > 0) {
		    assert(se->handle == eh);
		    se->scoreboard = 0;
		    fired = 1;
		}
		break;

	    /*
	     * Timed events: check the interval elapsed since last fired,
	     * and set it off if greater or equal to requested interval.
	     */
	    case EV_TIME:
		if (eh->lastfired == -1)
		    eh->lastfired = curtime;
		if (curtime - eh->lastfired >= eh->data) {
#ifdef EVENT_DEBUG
		    fprintf(stderr, "event: %X fired: time=%d\n", (int)eh,
			eh->data);
#endif
		    fired = 1;
		}
		break;

	    /*
	     * Wait events are fired before the select call.
	     */
	    case EV_WAIT:
		break;

	    /*
	     * Dead events require another scan for removal.
	     */
	    case EV_DEAD:
		break;

	    default:
		assert(0);
		break;
	    }

	    if (fired) {
#ifdef EVENT_DEBUG
		fprintf(stderr, "event: %X fired: data=%d, type=%s (qlen=%d)\n",
		    (int)eh, eh->data, event_type2str(eh->type),
		    eventq.qlength);
#endif
		assert(eh->type != EV_DEAD);
		eh->lastfired = curtime;
		(*eh->fn)(eh->arg);
	    }
	}
	/*
	 * Do a separate pass for these events, which are usually caused by
	 * events above, and need to run afterwards.
	 */
	for (eh = eventq_first(); eh != NULL; eh = nexteh) {
	    nexteh = eventq_next(eh);
	    switch (eh->type) {
	    /*
	     * If this EV_WAIT event has been woken up, then fire it.
	     */
	    case EV_WAIT:
		if (eh->wakeup == 0)
		    break;
		eh->wakeup = 0;
#ifdef EVENT_DEBUG
		fprintf(stderr, "event: %X fired: data=%d, type=%s (qlen=%d)\n",
		    (int)eh, eh->data, event_type2str(eh->type),
		    eventq.qlength);
#endif
		assert(eh->type != EV_DEAD);
		eh->lastfired = curtime;
		(*eh->fn)(eh->arg);
		break;

	    /*
	     * Prune dead events
	     */
	    case EV_DEAD:
		release(eh);
		break;

	    default:
		break;
	    }
	}

    } while (!dontblock && eventq.qlength > 0);

    assert(--entry == 0);
}

/*
 * Generic signal handler.  Used to count caught signals for the event
 * loop.
 */
static void
signal_handler(signo)
    int signo;
{

    assert(signo >= 0 && signo < NSIG);
    sigtable[signo].scoreboard++;
}

#ifdef EVENT_DEBUG
/*
 * Convert an event type into a string
 */
static const char *
event_type2str(type)
    event_type_t type;
{
    static const struct {
	event_type_t type;
	const char name[12];
    } event_types[] = {
#define	X(s)	{ s, stringize(s) }
	X(EV_READFD),
	X(EV_WRITEFD),
	X(EV_SIG),
	X(EV_TIME),
	X(EV_WAIT),
	X(EV_DEAD),
#undef X
    };
    int i;

    for (i = 0; i < sizeof(event_types) / sizeof(event_types[0]); i++)
	if (type == event_types[i].type)
	    return (event_types[i].name);
    return ("BOGUS EVENT TYPE");
}
#endif	/* EVENT_DEBUG */
