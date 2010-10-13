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

#include "amanda.h"
#include "security.h"
#include "auth.h"

/*
 * Types
 */

/* Tracking for the type of a conn or stream; this is done just to ensure that
 * the API is being used correctly */
typedef enum {
    AUTH_NEW,
    AUTH_LISTENING,
    AUTH_CONNECTING,
    AUTH_CONNECTED,
} auth_state_t;

/*
 * Stream
 */

static GType auth_compat_stream_get_type(void);
#define AUTH_COMPAT_STREAM_TYPE (auth_compat_stream_get_type())
#define AUTH_COMPAT_STREAM(obj) G_TYPE_CHECK_INSTANCE_CAST((obj), auth_compat_stream_get_type(), AuthCompatStream)
#define AUTH_COMPAT_STREAM_CONST(obj) G_TYPE_CHECK_INSTANCE_CAST((obj), auth_compat_stream_get_type(), AuthCompatStream const)
#define AUTH_COMPAT_STREAM_CLASS(klass) G_TYPE_CHECK_CLASS_CAST((klass), auth_compat_stream_get_type(), AuthCompatStreamClass)
#define IS_AUTH_COMPAT_STREAM(obj) G_TYPE_CHECK_INSTANCE_TYPE((obj), auth_compat_stream_get_type ())
#define AUTH_COMPAT_STREAM_GET_CLASS(obj) G_TYPE_INSTANCE_GET_CLASS((obj), auth_compat_stream_get_type(), AuthCompatStreamClass)

static GObjectClass *parent_stream_class = NULL;

typedef struct AuthCompatStream {
    AuthStream __parent__;

    legacy_security_stream_t *leg_stream;

    auth_state_t state;

    gboolean reading;
    auth_read_callback_t read_cb;
    gpointer read_cb_arg;
} AuthCompatStream;

typedef struct {
    AuthStreamClass __parent__;
} AuthCompatStreamClass;

struct AuthCompatConn;

static AuthCompatStream *
auth_compat_stream_new(
    struct AuthCompatConn *conn,
    legacy_security_stream_t *leg_stream,
    auth_state_t state)
{
    AuthCompatStream *self = AUTH_COMPAT_STREAM(g_object_new(AUTH_COMPAT_STREAM_TYPE, NULL));

    auth_stream_construct(AUTH_STREAM(self), AUTH_CONN(conn));
    AUTH_STREAM(self)->id = legacy_security_stream_id(leg_stream);

    self->leg_stream = leg_stream;
    self->state = state;
    return self;
}

static void
stream_init_impl(
    AuthStream *stream)
{
    AuthCompatStream *self = AUTH_COMPAT_STREAM(stream);

    self->state = AUTH_NEW;
}

static void
stream_finalize_impl(
    GObject * obj_self)
{
    AuthCompatStream *self = AUTH_COMPAT_STREAM(obj_self);

    if (self->reading) {
	legacy_security_stream_read_cancel(self->leg_stream);
	self->reading = FALSE;
    }

    legacy_security_stream_close(self->leg_stream);
    self->leg_stream = NULL;

    G_OBJECT_CLASS(parent_stream_class)->finalize(obj_self);
}

static gboolean
stream_accept_impl(
    AuthStream *stream)
{
    AuthCompatStream *self = AUTH_COMPAT_STREAM(stream);
    g_assert(self->state == AUTH_LISTENING);

    if (0 == legacy_security_stream_accept(self->leg_stream)) {
	self->state = AUTH_CONNECTED;
	return TRUE;
    }

    return FALSE;
}

static char *
stream_error_message_impl(
    AuthStream *stream)
{
    AuthCompatStream *self = AUTH_COMPAT_STREAM(stream);
    AUTH_DEBUG(1, "stream error message: %s", legacy_security_stream_geterror(self->leg_stream));
    return legacy_security_stream_geterror(self->leg_stream);
}

