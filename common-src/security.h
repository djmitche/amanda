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
 * $Id: security.h,v 1.1 1998/11/04 20:26:58 kashmir Exp $
 *
 * security api
 */
#ifndef SECURITY_H
#define	SECURITY_H

struct security_handle;

/*
 * This structure defines a security driver.  This driver abstracts
 * common security actions behind a set of function pointers.  Macros
 * mask this.
 */
typedef struct security_driver {
    /*
     * The name of this driver, eg, "BSD", "KRB4", etc...
     */
    const char *name;

    /*
     * Returns a security handle, for this driver.  This handle
     * is used in protocol communications.
     *
     * The first form gets a handle given a hostname.
     */
    void *(*connect) P((const char *));

    /*
     * This form sets up a callback that returns new handles as
     * they are received.  It takes an input and output file descriptor.
     */
    void (*accept) P((int, int, void (*)(void *, void *, pkt_t *), void *));

    /*
     * Frees up handles allocated by the previous
     */
    void (*close) P((void *));

    /*
     * This transmits a packet after adding the security information
     * Returns 0 on success, negative on error.
     */
    int (*sendpkt) P((void *, pkt_t *));

    /*
     * This creates an event in the event handler for receiving pkt_t's
     * on a security_handle.  The given callback with the given arg
     * will be called when the driver determines that it has data
     * for that handle.
     *
     * If the callback is called with a NULL packet, then an error
     * occurred in receiving.
     * 
     * Only one recvpkt request can exist per handle.
     */
    void (*recvpkt) P((void *, void (*)(void *, pkt_t *), void *));

    /*
     * Cancel an outstanding recvpkt request on a handle.
     */
    void (*recvpkt_cancel) P((void *));

    /*
     * Get a stream given a security handle
     */
    void *(*stream_server) P((void *));

    /*
     * Get a stream and connect it to a remote given a security handle
     * and a stream id.
     */
    void *(*stream_client) P((void *, int));

    /*
     * Close a stream opened with stream_server or stream_client
     */
    void (*stream_close) P((void *));

    /*
     * Return a numeric id for a stream.
     */
    int (*stream_id) P((void *));

    /*
     * Write to a stream.
     */
    int (*stream_write) P((void *, const void *, size_t));

    /*
     * Read asyncronously from a stream.  Only one request can exist
     * per stream.
     */
    void (*stream_read) P((void *, void (*)(void *, void *, size_t), void *));

    /*
     * Cancel a stream read request
     */
    void (*stream_read_cancel) P((void *));

} security_driver_t;

/*
 * This structure is a handle to a connection to a host for transmission
 * of protocol packets (pkt_t's).  The underlying security type defines
 * the actual protocol and transport.
 *
 * This handle is reference counted so that it can be used inside of
 * security streams after it has been closed by our callers.
 */
typedef struct security_handle {
    const security_driver_t *driver;
    char *error;
    int refcnt;
} security_handle_t;

/*
 * This structure is a handle to a stream connection to a host for
 * transmission of random data such as dumps or index data.
 * It is inherently associated with (and created from) a security_handle_t.
 */
typedef struct security_stream {
    security_handle_t *security_handle;
} security_stream_t;


const security_driver_t *security_getdriver P((const char *));

const char *security_geterror P((security_handle_t *));
void security_seterror P((security_handle_t *, const char *, ...));

security_handle_t *security_connect P((const security_driver_t *,
    const char *));
void security_accept P((const security_driver_t *, int, int,
    void (*)(security_handle_t *, pkt_t *)));
void security_close P((security_handle_t *));

/* int security_sendpkt P((security_handle_t *, const pkt_t *)); */
#define	security_sendpkt(handle, pkt)		\
    (*(handle)->driver->sendpkt)(handle, pkt)

/* void security_recvpkt P((security_handle_t *, void (*)(void *, pkt_t *),
    void *); */
#define	security_recvpkt(handle, fn, arg)	\
    (*(handle)->driver->recvpkt)(handle, fn, arg)

/* void security_recvpkt_cancel P((security_handle_t *)); */
#define	security_recvpkt_cancel(handle)		\
    (*(handle)->driver->recvpkt_cancel)(handle)

security_stream_t *security_stream_server P((security_handle_t *));
security_stream_t *security_stream_client P((security_handle_t *, int));
void security_stream_close P((security_stream_t *));

/* int security_stream_id P((security_stream_t *)); */
#define	security_stream_id(stream)		\
    (*(stream)->security_handle->driver->stream_id)(stream)

/* int security_stream_write P((security_stream_t *, const void *, size_t)); */
#define	security_stream_write(stream, buf, size)	\
    (*(stream)->security_handle->driver->stream_write)(stream, buf, size)

/* void security_stream_read P((security_stream_t *,
    void (*)(void *, void *, size_t), void *)); */
#define	security_stream_read(stream, fn, arg)		\
    (*(stream)->security_handle->driver->stream_read)(stream, fn, arg)

/* void security_stream_read_cancel P((security_stream_t *)); */
#define	security_stream_read_cancel(stream)		\
    (*(stream)->security_handle->driver->stream_read_cancel)(stream)

#endif	/* SECURITY_H */
