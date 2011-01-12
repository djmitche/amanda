#include "xbuf.h"

/*
 * Cache implementation for struct xbuf.
 *
 * The principle is that several caches exist, of different sizes, from
 * XBUF_SZ_MINSIZE to XBUF_SZ_MAXSIZE, both being powers of 2. When an xbuf of a
 * given size is asked for, it is taken from the cache which contains xbufs of
 * the size immediately superior or equal (or an xbuf is created and added to
 * the cache if no buffers are available).
 *
 * If a size is asked for which is even greater than XBUF_CACHE_MAXSIZE, a bare
 * xbuf is returned. The caller won't notice, since it will always use
 * xbuf_cache_get() or xbuf_cache_put().
 */

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
 * The minimal and maximal cache size of individual xbufs (defined here to 4k
 * and 64k). If you want to change these, change the XBUF_SZ_*_OFFSET values.
 */

#define XBUF_SZ_MIN_OFFSET 12
#define XBUF_SZ_MINSIZE (1 << XBUF_SZ_MIN_OFFSET)

#define XBUF_SZ_MAX_OFFSET 16
#define XBUF_SZ_MAXSIZE (1 << XBUF_SZ_MAX_OFFSET)

/*
 * The number of caches defined - dynamically computed from XBUF_CACHE_*_OFFSET
 */

#define NR_XBUF_CACHES (XBUF_SZ_MAX_OFFSET - XBUF_SZ_MIN_OFFSET + 1)

/*
 * This is the maximum memory we ever want to see allocated to an idle cache.
 * Set to 1 MiB by default. As sizes are power of 2, it is easy enough to
 * compute the maximum number of xbufs this represents.
 */

#define XBUF_CACHE_MAXALLOC_OFFSET 20
#define XBUF_CACHE_MAXSIZE (1 << XBUF_CACHE_MAXALLOC_OFFSET)

#define XBUF_CACHE_MAXENTRIES(offset) \
    (1 << (XBUF_CACHE_MAXALLOC_OFFSET - (offset)))

/*
 * Number of xbufs to create for a cache at initialization time. Be careful not
 * to go over the maximum wanted size...
 */

#define XBUF_CACHE_DEFAULT_ENTRIES (10)

#define XBUF_CACHE_INITIAL_ENTRIES(offset) \
    MIN(XBUF_CACHE_DEFAULT_ENTRIES, XBUF_CACHE_MAXENTRIES(offset))

/*
 * The "list" implementation is really dumb, but it's enough for our purpose.
 * Each xbuf has a member named "list", which is a pointer to an xbuf... And so
 * is the "head" of the list.
 */

static void push_xbuf(struct xbuf_cache *cache, struct xbuf *xbuf)
{
    xbuf->next = cache->list;
    cache->list = xbuf;
}

static struct xbuf *pop_xbuf(struct xbuf_cache *cache)
{
    struct xbuf *ret = cache->list;
    cache->list = ret->next;
    return ret;
}

/*
 * Each cache is initialized with its list pointer set to NULL. As buffers are
 * pushed into it, it will not be the case. When the last xbuf is popped out of
 * it, it will become NULL again. Checking whether a cache is empty is therefore
 * trivial...
 */

#define cache_is_empty(cache) ((cache)->list == NULL)

/*
 * All defined caches.
 */

static struct xbuf_cache caches[NR_XBUF_CACHES];

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
 * Create a struct xbuf with a given allocated size. The size will be a power of
 * two if this method is called from "within a cache". It may not be in a case
 * of an "oversized" xbuf. It will set every field of the resulting struct
 * except alloc, cache and next, which it is up to the caller to set.
 *
 * @size: the size allocated for this xbuf
 *
 * @returns: the newly allocated xbuf
 */

static struct xbuf *xbuf_create(size_t size)
{
    gpointer data = g_malloc(size);
    struct xbuf *xbuf = g_malloc(sizeof(struct xbuf));

    xbuf->data = data;
    xbuf->cache = NULL;

    return xbuf;
}

/**
 * Initialize one xbuf_cache. Each struct xbuf of this cache will have a memory
 * allocation of 1 << offset.
 *
 * @cache: the cache to initialize
 * @offset: see description
 */