static gboolean
stream_write_impl(
    AuthStream *stream,
    gconstpointer buf,
    size_t size)
{
    AuthCompatStream *self = AUTH_COMPAT_STREAM(stream);
    g_assert(self->state == AUTH_CONNECTED);

    return 0 == legacy_security_stream_write(self->leg_stream, buf, size);
}

static void
compat_read_callback(
    void *arg,
    void *buf,
    ssize_t size)
{
    AuthCompatStream *self = AUTH_COMPAT_STREAM(arg);
    g_assert(self->read_cb != NULL);

    AUTH_DEBUG(6, "compat_read_callback(%p, %p, %zd)", self, buf, size);

    self->reading = FALSE; /* TODO: tmp */
    self->read_cb(self->read_cb_arg, AUTH_STREAM(self), buf, (gsize)size);
}

static void
stream_read_impl(
    AuthStream *stream,
    auth_read_callback_t read_cb,
    gpointer read_cb_arg)
{
    AuthCompatStream *self = AUTH_COMPAT_STREAM(stream);
    g_assert(self->state == AUTH_CONNECTED);

    if (read_cb) {
	self->read_cb = read_cb;
	self->read_cb_arg = read_cb_arg;

	if (!self->reading)
	    legacy_security_stream_read(self->leg_stream,
			    compat_read_callback, self);
	self->reading = TRUE;
    } else {
	self->read_cb = NULL;
	self->read_cb_arg = NULL;

	/* TODO: Auth API claims to buffer incoming data at this point.. */
	if (self->reading)
	    legacy_security_stream_read_cancel(self->leg_stream);

	self->reading = FALSE;
    }
}

static void
stream_class_init(
    AuthCompatStreamClass * selfc)
{
    AuthStreamClass *klass = AUTH_STREAM_CLASS(selfc);
    GObjectClass *goc = G_OBJECT_CLASS(selfc);

    goc->finalize = stream_finalize_impl;

    klass->accept = stream_accept_impl;
    klass->error_message = stream_error_message_impl;
    klass->write = stream_write_impl;
    klass->read = stream_read_impl;

    parent_stream_class = g_type_class_peek_parent(selfc);
}

static GType
auth_compat_stream_get_type (void)
{
    static GType type = 0;

    if G_UNLIKELY(type == 0) {
        static const GTypeInfo info = {
            sizeof (AuthCompatStreamClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) stream_class_init,
            (GClassFinalizeFunc) NULL,
            NULL /* class_data */,
            sizeof (AuthCompatStream),
            0 /* n_preallocs */,
            (GInstanceInitFunc) stream_init_impl,
            NULL
        };

        type = g_type_register_static (AUTH_STREAM_TYPE, "AuthCompatStream", &info, 0);
    }

    return type;
}

/*
 * Legacy quirk handling
 */

typedef struct legacy_auth_quirk_t {
    /* name of the auth (key) */
    char *auth;

    /* If TRUE, this authentication needs a call to close_packet_stream to
     * close the packet stream before potentially opening a new one.  Basically
     * true for all tcpm-based legacy drivers */
    gboolean need_close_packet_stream;
} legacy_auth_quirk_t;

legacy_auth_quirk_t legacy_auth_quirks[] = {
    { "bsd", FALSE },
    { "bsdtcp", TRUE, },
    { "bsdudp", FALSE },
    { "local", TRUE },
    { "rsh", TRUE },
    { "ssh", TRUE },
    { "krb5", TRUE },
    { NULL, FALSE }
};

static legacy_auth_quirk_t *
find_quirk(
    char *auth)
{
    legacy_auth_quirk_t *quirk = legacy_auth_quirks;
    while (quirk->auth) {
	if (0 == g_ascii_strcasecmp(quirk->auth, auth))
	    break;
	quirk++;
    }

    /* (this may return the final, default quirk) */
    return quirk;
}

/*
 * Connection
 */

