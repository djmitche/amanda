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

#ifndef SECURITY_COMPAT_H
#define	SECURITY_COMPAT_H

#include "packet.h"
#include "auth.h"

#define security_status_t auth_operation_status_t
#define S_OK AUTH_OK
#define S_TIMEOUT AUTH_TIMEOUT
#define S_ERROR AUTH_ERROR

/* a "driver" is just the auth name */
typedef const char security_driver_t;
#define security_getdriver(d) ((security_driver_t *)(d))

typedef AuthConn security_handle_t;

/* temporarily allow transitions back to the legacy API during rewrite */
#define security_handle_from_conn(conn) \
    ((security_handle_t *)AUTH_CONN((conn)))

void security_connect(
    const security_driver_t *driver,
    const char *hostname,
    char *(*conf_fn)(char *, void *),
    void (*fn)(void *, security_handle_t *, security_status_t),
    void *arg,
    void *datap);

#define	security_get_authenticated_peer_name(handle) \
    auth_conn_get_authenticated_peer_name((handle))

void security_close(security_handle_t *handle);

#define	security_sendpkt(handle, pkt)		\
    auth_conn_sendpkt((handle), (pkt))

void security_recvpkt(
    security_handle_t *handle,
    void (*fn)(void *, pkt_t *, security_status_t),
    void *arg,
    int timeout);

#define	security_recvpkt_cancel(handle)		not_used

#define	security_geterror(handle)	\
    auth_conn_error_message((handle))

void security_seterror(security_handle_t *, const char *, ...)
     G_GNUC_PRINTF(2,3);

typedef AuthStream security_stream_t;

#define	security_stream_geterror(stream)	\
    auth_stream_error_message((stream))

/* Sets the string that security_stream_geterror() returns. */
void security_stream_seterror(security_stream_t *, const char *, ...)
     G_GNUC_PRINTF(2,3);

#define	security_stream_server(handle)	\
    auth_conn_stream_listen((handle))

#define	security_stream_accept(stream)		\
    auth_stream_accept((stream))

#define	security_stream_client(handle, id)	\
    auth_conn_stream_connect((handle), (id))

#define security_stream_close(stream) \
    g_object_unref((stream));

/* No-op */
#define	security_stream_auth(stream)		1

#define	security_stream_id(stream)		\
    (stream)->id

#define	security_stream_write(stream, buf, size)	\
    auth_stream_write((stream), (buf), (size))

void security_stream_read(
    security_stream_t *stream,
    void (*fn)(void *, void *, ssize_t),
    void *arg);

ssize_t security_stream_read_sync(
    security_stream_t *stream,
    void **buf);

#define	security_stream_read_cancel(stream)		not_used

#define security_close_connection(handle, hostname) \
    g_debug("ignoring security_close_connection(%p)", handle);

#endif	/* SECURITY_COMPAT_H */
