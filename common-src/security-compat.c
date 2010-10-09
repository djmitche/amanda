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
#include "security-compat.h"
#include "arglist.h"
#include "event.h"

/*
 * Implement the legacy security API in terms of the new Auth API
 */

void security_connect(
    const security_driver_t *driver,
    const char *hostname,
    char *(*conf_fn)(char *, void *),
    void (*fn)(void *, security_handle_t *, security_status_t),
    void *arg,
    void *datap)
{
    AuthConn *conn = auth_conn_new((char *)driver, (auth_conf_getter_t)conf_fn, datap);
    if (!conn) {
	fn(arg, NULL, AUTH_ERROR);
	return;
    }
    auth_conn_connect(conn, hostname, (auth_connect_callback_t)fn, arg);
}

/* the legacy security API has two functions to "close" a connection.  Neither
 * really does close the connection (all comments to the contrary .. the legacy
 * API is *full* of outright lies, misleading names, and so on.  Be warned).
 * In auth-compat, we ignore security_close_connection, which never really did
 * anything that interesting, and screwed up the reference counting for
 * tcp_conns.  However, security_close signals, after a fashion, that the protocol
 * transaction is over.  It also sorta-kinda frees the security handle, but there
 * are double-free problems there, too - security_close is called in multiple places
 * on the same handle.
 *
 * So we implement security_close to just signal the end of the transaction, but
 * not free the connection.  This leaks, but it's better than tracing down all the
 * fragile free/refcount bugs!
 */
void security_close(
    security_handle_t *handle)
{
    AuthConn *conn = handle;

    g_debug("security_close(%p)", handle);

    /* signal the end of the transaction */
    auth_conn_sendpkt(conn, NULL);
}

typedef struct recvpkt_stuff {
    void *arg;
    void (*fn)(void *, pkt_t *, security_status_t);
} recvpkt_stuff;

static void
legacy_recvpkt_cb(
    gpointer arg,
    AuthConn *conn G_GNUC_UNUSED,
    pkt_t *pkt,
    auth_operation_status_t status)
{
    recvpkt_stuff stuff = *(recvpkt_stuff *)arg;
    g_free(arg);

    /* if we got an EOF indication from the AuthConn, translate it into an
     * error for the legacy API.  The error message is already set appropriately  */
    if (pkt == NULL && status == AUTH_OK)
	status = AUTH_ERROR;

    /* note that auth_operation_status_t has the same values as security_status_t */
    stuff.fn(stuff.arg, pkt, status);
}

void security_recvpkt(
    security_handle_t *handle,
    void (*fn)(void *, pkt_t *, security_status_t),
    void *arg,
    int timeout)
{
    AuthConn *conn = handle;
    recvpkt_stuff *stuff = g_new0(recvpkt_stuff, 1);
    stuff->fn = fn;
    stuff->arg = arg;
    auth_conn_recvpkt(conn, legacy_recvpkt_cb, stuff, timeout);
}

void security_seterror(
    security_handle_t *handle,
    const char *fmt, ...)
{
    static char buf[1024];
    va_list argp;

    /* TODO: this basically gets ignored for AuthLocalConn (same for stream_seterror) */

    assert(handle->errmsg != NULL);
    arglist_start(argp, fmt);
    g_vsnprintf(buf, SIZEOF(buf), fmt, argp);
    arglist_end(argp);
    handle->errmsg = newstralloc(handle->errmsg, buf);
}

void security_stream_seterror(
    security_stream_t *stream,
    const char *fmt, ...)
{
    static char buf[1024];
    va_list argp;

    arglist_start(argp, fmt);
    g_vsnprintf(buf, SIZEOF(buf), fmt, argp);
    arglist_end(argp);
    stream->errmsg = newstralloc(stream->errmsg, buf);
}

typedef struct read_stuff {
    void *arg;
    void (*fn)(void *, void *, ssize_t size);
} read_stuff;

static void
legacy_read_callback(
    gpointer arg,
    AuthStream *stream G_GNUC_UNUSED,
    gpointer buf,
    gsize size)
{
    read_stuff stuff = *(read_stuff *)arg;
    g_free(arg);

    stuff.fn(stuff.arg, buf, size);
}

void security_stream_read(
    security_stream_t *stream,
    void (*fn)(void *, void *, ssize_t),
    void *arg)
{
    read_stuff *stuff = g_new0(read_stuff, 1);
    stuff->fn = fn;
    stuff->arg = arg;
    auth_stream_read(stream, legacy_read_callback, stuff);
}

typedef struct read_sync_stuff {
    ssize_t size;
    void **buf;
    event_handle_t *evt;
} read_sync_stuff;

static void
legacy_read_sync_callback(
    gpointer arg,
    AuthStream *stream G_GNUC_UNUSED,
    gpointer buf,
    gsize size)
{
    read_sync_stuff *stuff = (read_sync_stuff *)arg;

    stuff->size = size;
    if (size > 0) {
	*(stuff->buf) = g_malloc(size);
	memcpy(*(stuff->buf), buf, size);
    }
    event_wakeup(100092);
    event_release(stuff->evt);
}

static void
wait_fn(
    void *arg G_GNUC_UNUSED)
{
}

ssize_t security_stream_read_sync(
    security_stream_t *stream,
    void **buf)
{
    read_sync_stuff stuff = { 0, buf, NULL };
    stuff.evt = event_register(100092, EV_WAIT, wait_fn, NULL);

    auth_stream_read(stream, legacy_read_sync_callback, &stuff);
    event_wait(stuff.evt);
    return stuff.size;
}