GType auth_compat_conn_get_type(void);
#define AUTH_COMPAT_CONN_TYPE (auth_compat_conn_get_type())
#define AUTH_COMPAT_CONN(obj) G_TYPE_CHECK_INSTANCE_CAST((obj), auth_compat_conn_get_type(), AuthCompatConn)
#define AUTH_COMPAT_CONN_CONST(obj) G_TYPE_CHECK_INSTANCE_CAST((obj), auth_compat_conn_get_type(), AuthCompatConn const)
#define AUTH_COMPAT_CONN_CLASS(klass) G_TYPE_CHECK_CLASS_CAST((klass), auth_compat_conn_get_type(), AuthCompatConnClass)
#define IS_AUTH_COMPAT_CONN(obj) G_TYPE_CHECK_INSTANCE_TYPE((obj), auth_compat_conn_get_type ())
#define AUTH_COMPAT_CONN_GET_CLASS(obj) G_TYPE_INSTANCE_GET_CLASS((obj), auth_compat_conn_get_type(), AuthCompatConnClass)

static GObjectClass *parent_conn_class = NULL;

typedef struct AuthCompatConn {
    AuthConn __parent__;

    /* the name of the legacy driver to use, and its quirks */
    char *driver_name;
    legacy_auth_quirk_t *quirk;

    /* the security_handle we're wrapping */
    legacy_security_handle_t *leg_handle;

    /* tracking for the state of this handle; this just helps ensure that the
     * methods are called in the correct order */
    auth_state_t state;

    /* sometimes the underlying legacy API delivers packets earlier than we
     * want them, for example in the callback to legacy_security_accept, or for
     * protocol transactions after the first (delivered through a repeat
     * invocation of the accept callback).  So this is a queue of as-yet
     * un-recvpkt'd packets, possibly including a NULL packet to indicate the
     * end of packets. */
    GSList *pkt_queue;

    /* If TRUE, the recvpkt should not call legacy_security_recvpkt, as the packet
     * transaction is closed, and we are waiting for another call of the legacy
     * accept callback with a new packet */
    gboolean recvpkt_should_wait_for_accept_cb;

    /* outstanding callbacks, and their argument */
    auth_connect_callback_t connect_cb;
    auth_recvpkt_callback_t recvpkt_cb;
    gpointer arg;

    /* unique identifier for the "current" recvpkt invocation; this is used to
     * ensure that "old" timeouts quickly go away.  It should be incremented every
     * time recvpkt_cb is called. */
    guint64 current_recvpkt_num;
} AuthCompatConn;

typedef struct {
    AuthConnClass __parent__;
} AuthCompatConnClass;

static void
conn_init_impl(
    AuthConn *conn)
{
    AuthCompatConn *self = AUTH_COMPAT_CONN(conn);

    self->state = AUTH_NEW;
}

static void
conn_finalize_impl(
    GObject * obj_self)
{
    AuthCompatConn *self = AUTH_COMPAT_CONN(obj_self);

    if (self->leg_handle) {
	legacy_security_close_connection(self->leg_handle, "ignored");
	self->leg_handle = NULL;
    }

    if (self->driver_name)
	g_free(self->driver_name);

    G_OBJECT_CLASS(parent_conn_class)->finalize(obj_self);
}

/* -- construct -- */

static void
conn_construct_impl(
    AuthConn *conn,
    const char *auth,
    auth_conf_getter_t conf,
    gpointer conf_arg)
{
    AuthCompatConn *self = AUTH_COMPAT_CONN(conn);
    self->driver_name = g_strdup(auth);
    self->quirk = find_quirk(self->driver_name);

    AUTH_CONN_CLASS(parent_conn_class)->construct(conn, auth, conf, conf_arg);
}

/* -- recvpkt handling -- */

/* an object to track recvpkt timeouts when the legacy API won't do it for us
 * (specifically, between protocol transactions).  Note that most recvpkt
 * timeouts are handled internally by the legacy protocol. */