static void xbuf_cache_init(struct xbuf_cache *cache, gint offset)
{
    int i, entries = XBUF_CACHE_INITIAL_ENTRIES(offset);
    size_t size = 1 << offset;
    struct xbuf *xbuf;

    cache->mutex = g_mutex_new();
    cache->list = NULL;
    cache->size = 1 << offset;
    cache->max_entries = XBUF_CACHE_MAXENTRIES(offset);
    cache->nr_entries = entries;

    for (i = 0; i < entries; i++) {
        xbuf = xbuf_create(size);
        xbuf->cache = cache;
        push_xbuf(cache, xbuf);
    }
}

/**
 * Walk the cache array and initialize each cache.
 */

static void init_caches(void)
{
    static gboolean initialized = FALSE;
    static GStaticMutex mutex = G_STATIC_MUTEX_INIT;
    guint i;

    g_static_mutex_lock(&mutex);
    if (initialized)
        goto out;

    for (i = 0; i < G_N_ELEMENTS(caches); i++)
        xbuf_cache_init(&caches[i], XBUF_SZ_MIN_OFFSET + i);

    initialized = TRUE;
out:
    g_static_mutex_unlock(&mutex);
}

/**
 * Create an oversized xbuf (size greater than XBUF_CACHE_MAXSIZE). This method
 * is only called if the get_cache_by_size() method below returns NULL. It
 * basically just calls xbuf_create() and sets the cache pointer to NULL.
 *
 * @size: the size to allocate for this buffer.
 */

static struct xbuf *get_oversized_xbuf(size_t size)
{
    struct xbuf *ret = xbuf_create(size);
    ret->size = size;
    ret->cache = NULL;

    return ret;
}

/**
 * Given a size, get a pointer to the xbuf_cache containing xbufs able to
 * contain that size. Also initializes caches using the init_caches() function
 * above, since  it is the first function called by xbuf_cache_get() below in
 * order to grab the correct cache.
 *
 * @size: the size to compute the correct cache from
 *
 * @returns: a pointer to the appopriate cache, or NULL if an oversized xbuf
 * should be generated.
 */

static struct xbuf_cache *get_cache_by_size(size_t size)
{
    guint i = 0;
    size_t tmp = 1 << XBUF_SZ_MIN_OFFSET;

    init_caches();

    while (size > tmp) {
        tmp <<= 1;
        i++;
    }

    return (i < G_N_ELEMENTS(caches)) ? &caches[i] : NULL;
}

/*
 * PUBLICLY AVAILABLE FUNCTIONS
 */

/**
 * Given a size as an argument, find the most suitable cache and return an xbuf
 * from that cache (or an oversized buffer if no suitable cache is found).
 *
 * @size: the requested size
 */

struct xbuf *xbuf_cache_get(size_t size)
{
    struct xbuf_cache *cache = get_cache_by_size(size);
    struct xbuf *ret;

    if (cache == NULL)
        return get_oversized_xbuf(size);

    g_mutex_lock(cache->mutex);

    if (!cache_is_empty(cache)) {
        ret = pop_xbuf(cache);
        goto out;
    }

    /*
     * We must create a new one...
     */
    ret = xbuf_create(cache->size);
    ret->cache = cache;
    cache->nr_entries++;

out:
    ret->size = size;
    g_mutex_unlock(cache->mutex);
    return ret;
}

/**
 * Given an xbuf as an argument, put it back into the pool. The xbuf may be
 * destroyed in two cases:
 * - it's an oversized xbuf (its cache field is set to NULL);
 * - the cache this xbuf comes from has allocated more than XBUF_CACHE_MAXSIZE
 *   of data.
 *
 * @xbuf: pointer to the xbuf given back to us
 */

void xbuf_cache_put(struct xbuf *xbuf)
{
    struct xbuf_cache *cache = xbuf->cache;

    if (cache == NULL) {
        /*
         * Oversized buffer...
         */
        xbuf_destroy(xbuf);
        return;
    }

    g_mutex_lock(cache->mutex);

    /*
     * Check whether we have overriden our maximum number of entries: if we have
     * done so, we must destroy the xbuf.
     */

    if (cache->nr_entries > cache->max_entries) {
        xbuf_destroy(xbuf);
        cache->nr_entries--;
    } else
        push_xbuf(cache, xbuf);

    g_mutex_unlock(cache->mutex);
}

/**
 * Utility function to clamp the visible size of an xbuf to another size less
 * than or equal to its recorded size
 *
 * @xbuf: the xbuf
 * @newsize: the new size
 */

void xbuf_clamp_size(struct xbuf *xbuf, size_t newsize)
{
    g_assert(newsize <= xbuf->size);
    xbuf->size = newsize;
}

