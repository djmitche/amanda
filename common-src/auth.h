/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 2010 Zmanda, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * Contact information: Zmanda Inc., 465 S. Mathilda Ave., Suite 300
 * Sunnyvale, CA 94085, USA, or: http://www.zmanda.com
 */

/* TODO docs:
 *
 * - protocol process
 *   - amandad:
 *     1. listen
 *     2. recvpkt/sendpkt, repeatedly until end of xaction
 *        if recvpkt -> NULL, or REQ_TIMEOUT, exit
 *     3. sendpkt(NULL)
 *     4. go to 2
 *
 *   - initiator:
 *     1. connect
 *     2. sendpkt/recvpkt, repeatedly until end of xaction
 *        if recvpkt -> NULL, treat as EOF
 *     3. sendpkt(NULL) on end of xaction
 *     4. g_object_unref if done, else goto 2
 *        don't let connections sit around for > REQ_TIMEOUT/2
 *
 * TODO: add a "protocol" object to abstract this?
 *
 * YES: this is important.  Amrecover overlaps multiple services on
 * the server, so we need to be able to track protocol transactions through
 * to completion, although half-open-ness is not required.
 */

/* TODO: explicit close methods */

/* TODO: support half-close on streams */

/* TODO: once close/half-close is implemented, recheck refcounting and be more
 * aggressive about holding refs */

/* TODO: lots more state asserts in auth-compat.c */

#ifndef AUTH_H
#define AUTH_H

#include <glib.h>
#include <glib-object.h>
#include "packet.h"

/*
 * Types
 */

typedef struct AuthConn AuthConn;
typedef struct AuthStream AuthStream;
typedef struct AuthProto AuthProto;

typedef enum {
    AUTH_OK,	    /* operation complete */
    AUTH_TIMEOUT,   /* operation timed out */
    AUTH_ERROR,	    /* error performing operation */
} auth_operation_status_t;

/* A callback to get a configuration parameter.
 *
 * TODO: replace with just a pointer to auth_t from conffile.h
 */
typedef char *(*auth_conf_getter_t)(
    char *param_name,
    gpointer arg);

/* A callback invoked when a connect call is complete (either success,
 * failure, or timeout, as described by STATUS)
 *
 * @param arg: the argument passed along with the callback
 * @param conn: the connection
 * @param status: the status of the operation
 */
typedef void (*auth_connect_callback_t)(
    gpointer arg,
    AuthConn *conn,
    auth_operation_status_t status);

/* A callback invoked when a new protocol transaction begins, or when no
 * more protocol transactions should be expected (PROTO is NULL)
 *
 * @param arg: the argument passed along with the callback
 * @param conn: the connection
 * @param proto: the new proto
 */
typedef void (*auth_proto_callback_t)(
    gpointer arg,
    AuthConn *conn,
    AuthProto *proto);

/* A callback invoked when a recvpkt operation is complete (either success,
 * failure, or timeout, as described by STATUS).
 *
 * @param arg: the argument passed along with the callback
 * @param proto: the proto for this packet
 * @param pkt: the packet (only set if status is AUTH_OK)
 * @param status: the status of the operation
 */
typedef void (*auth_recvpkt_callback_t)(
    gpointer arg,
    AuthProto *proto,
    pkt_t *pkt,
    auth_operation_status_t status);

/* A callback invoked when data is available from an AuthStream.
 *
 * @param arg: the argument passed along with the callback
 * @param stream: the stream
 * @param buf: the data buffer, or NULL on error
 * @param size: the buffer size, or 0 on EOF
 */
typedef void (*auth_read_callback_t)(
    gpointer arg,
    AuthStream *stream,
    gpointer buf,
    gsize size);

/*********
 * AuthConn
 *
 * An Auth connection to a remote host
 */

GType auth_conn_get_type(void);
#define AUTH_CONN_TYPE (auth_conn_get_type())
#define AUTH_CONN(obj) G_TYPE_CHECK_INSTANCE_CAST((obj), auth_conn_get_type(), AuthConn)
#define AUTH_CONN_CONST(obj) G_TYPE_CHECK_INSTANCE_CAST((obj), auth_conn_get_type(), AuthConn const)
#define AUTH_CONN_CLASS(klass) G_TYPE_CHECK_CLASS_CAST((klass), auth_conn_get_type(), AuthConnClass)
#define IS_AUTH_CONN(obj) G_TYPE_CHECK_INSTANCE_TYPE((obj), auth_conn_get_type ())
#define AUTH_CONN_GET_CLASS(obj) G_TYPE_INSTANCE_GET_CLASS((obj), auth_conn_get_type(), AuthConnClass)

/*
 * Main object structure
 */