typedef struct recvpkt_timeout_tracker_t {
    /* refcounted pointer to the connection; this may keep stuff open longer
     * than desired, but it beats dangling references */
    AuthCompatConn *conn;

    /* the recvpkt_num for which we're timing out */
    guint64 recvpkt_num;

    /* remaining seconds until timeout.  We use this, rather than just
     * scheduling a callback at the end of the timeout because if other
     * operations block for nearly the duration of the timeout, we may end up
     * being called back in the same cycle of the mainloop as the packet
     * callback, and miss the packet itself.  This would be sad.
     */
    int remaining;
} recvpkt_timeout_tracker_t;

/* GSourceFunc for recvpkt timeouts */
static gboolean
recvpkt_timeout_sourcefunc(
    gpointer arg)
{
    recvpkt_timeout_tracker_t *trk = arg;
    auth_recvpkt_callback_t cb;

    g_debug("recvpkt sourcefunc tick %d", trk->remaining);

    /* first, if this recvpkt has happened, cancel the source */
    if (trk->conn->current_recvpkt_num != trk->recvpkt_num)
	goto free_and_cancel;

    /* if the timeout hasn't expired, call me again */
    if (--trk->remaining)
	return TRUE;

    /* time's up! */
    AUTH_DEBUG(6, "recvpkt_timeout_sourcefunc(conn=%p) calling back", trk->conn);

    /* NULL out the callback before calling it */
    cb = trk->conn->recvpkt_cb;
    trk->conn->recvpkt_cb = NULL;
    trk->conn->current_recvpkt_num++;

    /* note that legacy_security and auth statuses have the same values */
    cb(trk->conn->arg, AUTH_CONN(trk->conn), NULL, AUTH_TIMEOUT);

free_and_cancel:
    g_object_unref(trk->conn);
    g_free(trk);
    return FALSE;
}

/* if conn->recvpkt_cb is set and there's a packet, bring the two together and
 * return TRUE; else FALSE */
static gboolean
maybe_call_recvpkt_cb(
    AuthCompatConn *self)
{
    auth_recvpkt_callback_t cb;

    if (self->recvpkt_cb && self->pkt_queue) {
	pkt_t *pkt = self->pkt_queue->data;
	self->pkt_queue = g_slist_remove(self->pkt_queue, pkt);

	/* NULL out the callback before invoking it */
	cb = self->recvpkt_cb;
	self->recvpkt_cb = NULL;
	self->current_recvpkt_num++;

	cb(self->arg, AUTH_CONN(self), pkt, AUTH_OK);
	return TRUE;
    }
    return FALSE;
}

static void
compat_recvpkt_callback(
    void *arg,
    pkt_t *pkt,
    legacy_security_status_t status)
{
    AuthCompatConn *self = AUTH_COMPAT_CONN(arg);
    auth_recvpkt_callback_t cb;

    AUTH_DEBUG(6, "compat_recvpkt_callback(%p, %p, %d)", arg, pkt, status);

    /* this callback should never be called when there are packets queued */
    g_assert(self->pkt_queue == NULL);

    /* we need to translate EOF (NULL, S_ERROR + errmsg matching "EOF")
     * into (NULL, AUTH_OK).  Luckily the tcpm code uses a single error
     * message for the EOF. */
    if (!pkt && status == S_ERROR) {
	char *errmsg = legacy_security_geterror(self->leg_handle);
	if (errmsg && 0 == strncmp_const(errmsg, "EOF on read"))
	    status = AUTH_OK;
    }

    /* NULL out the callback before calling it */
    cb = self->recvpkt_cb;
    self->recvpkt_cb = NULL;
    self->current_recvpkt_num++;

    /* note that legacy_security and auth statuses have the same values */
    cb(self->arg, AUTH_CONN(self), pkt, (auth_operation_status_t)status);
}

