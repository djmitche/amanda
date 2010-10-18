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
#include "auth.h"
#include "conffile.h"
#include "glib-util.h"

/*
 * AuthConn
 */

static GObjectClass *parent_conn_class = NULL;

static void
auth_conn_init(
    AuthConn *self)
{
    self->errmsg = g_strdup("(no error)");
}

static void
conn_finalize_impl(
    GObject * obj_self)
{
    AuthConn *self = AUTH_CONN(obj_self);

    if (self->errmsg) {
	g_free(self->errmsg);
    }

    G_OBJECT_CLASS(parent_conn_class)->finalize(obj_self);
}

static void
conn_construct_impl(
    AuthConn *conn,
    const char *auth G_GNUC_UNUSED,
    auth_conf_getter_t conf,
    gpointer conf_arg)
{
    conn->conf = conf;
    conn->conf_arg = conf_arg;
}

static char *
conn_error_message_impl(
    AuthConn *conn)
{
    return conn->errmsg?
	conn->errmsg : "(no error)";
}

static void
conn_set_error_message_impl(
    AuthConn *conn,
    char *errmsg)
{
    g_free(conn->errmsg);
    conn->errmsg = g_strdup(errmsg);
}

static void
auth_conn_class_init(
    AuthConnClass * klass)
{
    GObjectClass *goc = (GObjectClass*) klass;

    goc->finalize = conn_finalize_impl;
    klass->construct = conn_construct_impl;
    klass->error_message = conn_error_message_impl;
    klass->set_error_message = conn_set_error_message_impl;

    parent_conn_class = g_type_class_peek_parent(goc);
}

GType
auth_conn_get_type(void)
{
    static GType type = 0;

    if G_UNLIKELY(type == 0) {
        static const GTypeInfo info = {
            sizeof (AuthConnClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) auth_conn_class_init,
            (GClassFinalizeFunc) NULL,
            NULL /* class_data */,
            sizeof (AuthConn),
            0 /* n_preallocs */,
            (GInstanceInitFunc) auth_conn_init,
            NULL
        };

        type = g_type_register_static (G_TYPE_OBJECT, "AuthConn", &info,
                                       (GTypeFlags)G_TYPE_FLAG_ABSTRACT);
    }

    return type;
}

/* method stubs */

void auth_conn_construct(
    AuthConn *conn,
    const char *auth,
    auth_conf_getter_t conf,
    gpointer conf_arg)
{
    g_assert(conn != NULL);
    g_assert(auth != NULL);
    g_assert(conf != NULL);

    AUTH_DEBUG(6, "auth_conn_construct(%p, \"%s\", conf=%p, conf_arg=%p)",
	    conn, auth, conf, conf_arg);

    AUTH_CONN_GET_CLASS(conn)->construct(conn, auth, conf, conf_arg);
}

void
auth_conn_connect(
    AuthConn *conn,
    const char *hostname,
    auth_connect_callback_t op_cb,
    gpointer op_cb_arg)
{
    g_assert(conn != NULL);
    g_assert(hostname != NULL);
    g_assert(op_cb != NULL);

    AUTH_DEBUG(6, "auth_conn_connect(%p, \"%s\", cb=%p, arg=%p)", conn,
	    hostname, op_cb, op_cb_arg);

    AUTH_CONN_GET_CLASS(conn)->connect(conn, hostname, op_cb, op_cb_arg);
}

char *
auth_conn_get_authenticated_peer_name(
    AuthConn *conn)
{
    g_assert(conn != NULL);

    AUTH_DEBUG(6, "auth_conn_get_authenticated_peer_name(%p)", conn);

    return AUTH_CONN_GET_CLASS(conn)->get_authenticated_peer_name(conn);
}

char *
auth_conn_error_message(
    AuthConn *conn)
{
    g_assert(conn != NULL);

    AUTH_DEBUG(6, "auth_conn_error_message(%p)", conn);

    return AUTH_CONN_GET_CLASS(conn)->error_message(conn);
}

void
auth_conn_set_error_message(
    AuthConn *conn,
    char *errmsg)
{
    g_assert(conn != NULL);
    g_assert(errmsg != NULL);

    AUTH_DEBUG(6, "auth_conn_set_error_message(%p, \"%s\")", conn, errmsg);

    return AUTH_CONN_GET_CLASS(conn)->set_error_message(conn, errmsg);
}

AuthProto *
auth_conn_proto_new(
    AuthConn *conn)
{
    g_assert(conn != NULL);

    AUTH_DEBUG(6, "auth_conn_proto_new(%p)", conn);

    return AUTH_CONN_GET_CLASS(conn)->proto_new(conn);
}

AuthStream *
auth_conn_stream_listen(
    AuthConn *conn)
{
    g_assert(conn != NULL);

    AUTH_DEBUG(6, "auth_conn_stream_listen(%p)", conn);

    return AUTH_CONN_GET_CLASS(conn)->stream_listen(conn);
}

