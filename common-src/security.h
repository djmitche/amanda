/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1999 University of Maryland at College Park
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
 * $Id: security.h,v 1.10 1999/04/16 05:13:09 kashmir Exp $
 *
 * security api
 */
#ifndef SECURITY_H
#define	SECURITY_H

struct security_handle;

/*
 * This is a type that gets passed to the security_recvpkt() and
 * security_connect() callbacks.
 * It details what the status of this callback is.
 */
typedef enum { S_OK, S_TIMEOUT, S_ERROR } security_status_t;

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
     * Connects a security handle, for this driver to a remote
     * host.
     */
    void (*connect) P((const char *,
	void (*)(void *, struct security_handle *, security_status_t),
	void *));

    /*
     * This form sets up a callback that returns new handles as
     * they are received.  It takes an input and output file descriptor.
     */
    void (*accept) P((int, int, void (*)(struct security_handle *, pkt_t *)));

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
     * for that handle.  The last argument is a timeout, in seconds.
     * This may be -1 to indicate no timeout.
     *
     * If there was an error or timeout, this will be indicated in
     * the status argument.
     * 
     * Only one recvpkt request can exist per handle.
     */
    void (*recvpkt) P((void *, void (*)(void *, pkt_t *,
	security_status_t), void *, int));

    /*
     * Cancel an outstanding recvpkt request on a handle.
     */
    void (*recvpkt_cancel) P((void *));

    /*
     * Get a stream given a security handle
     */
    void *(*stream_server) P((void *));

    /*
     * Accept a stream created by stream_server
     */
    int (*stream_accept) P((void *));

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
     * Authenticate a stream.
     */
    int (*stream_auth) P((void *));

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
    void (*stream_read) P((void *, void (*)(void *, void *, ssize_t), void *));

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
} security_handle_t;

/*
 * This structure is a handle to a stream connection to a host for
 * transmission of random data such as dumps or index data.
 */
typedef struct security_stream {
    const security_driver_t *driver;
    char *error;
} security_stream_t;


const security_driver_t *security_getdriver P((const char *));
void security_handleinit P((security_handle_t *, const security_driver_t *));
void security_streaminit P((security_stream_t *, const security_driver_t *));

/* const char *security_geterror P((security_handle_t *)); */
#define	security_geterror(handle)	((handle)->error)
void security_seterror P((security_handle_t *, const char *, ...))
    __attribute__ ((format (printf, 2, 3)));


/* void security_connect P((const security_driver_t *, const char *,
    void (*)(void *, security_handle_t *, security_status_t), void *)); */
#define	security_connect(driver, hostname, fn, arg)	\
    (*(driver)->connect)(hostname, fn, arg)
/* void security_accept P((const security_driver_t *, int, int,
    void (*)(security_handle_t *, pkt_t *))); */
#define	security_accept(driver, in, out, fn)	\
    (*(driver)->accept)(in, out, fn)
void security_close P((security_handle_t *));

/* int security_sendpkt P((security_handle_t *, const pkt_t *)); */
#define	security_sendpkt(handle, pkt)		\
    (*(handle)->driver->sendpkt)(handle, pkt)

/* void security_recvpkt P((security_handle_t *,
    void (*)(void *, pkt_t *, security_status_t), void *, int); */
#define	security_recvpkt(handle, fn, arg, timeout)	\
    (*(handle)->driver->recvpkt)(handle, fn, arg, timeout)

/* void security_recvpkt_cancel P((security_handle_t *)); */
#define	security_recvpkt_cancel(handle)		\
    (*(handle)->driver->recvpkt_cancel)(handle)

/* const char *security_stream_geterror P((security_stream_t *)); */
#define	security_stream_geterror(stream)	((stream)->error)
void security_stream_seterror P((security_stream_t *, const char *, ...))
    __attribute__ ((format (printf, 2, 3)));

/* security_stream_t *security_stream_server P((security_handle_t *)); */
#define	security_stream_server(handle)	\
    (*(handle)->driver->stream_server)(handle)

/* int security_stream_accept P((security_stream_t *)); */
#define	security_stream_accept(stream)		\
    (*(stream)->driver->stream_accept)(stream)

/* security_stream_t *security_stream_client P((security_handle_t *, int)); */
#define	security_stream_client(handle, id)	\
    (*(handle)->driver->stream_client)(handle, id)

void security_stream_close P((security_stream_t *));

/* int security_stream_auth P((security_stream_t *)); */
#define	security_stream_auth(stream)		\
    (*(stream)->driver->stream_auth)(stream)

/* int security_stream_id P((security_stream_t *)); */
#define	security_stream_id(stream)		\
    (*(stream)->driver->stream_id)(stream)

/* int security_stream_write P((security_stream_t *, const void *, size_t)); */
#define	security_stream_write(stream, buf, size)	\
    (*(stream)->driver->stream_write)(stream, buf, size)

/* void security_stream_read P((security_stream_t *,
    void (*)(void *, void *, size_t), void *)); */
#define	security_stream_read(stream, fn, arg)		\
    (*(stream)->driver->stream_read)(stream, fn, arg)

/* void security_stream_read_cancel P((security_stream_t *)); */
#define	security_stream_read_cancel(stream)		\
    (*(stream)->driver->stream_read_cancel)(stream)

#endif	/* SECURITY_H */