static void
conn_recvpkt_impl(
    AuthConn *conn,
    auth_recvpkt_callback_t recvpkt_cb,
    gpointer recvpkt_cb_arg,
    int timeout)
{
    AuthCompatConn *self = AUTH_COMPAT_CONN(conn);
    g_assert(self->state == AUTH_CONNECTED);

    self->recvpkt_cb = recvpkt_cb;
    self->arg = recvpkt_cb_arg;

    /* are we expecting the next packet via the accept cb? */
    if (self->recvpkt_should_wait_for_accept_cb) {
	recvpkt_timeout_tracker_t *trk = g_new0(recvpkt_timeout_tracker_t, 1);

	/* don't do this next time */
	self->recvpkt_should_wait_for_accept_cb = FALSE;

	/* kick off a timeout for this, since accept_cb will not get
	 * any indication that things have timed out.  This gets called
	 * every second until the timeout occurs. */
	g_object_ref(self);
	trk->conn = self;
	trk->recvpkt_num = self->current_recvpkt_num;
	trk->remaining = timeout;
	/* 1000 ms = 1 s */
	g_timeout_add(1000, recvpkt_timeout_sourcefunc, trk);

	/* if we have a packet already, call it; otherwise, the accept cb
	 * will call this later */
	maybe_call_recvpkt_cb(self);
	return;
    }

    /* if we already have a cached packet, we're done */
    if (maybe_call_recvpkt_cb(self))
	return;

    /* otherwise, schedule a recvpkt call */
    legacy_security_recvpkt(self->leg_handle, compat_recvpkt_callback,
	    self, (timeout==0)? -1 : timeout);
}

/* -- listen handling -- */

typedef struct compat_listen_info {
    char *auth;
    auth_connect_callback_t cb;
    gpointer cb_arg;
    auth_conf_getter_t conf;
    gpointer conf_arg;
    legacy_auth_quirk_t *quirk;
    AuthCompatConn *already_accepted_conn;
} compat_listen_info;

/* the legacy security API does not include any arg for its callback, but
 * also doesn't support listening on more than one auth at a time, so we
 * will just track this with a static variable. */
static compat_listen_info listen_info;

static void
compat_listen_callback(
    struct legacy_security_handle *handle,
    pkt_t *pkt)
{
    AuthCompatConn *self;
    g_assert(handle != NULL);

    AUTH_DEBUG(6, "compat_listen_callback(%p, %p)", handle, pkt);

    /* The legacy API will call this callback again, if an incoming packet
     * arrives and the previous protocol transaction has been finished.  When
     * this happens, we already have a connection, so instead of calling an
     * auth_connect_callback_t, we'll stuff the packet onto the recvpkt queue
     * and see if recvpkt has been called yet. */
    if (listen_info.already_accepted_conn) {
	self = listen_info.already_accepted_conn;

	/* replace the security handle.  We should free the old one here, but
	 * that triggers refcounting bugs in the legacy API, so we leak
	 * (slowly).. */
	self->leg_handle = handle;

	/* add the packet to the queue; note that the packet may be NULL at
	 * this point, in which case recvpkt will helpfully signal EOF to
	 * its caller. */
	AUTH_DEBUG(2, "compat_listen_callback queueing a nil packet for recvpkt");
	self->pkt_queue = g_slist_append(self->pkt_queue, pkt);
	maybe_call_recvpkt_cb(self);
	return;
    }

    /* create a new conn to wrap this handle */
    self = g_object_new(AUTH_COMPAT_CONN_TYPE, NULL);
    listen_info.already_accepted_conn = self;
    auth_conn_construct(AUTH_CONN(self), listen_info.auth,
		listen_info.conf, listen_info.conf_arg);
    self->leg_handle = handle;
    self->state = AUTH_CONNECTED;

    /* add the packet into the new handle, so recvpkt will
     * return it later */
    self->pkt_queue = g_slist_append(self->pkt_queue, pkt);

    listen_info.cb(listen_info.cb_arg, AUTH_CONN(self), AUTH_OK);
}

static char *
compat_listen_conf_getter(
    char *param,
    void *arg G_GNUC_UNUSED)
{
    return listen_info.conf(param, listen_info.conf_arg);
}