AuthStream *
auth_conn_stream_connect(
    AuthConn *conn,
    int id)
{
    g_assert(conn != NULL);

    AUTH_DEBUG(6, "auth_conn_stream_connect(%p, %d)", conn, id);

    return AUTH_CONN_GET_CLASS(conn)->stream_connect(conn, id);
}

/*
 * AuthProto
 */

static GObjectClass *parent_proto_class = NULL;

static void
auth_proto_init(
    AuthProto *self)
{
    self->errmsg = g_strdup("(no error)");
}

static void
proto_finalize_impl(
    GObject * obj_self)
{
    AuthProto *self = AUTH_PROTO(obj_self);

    if (self->conn) {
	g_object_unref(self->conn);
	self->conn = NULL;
    }

    if (self->errmsg) {
	g_free(self->errmsg);
    }

    G_OBJECT_CLASS(parent_proto_class)->finalize(obj_self);
}

static void
proto_construct_impl(
    AuthProto *self,
    AuthConn *conn)
{
    g_object_ref(conn);
    self->conn = conn;
}

static char *
proto_error_message_impl(
    AuthProto *proto)
{
    return proto->errmsg?
	proto->errmsg : "(no error)";
}

static void
auth_proto_class_init(
    AuthProtoClass * klass)
{
    GObjectClass *goc = (GObjectClass*) klass;

    goc->finalize = proto_finalize_impl;
    klass->error_message = proto_error_message_impl;
    klass->construct = proto_construct_impl;

    parent_proto_class = g_type_class_peek_parent(goc);
}

GType
auth_proto_get_type(void)
{
    static GType type = 0;

    if G_UNLIKELY(type == 0) {
        static const GTypeInfo info = {
            sizeof (AuthProtoClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) auth_proto_class_init,
            (GClassFinalizeFunc) NULL,
            NULL /* class_data */,
            sizeof (AuthProto),
            0 /* n_preallocs */,
            (GInstanceInitFunc) auth_proto_init,
            NULL
        };

        type = g_type_register_static (G_TYPE_OBJECT, "AuthProto", &info,
                                       (GTypeFlags)G_TYPE_FLAG_ABSTRACT);
    }

    return type;
}

/* method stubs */

void
auth_proto_construct(
    AuthProto *proto,
    AuthConn *conn)
{
    g_assert(proto != NULL);
    g_assert(conn != NULL);

    AUTH_DEBUG(6, "auth_proto_construct(%p, %p)", proto, conn);

    return AUTH_PROTO_GET_CLASS(proto)->construct(proto, conn);
}

char *
auth_proto_error_message(
    AuthProto *proto)
{
    g_assert(proto != NULL);

    AUTH_DEBUG(6, "auth_proto_error_message(%p)", proto);

    return AUTH_PROTO_GET_CLASS(proto)->error_message(proto);
}

gboolean
auth_proto_sendpkt(
    AuthProto *proto,
    pkt_t *packet)
{
    g_assert(proto != NULL);

    AUTH_DEBUG(6, "auth_proto_sendpkt(%p, pkt=%p)", proto, packet);

    return AUTH_PROTO_GET_CLASS(proto)->sendpkt(proto, packet);
}

void
auth_proto_recvpkt(
    AuthProto *proto,
    auth_recvpkt_callback_t recvpkt_cb,
    gpointer recvpkt_cb_arg,
    int timeout)
{
    g_assert(proto != NULL);
    g_assert(recvpkt_cb != NULL);

    AUTH_DEBUG(6, "auth_proto_recvpkt(%p, cb=%p, arg=%p, timeout=%d)", proto,
	    recvpkt_cb, recvpkt_cb_arg, timeout);

    return AUTH_PROTO_GET_CLASS(proto)->recvpkt(proto, recvpkt_cb,
		    recvpkt_cb_arg, timeout);
}

/*
 * AuthStream
 */

static GObjectClass *parent_stream_class = NULL;

static void
auth_stream_init(
    AuthStream *self)
{
    self->errmsg = g_strdup("(no error)");
}

static void
stream_finalize_impl(
    GObject * obj_self)
{
    AuthStream *self = AUTH_STREAM(obj_self);

    if (self->conn) {
	g_object_unref(self->conn);
	self->conn = NULL;
    }

    if (self->errmsg) {
	g_free(self->errmsg);
    }

    G_OBJECT_CLASS(parent_stream_class)->finalize(obj_self);
}

static void
stream_construct_impl(
    AuthStream *self,
    AuthConn *conn)
{
    g_object_ref(conn);
    self->conn = conn;
}

static char *
stream_error_message_impl(
    AuthStream *stream)
{
    return stream->errmsg?
	stream->errmsg : "(no error)";
}

static void
auth_stream_class_init(
    AuthStreamClass * klass)
{
    GObjectClass *goc = (GObjectClass*) klass;

    goc->finalize = stream_finalize_impl;
    klass->error_message = stream_error_message_impl;
    klass->construct = stream_construct_impl;

    parent_stream_class = g_type_class_peek_parent(goc);
}