struct AuthConn {
    GObject __parent__;

    /* protected */

    /* configuration-getter */
    auth_conf_getter_t conf;
    gpointer conf_arg;

    /* current error message, or NULL if no error has occurred; this is returned
     * by the default error_message method, and can be set by subclasses.  The
     * parent class will free this string on finalize, if it's not NULL */
    char *errmsg;
};

/*
 * Class definition
 */

typedef struct {
    GObjectClass __parent__;

    /*The name of this driver, eg, "BSD", "BSDTCP", "KRB5" */
    const char *name;

    /* Finish constructing this object.
     *
     * This method need not be implemented by subclasses, but if it is, the
     * subclass must call up to the parent implementation.
     *
     * @param conn: connection
     * @param auth: authentication mechanism in use
     * @param conf: config getter
     * @param conf_arg: arg for config getter
     */
    void (*construct)(
	AuthConn *conn,
	const char *auth,
	auth_conf_getter_t conf,
	gpointer conf_arg);

    /* STATIC METHOD - called by amandad via auth_listen
     *
     * This creates and returns the amandad-side AuthConn object, and begins
     * listening for incoming protocol transactions.
     *
     * Must be implemented in subclasses.
     *
     * @param auth: the authentication name
     * @param cb: callback made for each new proto conversation
     * @param cb_arg: arg to be passed to CB
     * @param conf: configuration getter function
     * @param conf_arg: arg to be passed to CONF
     * @param argc: count of remaining amandad command-line arguments
     * @param argv: remaining args
     */
    AuthConn *(*listen)(
	const char *auth,
	auth_proto_callback_t cb,
	gpointer cb_arg,
	auth_conf_getter_t conf,
	gpointer conf_arg,
	int argc,
	char **argv);

    /* Sets up an Auth connection to a remote host.
     *
     * Must be implemented in subclasses.
     *
     * @param conn: connection
     * @param hostname: the host to connect to
     * @param cb: callback to invoke when the connection is made
     * @param cb_arg: arg to pass to the callback
     */
    void (*connect)(
	AuthConn *conn,
	const char *hostname,
	auth_connect_callback_t cb,
	gpointer cb_arg);

    /* Get the remote hostname.  This hostname is verified to the extent that
     * the underlying authentication method can do so.
     *
     * Must be implemented in subclasses.
     *
     * @param conn: connection
     * @returns: static string giving peer name
     */
    char *(*get_authenticated_peer_name)(
	AuthConn *conn);

    /* Error handling */

    /* Returns a (statically allocated) error message string.
     *
     * @param conn: the connection
     * @returns: an error message (never NULL)
     */
    char *(*error_message)(
	AuthConn *conn);

    /* Set the error message - the input string will be copied internally, and
     * remains the responsibility of the caller.  This is used by protocol.c to
     * add more helpful error messages to a security handle.
     *
     * @param conn: the connection
     * @param errmsg: the new error message
     */
    void (*set_error_message)(
	AuthConn *conn,
	char *errmsg);

    /* Protocol */

    /* Create an outgoing protocol transaction.
     *
     * Must be implemented in subclasses.
     *
     * @param conn: the conncection
     * @returns: an AuthProto or NULL on error
     */
    AuthProto *(*proto_new)(
	AuthConn *conn);

    /* Streams */

    /* Listen for an incoming stream within the connection.
     *
     * Must be implemented in subclasses.
     *
     * @param conn: the conncection
     * @returns: an AuthStream or NULL on error
     */
    AuthStream *(*stream_listen)(
	AuthConn *conn);

    /* Create a new stream within the connection, connecting to the given ID
     * (from the id method of the stream on the other end of the connection).
     *
     * If verification of the remote system fails, then this method will
     * return NULL with an appropriate error message from the error_message
     * method.
     *
     * Must be implemented in subclasses.
     *
     * @param conn: the conncection
     * @param id: stream identifier
     * @returns: an AuthStream or NULL on error
     */
    AuthStream *(*stream_connect)(
	AuthConn *conn,
	int id);
} AuthConnClass;

/*
 * Method stubs
 */

/* see the class definition above for documentation */
void auth_conn_construct(AuthConn *conn, const char *auth,
	auth_conf_getter_t conf, gpointer conf_arg);
void auth_conn_connect(AuthConn *conn, const char *hostname,
	auth_connect_callback_t cb, gpointer cb_arg);
char *auth_conn_get_authenticated_peer_name(AuthConn *conn);
char *auth_conn_error_message(AuthConn *conn);
void auth_conn_set_error_message(AuthConn *conn, char *errmsg);
AuthProto *auth_conn_proto_new(AuthConn *conn);
AuthStream *auth_conn_stream_listen(AuthConn *conn);
AuthStream *auth_conn_stream_connect(AuthConn *conn, int id);