static void
conn_listen_impl(
    const char *auth,
    auth_connect_callback_t cb,
    gpointer cb_arg,
    auth_conf_getter_t conf,
    gpointer conf_arg,
    int argc G_GNUC_UNUSED,
    char **argv G_GNUC_UNUSED)
{
    const legacy_security_driver_t *driver;

    g_assert(!listen_info.auth);
    listen_info.auth = g_strdup(auth);
    listen_info.cb = cb;
    listen_info.cb_arg = cb_arg;
    listen_info.conf = conf;
    listen_info.conf_arg = conf_arg;
    listen_info.quirk = find_quirk(listen_info.auth);
    listen_info.already_accepted_conn = NULL;

    driver = legacy_security_getdriver(listen_info.auth);
    g_assert(driver != NULL);

    /* TODO process -udp= and -tcp= from argc/argv */

    legacy_security_accept(driver, compat_listen_conf_getter, 0, 1,
			    compat_listen_callback, NULL);
}

/* -- connect handling -- */

static void
compat_connect_callback(
    void *arg,
    legacy_security_handle_t *hdl,
    legacy_security_status_t status)
{
    AuthCompatConn *self = AUTH_COMPAT_CONN(arg);
    auth_connect_callback_t cb = self->connect_cb;

    AUTH_DEBUG(6, "compat_connect_callback(%p, %p, %d)", self, hdl, status);

    g_assert(self->state == AUTH_CONNECTING);
    self->leg_handle = hdl;
    self->state = AUTH_CONNECTED;
    self->connect_cb = NULL;

    /* note that legacy_security and auth statuses have the same values */
    cb(self->arg, AUTH_CONN(self), (auth_operation_status_t)status);
}

static char *
compat_conf_getter(
    char *param,
    void *arg)
{
    AuthConn *conn = AUTH_CONN(arg);
    return conn->conf(param, conn->conf_arg);
}

static void
conn_connect_impl(
    AuthConn *conn,
    const char *hostname,
    auth_connect_callback_t cb,
    gpointer cb_arg)
{
    AuthCompatConn *self = AUTH_COMPAT_CONN(conn);
    const legacy_security_driver_t *driver;

    /* must be in the NEW state to connect */
    g_assert(self->state == AUTH_NEW);
    self->state = AUTH_CONNECTING;

    /* set up for the callback */
    self->connect_cb = cb;
    self->arg = cb_arg;

    driver = legacy_security_getdriver(self->driver_name);
    g_assert(driver != NULL);
    legacy_security_connect(driver, hostname, compat_conf_getter,
			    compat_connect_callback, self, self);
}

/* -- sendpkt -- */

static gboolean
conn_sendpkt_impl(
    AuthConn *conn,
    pkt_t *packet)
{
    AuthCompatConn *self = AUTH_COMPAT_CONN(conn);
    g_assert(self->state == AUTH_CONNECTED);

    /* if packet is NULL, that means the transaction is over; this is signaled
     * to some legacy drivers using a special hack.. */
    if (packet == NULL) {
	if (self->quirk->need_close_packet_stream) {
	    self->leg_handle->driver->close_packet_stream(self->leg_handle);

	    /* and with this hack in place, a call to legacy_security_recvpkt
	     * will crash.  Instead, recvpkt will need to wait for a new invocation
	     * of the accept callback. */
	    self->recvpkt_should_wait_for_accept_cb = TRUE;
	}
	return TRUE;
    }

    return 0 == legacy_security_sendpkt(self->leg_handle, packet);
}

/* -- other methods -- */

static char *
conn_error_message_impl(
    AuthConn *conn)
{
    AuthCompatConn *self = AUTH_COMPAT_CONN(conn);
    AUTH_DEBUG(1, "conn error message: %s", legacy_security_geterror(self->leg_handle));
    return legacy_security_geterror(self->leg_handle);
}

static void
conn_set_error_message_impl(
    AuthConn *conn,
    char *errmsg)
{
    AuthCompatConn *self = AUTH_COMPAT_CONN(conn);
    return legacy_security_seterror(self->leg_handle, "%s", errmsg);
}

