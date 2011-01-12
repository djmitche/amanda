#ifndef __AM_XBUF_CACHE_H__
#define __AM_XBUF_CACHE_H__

#include <glib.h>

/**
 * The structure for an xbuf_cache
 *
 * @mutex: the mutex for this cache
 * @size: the size (a power of 2) allocated to each xbuf in this cache
 * @list: a pointer to the pool of xbufs currently available
 * @max_entries: the maximum number of xbufs this cache should be holding
 * @nr_entries: the current number of xbufs this cache is currently holding
 */

struct xbuf;

struct xbuf_cache {
    GMutex *mutex;
    gsize size;
    struct xbuf *list;
    gint max_entries, nr_entries;
};

/*
 * Prototypes
 */

struct xbuf *xbuf_cache_get(gsize size);
void xbuf_cache_put(struct xbuf *xbuf);

#endif /* __AM_XBUF_CACHE_H__ */