/*********
 * AuthProto
 *
 * A packet-based transaction within an AuthConn
 */

GType auth_proto_get_type(void);
#define AUTH_PROTO_TYPE (auth_proto_get_type())
#define AUTH_PROTO(obj) G_TYPE_CHECK_INSTANCE_CAST((obj), auth_proto_get_type(), AuthProto)
#define AUTH_PROTO_CONST(obj) G_TYPE_CHECK_INSTANCE_CAST((obj), auth_proto_get_type(), AuthProto const)
#define AUTH_PROTO_CLASS(klass) G_TYPE_CHECK_CLASS_CAST((klass), auth_proto_get_type(), AuthProtoClass)
#define IS_AUTH_PROTO(obj) G_TYPE_CHECK_INSTANCE_TYPE((obj), auth_proto_get_type ())
#define AUTH_PROTO_GET_CLASS(obj) G_TYPE_INSTANCE_GET_CLASS((obj), auth_proto_get_type(), AuthProtoClass)

/*
 * Main object structure
 */

struct AuthProto {
    GObject __parent__;

    /* protected */

    /* the connection housing this proto */
    AuthConn *conn;

    /* private */

    /* current error message, or NULL if no error has occurred; this is returned
     * by the default error_message method, and can be set by subclasses if they
     * are using this method. */
    char *errmsg;
};

/*
 * Class definition
 */

typedef struct {
    GObjectClass __parent__;

    /* Finish constructing this object.  In particular, this adds a reference to
     * the parent connection which will be removed when the proto is closed.
     *
     * @param proto: the proto
     * @param conn: parent connection
     */
    void (*construct)(
	AuthProto *proto,
	AuthConn *conn);

    /* Returns a (statically allocated) error message string.
     *
     * @param proto: the proto
     * @returns: an error message (never NULL)
     */
    char *(*error_message)(
	AuthProto *proto);

    /* Transmit a packet (optionally adding driver-specific metadata to it).
     * Signal the end of a protocol transaction with a NULL packet.
     *
     * Must be implemented in subclasses.
     *
     * @param proto: the proto
     * @param packet: packet to send (remains caller's responsibility to free),
     *	    or NULL to signal the end of a protocol transaction
     *	    TODO: ^^^ change that to an explicit close method
     * @returns TRUE on success, otherwise FALSE.
     */
    gboolean (*sendpkt)(
	AuthProto *proto,
	pkt_t *packet);

    /* Wait for a packet in this transaction and call RECVPKT_CB when one
     * arrives or when the TIMEOUT expires.  Note that it is up to the caller
     * to know whether to expect a packet or not.
     *
     * Must be implemented in subclasses.
     *
     * @param proto: the proto
     * @param recvpkt_cb: callback to invoke for each incoming packet
     * @param recvpkt_cb_arg: arg to pass to the callback
     * @param timeout: timeout, in seconds, or 0 for no timeout
     */
    void (*recvpkt)(
	AuthProto *proto,
	auth_recvpkt_callback_t recvpkt_cb,
	gpointer recvpkt_cb_arg,
	int timeout);

} AuthProtoClass;

/*
 * Method stubs
 */

void auth_proto_construct(AuthProto *proto, AuthConn *conn);
gboolean auth_proto_accept(AuthProto *proto);
char *auth_proto_error_message(AuthProto *proto);
gboolean auth_proto_sendpkt(AuthProto *proto, pkt_t *packet);
void auth_proto_recvpkt(AuthProto *proto, auth_recvpkt_callback_t recvpkt_cb,
	gpointer recvpkt_cb_arg, int timeout);


/*********
 * AuthStream
 *
 * A bidirectional data flow within an AuthConn
 */

GType auth_stream_get_type(void);
#define AUTH_STREAM_TYPE (auth_stream_get_type())
#define AUTH_STREAM(obj) G_TYPE_CHECK_INSTANCE_CAST((obj), auth_stream_get_type(), AuthStream)
#define AUTH_STREAM_CONST(obj) G_TYPE_CHECK_INSTANCE_CAST((obj), auth_stream_get_type(), AuthStream const)
#define AUTH_STREAM_CLASS(klass) G_TYPE_CHECK_CLASS_CAST((klass), auth_stream_get_type(), AuthStreamClass)
#define IS_AUTH_STREAM(obj) G_TYPE_CHECK_INSTANCE_TYPE((obj), auth_stream_get_type ())
#define AUTH_STREAM_GET_CLASS(obj) G_TYPE_INSTANCE_GET_CLASS((obj), auth_stream_get_type(), AuthStreamClass)

/*
 * Main object structure
 */