static char *
conn_get_authenticated_peer_name_impl(
    AuthConn *conn)
{
    AuthCompatConn *self = AUTH_COMPAT_CONN(conn);
    g_assert(self->state == AUTH_CONNECTED);

    return legacy_security_get_authenticated_peer_name(self->leg_handle);
}

static AuthStream *
conn_stream_listen_impl(
    AuthConn *conn)
{
    AuthCompatConn *self = AUTH_COMPAT_CONN(conn);
    legacy_security_stream_t *leg_stream;

    leg_stream = legacy_security_stream_server(self->leg_handle);
    if (!leg_stream)
	return NULL;

    return AUTH_STREAM(auth_compat_stream_new(self, leg_stream, AUTH_LISTENING));
}

static AuthStream *
conn_stream_connect_impl(
    AuthConn *conn,
    int id)
{
    AuthCompatConn *self = AUTH_COMPAT_CONN(conn);
    legacy_security_stream_t *leg_stream;

    leg_stream = legacy_security_stream_client(self->leg_handle, id);
    if (!leg_stream)
	return NULL;

    if (0 != legacy_security_stream_auth(leg_stream)) {
	/* copy the error message to the connection */
	legacy_security_seterror(self->leg_handle, "%s",
		legacy_security_geterror(leg_stream));
	security_stream_close(leg_stream);
	return NULL;
    }

    return AUTH_STREAM(auth_compat_stream_new(self, leg_stream, AUTH_CONNECTED));
}

static void
conn_class_init(
    AuthCompatConnClass * selfc)
{
    AuthConnClass *klass = AUTH_CONN_CLASS(selfc);
    GObjectClass *goc = G_OBJECT_CLASS(selfc);

    goc->finalize = conn_finalize_impl;

    klass->construct = conn_construct_impl;
    klass->connect = conn_connect_impl;
    klass->listen = conn_listen_impl;
    klass->error_message = conn_error_message_impl;
    klass->set_error_message = conn_set_error_message_impl;
    klass->get_authenticated_peer_name = conn_get_authenticated_peer_name_impl;
    klass->sendpkt = conn_sendpkt_impl;
    klass->recvpkt = conn_recvpkt_impl;
    klass->stream_listen = conn_stream_listen_impl;
    klass->stream_connect = conn_stream_connect_impl;

    parent_conn_class = g_type_class_peek_parent(selfc);
}

GType /* not static - referenced in auth.c */
auth_compat_conn_get_type (void)
{
    static GType type = 0;

    if G_UNLIKELY(type == 0) {
        static const GTypeInfo info = {
            sizeof (AuthCompatConnClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) conn_class_init,
            (GClassFinalizeFunc) NULL,
            NULL /* class_data */,
            sizeof (AuthCompatConn),
            0 /* n_preallocs */,
            (GInstanceInitFunc) conn_init_impl,
            NULL
        };

        type = g_type_register_static (AUTH_CONN_TYPE, "AuthCompatConn", &info, 0);
    }

    return type;
}

/* TODO - integrate */
#if 0
#if defined(USE_REUSEADDR)
    const int on = 1;
    int r;
