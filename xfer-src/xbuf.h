#ifndef __AM_XBUF_H__
#define __AM_XBUF_H__

#include <glib.h>

/**
 * The structure holding a buffer.
 *
 * @data: a pointer to the allocated memory, @size or more
 * @bufsize: the size of the buffer
 * @size: the size requested by the user
 * @next: a pointer to the next xbuf entry when this xbuf is in a cache pool
 */

struct xbuf {
    gpointer data;
    size_t bufsize;
    size_t size;
    struct xbuf *next;
};

/**
 * The structure for an xbuf_cache
 *
 * @mutex: the mutex for this cache
 * @size: the size allocated to each xbuf in this cache
 * @list: a pointer to the pool of xbufs currently available
 * @nr_entries: the current number of xbufs this cache is currently holding
 */

struct xbuf_cache {
    GMutex *mutex;
    size_t size;
    struct xbuf *list;
    guint nr_entries;
};

/**
 * Create an xbuf_cache. Optionally populate it with an initial number of xbufs.
 *
 * @param size: the size of buffers allocated to xbufs
 * @param nr_entries: the initial number of xbufs allocated (may be 0)
 * @returns: the generated cache.
 */

struct xbuf_cache *xbuf_cache_create(size_t size, guint nr_entries);

/**
 * Destroy an xbuf_cache. All xbufs within it will be destroyed as well.
 *
 * @param victim: the cache to destroy.
 */

void xbuf_cache_destroy(struct xbuf_cache *victim);

/**
 * Get an xbuf from a cache. Grab one from the pool if it is not empty,
 * otherwise create a new one. The visible size may be less than what is
 * allocated to the xbuf (0 means default size).
 *
 * @param cache: the cache
 * @param size: the size of the xbuf to get (optional)
 * @size: the requested size
 */

struct xbuf *xbuf_cache_get(struct xbuf_cache *cache, size_t size);

/**
 * Given an xbuf as an argument, put it into a cache. The size of the allocated
 * buffer of the xbuf and the xbuf_cache MUST match.
 *
 * @param cache: the cache to put the xbuf into
 * @param xbuf: pointer to the xbuf given back to us
 */


void xbuf_cache_put(struct xbuf_cache *cache, struct xbuf *xbuf);

/**
 * Change the vision of an xbuf's size. The new size MUST be less than the
 * current size.
 *
 * @param xbuf: the xbuf to modify
 * @param newsize: the new size
 */

void xbuf_shrink_size(struct xbuf *xbuf, size_t newsize);

#endif /* __AM_XBUF_H__ */