GType
auth_stream_get_type(void)
{
    static GType type = 0;

    if G_UNLIKELY(type == 0) {
        static const GTypeInfo info = {
            sizeof (AuthStreamClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) auth_stream_class_init,
            (GClassFinalizeFunc) NULL,
            NULL /* class_data */,
            sizeof (AuthStream),
            0 /* n_preallocs */,
            (GInstanceInitFunc) auth_stream_init,
            NULL
        };

        type = g_type_register_static (G_TYPE_OBJECT, "AuthStream", &info,
                                       (GTypeFlags)G_TYPE_FLAG_ABSTRACT);
    }

    return type;
}

/* method stubs */

void
auth_stream_construct(
    AuthStream *stream,
    AuthConn *conn)
{
    g_assert(stream != NULL);
    g_assert(conn != NULL);

    AUTH_DEBUG(6, "auth_stream_construct(%p, %p)", stream, conn);

    return AUTH_STREAM_GET_CLASS(stream)->construct(stream, conn);
}

gboolean
auth_stream_accept(
    AuthStream *stream)
{
    g_assert(stream != NULL);

    AUTH_DEBUG(6, "auth_stream_accept(%p)", stream);

    return AUTH_STREAM_GET_CLASS(stream)->accept(stream);
}

char *
auth_stream_error_message(
    AuthStream *stream)
{
    g_assert(stream != NULL);

    AUTH_DEBUG(6, "auth_stream_error_message(%p)", stream);

    return AUTH_STREAM_GET_CLASS(stream)->error_message(stream);
}

gboolean
auth_stream_write(
    AuthStream *stream,
    gconstpointer buf,
    size_t size)
{
    g_assert(stream != NULL);
    g_assert(buf != NULL);

    AUTH_DEBUG(6, "auth_stream_write(%p, buf=%p, size=%zu)", stream, buf, size);

    return AUTH_STREAM_GET_CLASS(stream)->write(stream, buf, size);
}

void
auth_stream_read(
    AuthStream *stream,
    auth_read_callback_t read_cb,
    gpointer read_cb_arg)
{
    g_assert(stream != NULL);
    g_assert(read_cb != NULL);

    AUTH_DEBUG(6, "auth_stream_read(%p, cb=%p, arg=%p)", stream,
	    read_cb, read_cb_arg);

    return AUTH_STREAM_GET_CLASS(stream)->read(stream,
	    read_cb, read_cb_arg);
}

/*
 * Constructor
 */

GType auth_compat_conn_get_type(void);

static struct auth_impl {
    char *name;
    GType (*get_type)(void);
    AuthConnClass *klass;
} auth_impls[] = {
#ifdef BSD_SECURITY
    { "bsd", auth_compat_conn_get_type, NULL },
#endif
#ifdef KRB5_SECURITY
    { "krb5", auth_compat_conn_get_type, NULL },
#endif
#ifdef RSH_SECURITY
    { "rsh", auth_compat_conn_get_type, NULL },
#endif
#ifdef SSH_SECURITY
    { "ssh", auth_compat_conn_get_type, NULL },
#endif
#ifdef BSDTCP_SECURITY
    { "bsdtcp", auth_compat_conn_get_type, NULL },
#endif
#ifdef BSDUDP_SECURITY
    { "bsdudp", auth_compat_conn_get_type, NULL },
#endif
    { "local", auth_compat_conn_get_type, NULL },
    { NULL, NULL, NULL },
};

static struct auth_impl *
get_auth_impl(const char *auth)
{
    struct auth_impl *iter;

    /* make sure glib is initialized */
    glib_init();

    for (iter = auth_impls; iter->name; iter++) {
	if (0 == g_ascii_strcasecmp(auth, iter->name)) {
	    return iter;
	}
    }

    return NULL;
}

AuthConn *
auth_conn_new(
    const char *auth,
    auth_conf_getter_t conf,
    gpointer conf_arg)
{
    struct auth_impl *impl = get_auth_impl(auth);
    GType type;
    AuthConn *conn;

    if (!impl) {
	g_warning("auth-compat driver '%s' not recognized", auth);
	/* TODO: return a 'NULL' security object with proper error */
	return NULL;
    }

    type = impl->get_type();
    conn = AUTH_CONN(g_object_new(type, NULL));
    auth_conn_construct(conn, auth, conf, conf_arg);

    return conn;
}

void
auth_listen(
    const char *auth,
    auth_proto_callback_t cb,
    gpointer cb_arg,
    auth_conf_getter_t conf,
    gpointer conf_arg,
    int argc,
    char **argv)
{
    struct auth_impl *impl = get_auth_impl(auth);

    if (!impl) {
	g_critical("auth-compat driver '%s' not recognized", auth);
	exit(1);
    }

    if (!impl->klass) {
	/* find the corresponding class, and keep a ref to it permanently */
	GType type = impl->get_type();
	impl->klass = g_type_class_ref(type);
    }

    /* and call the listen method on that class as a static method */
    g_assert(impl->klass->listen != NULL);
    impl->klass->listen(auth, cb, cb_arg, conf, conf_arg, argc, argv);
}