#endif
	/*
	 * Allow us to directly bind to a udp port for debugging.
	 * This may only apply to some security types.
	 */
	else if (strncmp(argv[i], "-udp=", strlen("-udp=")) == 0) {
#ifdef WORKING_IPV6
	    struct sockaddr_in6 sin;
#else
	    struct sockaddr_in sin;
#endif

	    argv[i] += strlen("-udp=");
#ifdef WORKING_IPV6
	    in = out = socket(AF_INET6, SOCK_DGRAM, 0);
#else
	    in = out = socket(AF_INET, SOCK_DGRAM, 0);
#endif
	    if (in < 0) {
		error(_("can't create dgram socket: %s\n"), strerror(errno));
		/*NOTREACHED*/
	    }
#ifdef USE_REUSEADDR
	    r = setsockopt(in, SOL_SOCKET, SO_REUSEADDR,
		(void *)&on, (socklen_t_equiv)sizeof(on));
	    if (r < 0) {
		dbprintf(_("amandad: setsockopt(SO_REUSEADDR) failed: %s\n"),
			  strerror(errno));
	    }
#endif

#ifdef WORKING_IPV6
	    sin.sin6_family = (sa_family_t)AF_INET6;
	    sin.sin6_addr = in6addr_any;
	    sin.sin6_port = (in_port_t)htons((in_port_t)atoi(argv[i]));
#else
	    sin.sin_family = (sa_family_t)AF_INET;
	    sin.sin_addr.s_addr = INADDR_ANY;
	    sin.sin_port = (in_port_t)htons((in_port_t)atoi(argv[i]));
#endif
	    if (bind(in, (struct sockaddr *)&sin, (socklen_t_equiv)sizeof(sin)) < 0) {
		error(_("can't bind to port %d: %s\n"), atoi(argv[i]),
		    strerror(errno));
		/*NOTREACHED*/
	    }
	}
	/*
	 * Ditto for tcp ports.
	 */
	else if (strncmp(argv[i], "-tcp=", strlen("-tcp=")) == 0) {
#ifdef WORKING_IPV6
	    struct sockaddr_in6 sin;
#else
	    struct sockaddr_in sin;
#endif
	    int sock;
	    socklen_t_equiv n;

	    argv[i] += strlen("-tcp=");
#ifdef WORKING_IPV6
	    sock = socket(AF_INET6, SOCK_STREAM, 0);
#else
	    sock = socket(AF_INET, SOCK_STREAM, 0);
#endif
	    if (sock < 0) {
		error(_("can't create tcp socket: %s\n"), strerror(errno));
		/*NOTREACHED*/
	    }
#ifdef USE_REUSEADDR
	    r = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
		(void *)&on, (socklen_t_equiv)sizeof(on));
	    if (r < 0) {
		dbprintf(_("amandad: setsockopt(SO_REUSEADDR) failed: %s\n"),
			  strerror(errno));
	    }
#endif
#ifdef WORKING_IPV6
	    sin.sin6_family = (sa_family_t)AF_INET6;
	    sin.sin6_addr = in6addr_any;
	    sin.sin6_port = (in_port_t)htons((in_port_t)atoi(argv[i]));
#else
	    sin.sin_family = (sa_family_t)AF_INET;
	    sin.sin_addr.s_addr = INADDR_ANY;
	    sin.sin_port = (in_port_t)htons((in_port_t)atoi(argv[i]));
#endif
	    if (bind(sock, (struct sockaddr *)&sin, (socklen_t_equiv)sizeof(sin)) < 0) {
		error(_("can't bind to port %d: %s\n"), atoi(argv[i]),
		    strerror(errno));
		/*NOTREACHED*/
	    }
	    listen(sock, 10);
	    n = (socklen_t_equiv)sizeof(sin);
	    in = out = accept(sock, (struct sockaddr *)&sin, &n);
	}






/* userid checking */

#ifndef SINGLE_USERID
    if (geteuid() == 0) {
	if (strcasecmp(auth, "krb5") != 0) {
	    struct passwd *pwd;
	    /* lookup our local user name */
	    if ((pwd = getpwnam(CLIENT_LOGIN)) == NULL) {
		error(_("getpwnam(%s) failed."), CLIENT_LOGIN);
	    }

	    if (pwd->pw_uid != 0) {
		error(_("'amandad' must be run as user '%s' when using '%s' authentication"),
		      CLIENT_LOGIN, auth);
	    }
	}
    } else {
	if (strcasecmp(auth, "krb5") == 0) {
	    error(_("'amandad' must be run as user 'root' when using 'krb5' authentication"));
	}
    }
#endif

    /* krb5 require the euid to be 0 */
    if (strcasecmp(auth, "krb5") == 0) {
	seteuid((uid_t)0);
    }


#endif
