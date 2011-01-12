#ifndef __AM_XBUF_H__
#define __AM_XBUF_H__

#include <glib.h>

/**
 * The structure holding a buffer.
 *
 * @data: a pointer to the allocated memory, @size or more
 * @size: the size requested by the user
 * @next: a pointer to the next xbuf entry when this xbuf is in a cache pool
 * @cache: the cache this xbuf came from, NULL if oversized
 */

struct xbuf_cache;

struct xbuf {
    gpointer data;
    size_t size;
    struct xbuf *next;
    struct xbuf_cache *cache;
};

/*
 * Prototypes
 *
 * xbuf_cache_get(): get an xbuf from the caches;
 * xbuf_cache_put(): handle back an xbuf;
 * xbuf_clamp_size(): clamp the size of an xbuf.
 */

struct xbuf *xbuf_cache_get(size_t size);
void xbuf_cache_put(struct xbuf *xbuf);
void xbuf_clamp_size(struct xbuf *xbuf, size_t newsize);

#endif /* __AM_XBUF_H__ */
