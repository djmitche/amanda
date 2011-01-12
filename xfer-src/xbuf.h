#ifndef __AM_XBUF_H__
#define __AM_XBUF_H__

#include <glib.h>

#include "xbuf_cache.h"

/**
 * The structure holding a buffer.
 *
 * @data: a pointer to the allocated memory, @size or more
 * @size: the size requested by the user
 * @next: a pointer to the next xbuf entry when this xbuf is in a cache pool
 * @cache: the cache this xbuf came from, NULL if oversized
 */

struct xbuf {
    gpointer data;
    size_t size;
    struct xbuf *next;
    struct xbuf_cache *cache;
};

#endif /* __AM_XBUF_H__ */
