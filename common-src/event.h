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
 * $Id: event.h,v 1.1 1998/11/04 20:08:35 kashmir Exp $
 */
#ifndef EVENT_H
#define EVENT_H

/*
 * These functions define a generic event interface.  One can register a
 * function vector and the type of events to act on, and the event handler
 * will dispatch as necessary.
 */

/*
 * An opaque handle returned by the registry functions that can be
 * used to unregister an event in the future.
 */
struct event_handle;
typedef struct event_handle event_handle_t;

/*
 * The function signature for functions that get called when an event
 * fires.
 */
typedef void (*event_fn_t) P((void *));

/*
 * The types of events we can register.
 * EV_DEAD is for internal use only.
 */
typedef enum { EV_READFD, EV_WRITEFD, EV_SIG, EV_TIME, EV_DEAD } event_type_t;

/*
 * Register an event handler.
 *
 * For readfd and writefd events, the first arg is the file descriptor.
 * There can be multiple callers firing on the same file descriptor.
 *
 * For signal events, the first arg is the signal number as defined in
 * <signal.h>.  There can only be one signal handler. (do we need more?)
 *
 * For time events, the first arg is the interval in seconds between
 * pulses.  There can be multiple time events, of course.  Don't
 * count on the time events being too accurate.  They depend on the
 * caller calling event_loop() often enough.
 */
event_handle_t *event_register P((int, event_type_t, event_fn_t, void *));

/*
 * Release an event handler.
 */
void event_release P((event_handle_t *));

/*
 * Process events.  If the argument is nonzero, then the loop does
 * not block.
 */
void event_loop P((const int));

#endif	/* EVENT_H */