struct AuthStream {
    GObject __parent__;

    /* protected */

    /* the connection housing this stream */
    AuthConn *conn;

    /* The stream identifier for this stream.  When a stream is created via
     * stream_listen, then this ID should be transmitted to the remote system
     * for use with the stream_connect method. */
    int id;

    /* private */

    /* current error message, or NULL if no error has occurred; this is returned
     * by the default error_message method, and can be set by subclasses if they
     * are using this method. */
    char *errmsg;
};

/*
 * Class definition
 */

typedef struct {
    GObjectClass __parent__;

    /* Finish constructing this object.  In particular, this adds a reference to
     * the parent connection which will be removed when the stream is closed.
     *
     * @param stream: the stream
     * @param conn: parent connection
     */
    void (*construct)(
	AuthStream *stream,
	AuthConn *conn);

    /* Accept an incoming connection from the remote end; usable only on
     * streams created with stream_listen.  This performs any appropriate
     * verification of the remote system, and returns FALSE if the
     * authentication fails.
     *
     * Must be implemented in subclasses.
     *
     * TODO: auth-compat should copy error from conn to stream?
     *
     * @param stream: the stream
     * @returns: FALSE on failure
     */
    gboolean (*accept)(
	AuthStream *stream);

    /* Returns a (statically allocated) error message string.
     *
     * @param stream: the stream
     * @returns: an error message (never NULL)
     */
    char *(*error_message)(
	AuthStream *stream);

    /* Write a full buffer to a stream, blocking until all bytes are written.
     * Returns FALSE on an error.
     *
     * Must be implemented in subclasses.
     *
     * @param stream: stream to write to
     * @param buf: buffer to write
     * @param size: number of bytes to write
     * @returns: FALSE on error, TRUE on success.
     */
    gboolean (*write)(
	AuthStream *stream,
	gconstpointer buf,
	size_t size);

    /* Set the callback to receive the next block of data that arrives on this
     * stream.  Any bytes that arrive while the callback is set to NULL will be
     * discarded.  On EOF, the callback is invoked with a NULL buffer and a
     * size of zero.  On error, the callback is invoked with a NULL buffer and
     * a negative size.
     *
     * TODO: change this around so it's a permanent registration?
     *
     * Must be implemented in subclasses.
     *
     * @param stream: the stream
     * @param read_cb: the callback, or NULL to discard bytes
     * @param read_cb_arg: the argument passed to the callback
     */
    void (*read)(
	AuthStream *stream,
	auth_read_callback_t read_cb,
	gpointer read_cb_arg);
} AuthStreamClass;

/*
 * Method stubs
 */

void auth_stream_construct(AuthStream *stream, AuthConn *conn);
gboolean auth_stream_accept(AuthStream *stream);
char *auth_stream_error_message(AuthStream *stream);
gboolean auth_stream_write(AuthStream *stream, gconstpointer buf, size_t size);
void auth_stream_read(AuthStream *stream,
	auth_read_callback_t read_cb, gpointer read_cb_arg);

/*
 * Constructor
 */

/* Create a new AuthConn object with the given AUTH and using configuration
 * acquired via the given getter CONF.
 *
 * @param auth: the authentication name
 * @param conf: configuration getter function
 * @param conf_arg: arg to be passed to CONF
 * @returns: a newly allocated AuthConn object, or NULL on error
 */
AuthConn *auth_conn_new(const char *auth, auth_conf_getter_t conf, gpointer conf_arg);

/* Listen for incoming connections, calling the given accept_cb with each new
 * incoming connection, or when no more connections are expected.  This is
 * intended for "blind" use from amandad, and should do whatever is appropriate
 * for the mode by which amandad should be invoked for the corresponding auth
 * method.
 *
 * @param auth: the authentication name
 * @param cb: callback to be invoked when the connection is accepted
 * @param cb_arg: arg to be passed to CB
 * @param conf: configuration getter function
 * @param conf_arg: arg to be passed to CONF
 * @param argc: count of remaining amandad command-line arguments
 * @param argv: remaining args
 */
void auth_listen(const char *auth,
	auth_proto_callback_t cb, gpointer cb_arg,
	auth_conf_getter_t conf, gpointer conf_arg,
	int argc, char **argv);

/*
 * Debugging
 *
 * Levels:
 *  1: amandad connection/packet logging
 *  2: odd codepaths
 *  6: every API call
 */

/* Note that this uses g_debug, so newlines are not necessary */
extern int debug_auth;	/* <-- easier than including conffile.h */
#define AUTH_DEBUG(lvl, ...) do { \
    if ((lvl) <= debug_auth) { \
	g_debug(__VA_ARGS__); \
    } \
} while (0)

#endif
