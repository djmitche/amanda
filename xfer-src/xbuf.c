#include "xbuf.h"

/*
 * xbuf and xbuf_cache implementation
 */

/*
 * The xbuf pool implementation is really dumb, but it's enough for our purpose.
 * Each xbuf has a member named "list", which is a pointer to an xbuf... And so
 * is the "head" of the list.
 */

static void push_xbuf(struct xbuf_cache *cache, struct xbuf *xbuf)
{
    xbuf->next = cache->list;
    cache->list = xbuf;
    cache->nr_entries++;
}

static struct xbuf *pop_xbuf(struct xbuf_cache *cache)
{
    struct xbuf *ret = cache->list;
    cache->list = ret->next;
    cache->nr_entries--;
    return ret;
}

/*
 * Each cache is initialized with its list pointer set to NULL. As buffers are
 * pushed into it, it will not be the case. When the last xbuf is popped out of
 * it, it will become NULL again. Checking whether a cache is empty is therefore
 * trivial...
 */

#define cache_is_empty(cache) ((cache)->list == NULL)

/**
 * Macro to destroy an xbuf
 *
 * @xbuf: the victim
 */

#define xbuf_destroy(xbuf) do { \
    g_free((xbuf)->data); \
    g_free((xbuf)); \
} while (0)

/**
 * Create a struct xbuf with a given allocated size. The size member of the
 * structure is NOT initialized here (this is the role of xbuf_cache_get()).
 *
 * @param bufsize: the size of the allocated buffer
 * @returns: the newly allocated xbuf
 */

static struct xbuf *xbuf_create(size_t bufsize)
{
    gpointer data = g_malloc(bufsize);
    struct xbuf *xbuf = g_malloc(sizeof(struct xbuf));

    xbuf->data = data;
    xbuf->bufsize = bufsize;

    return xbuf;
}

/*
 * PUBLICLY AVAILABLE FUNCTIONS
 */

/**
 * Create an xbuf_cache. Optionally populate it with an initial number of xbufs.
 *
 * @param size: the size of buffers allocated to xbufs
 * @param nr_entries: the initial number of xbufs allocated (may be 0)
 * @returns: the generated cache.
 */

struct xbuf_cache *xbuf_cache_create(size_t size, guint nr_entries)
{
    struct xbuf_cache *cache = g_malloc(sizeof(struct xbuf_cache));

    cache->mutex = g_mutex_new();
    cache->list = NULL;
    cache->size = size;
    cache->nr_entries = 0;

    while (nr_entries--) {
        struct xbuf *xbuf = xbuf_create(size);
        push_xbuf(cache, xbuf);
    }

    return cache;
}

/**
 * Destroy an xbuf_cache. All xbufs within it will be destroyed as well.
 *
 * @param victim: the cache to destroy.
 */

void xbuf_cache_destroy(struct xbuf_cache *victim)
{
    struct xbuf *xbuf;
    guint entries = victim->nr_entries, destroyed = 0;

    while (!cache_is_empty(victim)) {
        xbuf = pop_xbuf(victim);
        xbuf_destroy(xbuf);
        destroyed++;
    }

    g_assert(entries == destroyed);

    g_free(victim);
}

/**
 * Get an xbuf from a cache. Grab one from the pool if it is not empty,
 * otherwise create a new one. The visible size may be less than what is
 * allocated to the xbuf (0 means default size).
 *
 * @param cache: the cache
 * @param size: the size of the xbuf to get (optional)
 * @size: the requested size
 */

struct xbuf *xbuf_cache_get(struct xbuf_cache *cache, size_t size)
{
    struct xbuf *ret;

    g_assert(size <= cache->size);

    g_mutex_lock(cache->mutex);

    if (!cache_is_empty(cache)) {
        ret = pop_xbuf(cache);
        goto out;
    }

    /*
     * We must create a new one...
     */
    ret = xbuf_create(cache->size);

out:
    ret->size = size;
    g_mutex_unlock(cache->mutex);
    return ret;
}

/**
 * Given an xbuf as an argument, put it into a cache. The size of the allocated
 * buffer of the xbuf and the xbuf_cache MUST match.
 *
 * @param cache: the cache to put the xbuf into
 * @param xbuf: pointer to the xbuf given back to us
 */

void xbuf_cache_put(struct xbuf_cache *cache, struct xbuf *xbuf)
{
    g_assert(xbuf->bufsize == cache->size);

    g_mutex_lock(cache->mutex);

    push_xbuf(cache, xbuf);

    g_mutex_unlock(cache->mutex);
}

/**
 * Change the vision of an xbuf's size. The new size MUST be less than the
 * current size.
 *
 * @param xbuf: the xbuf to modify
 * @param newsize: the new size
 */

void xbuf_shrink_size(struct xbuf *xbuf, size_t newsize)
{
    g_assert(newsize <= xbuf->size);
    xbuf->size = newsize;
}

