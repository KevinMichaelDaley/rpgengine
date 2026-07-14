/**
 * @file lm_parallel.c
 * @brief pthread parallel-for (see lm_parallel.h).
 */
#include "ferrum/lightmap/lm_parallel.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>

/* Shared state for the dynamic (work-stealing) parallel-for: threads atomically
 * grab small fixed-size chunks off a single counter until the range is drained.
 * This keeps every core busy to the very end even when per-index cost varies
 * wildly (e.g. a vault luxel path-traces far more than a floor luxel) -- static
 * contiguous chunking would leave the cheap threads idle at the tail. */
typedef struct lm_par_pool {
    uint32_t       n;         /**< total index count. */
    uint32_t       chunk;     /**< indices per grabbed chunk. */
    uint32_t       n_chunks;  /**< ceil(n/chunk). */
    atomic_uint    next;      /**< next chunk to claim. */
    lm_parallel_fn fn;
    void          *ctx;
} lm_par_pool_t;

static void *lm_par_worker(void *arg)
{
    lm_par_pool_t *p = (lm_par_pool_t *)arg;
    for (;;) {
        uint32_t c = atomic_fetch_add_explicit(&p->next, 1u, memory_order_relaxed);
        if (c >= p->n_chunks)
            break;
        uint32_t i0 = c * p->chunk;
        uint32_t i1 = i0 + p->chunk;
        if (i1 > p->n)
            i1 = p->n;
        p->fn(i0, i1, p->ctx);
    }
    return NULL;
}

uint32_t lm_parallel_threads(uint32_t requested)
{
    if (requested > 0)
        return requested;
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return (n > 0) ? (uint32_t)n : 1u;
}

void lm_parallel_for(uint32_t n, lm_parallel_fn fn, void *ctx, uint32_t n_threads)
{
    if (n == 0 || fn == NULL)
        return;
    n_threads = lm_parallel_threads(n_threads);
    if (n_threads > n)
        n_threads = n;
    if (n_threads <= 1) {
        fn(0, n, ctx);
        return;
    }

    /* Aim for ~many chunks per thread so the atomic hand-out balances the load
     * (but keep chunks big enough that atomic contention stays negligible). */
    uint32_t chunk = n / (n_threads * 32u);
    if (chunk < 64u)
        chunk = 64u;
    lm_par_pool_t pool;
    pool.n = n;
    pool.chunk = chunk;
    pool.n_chunks = (n + chunk - 1u) / chunk;
    atomic_init(&pool.next, 0u);
    pool.fn = fn;
    pool.ctx = ctx;

    pthread_t *th = malloc((size_t)n_threads * sizeof(pthread_t));
    if (!th) {
        fn(0, n, ctx); /* fall back to serial */
        return;
    }
    uint32_t spawned = 0;
    for (uint32_t t = 0; t < n_threads; ++t) {
        if (pthread_create(&th[spawned], NULL, lm_par_worker, &pool) == 0)
            ++spawned;
    }
    if (spawned == 0)
        lm_par_worker(&pool); /* no threads spawned: drain on this thread */
    for (uint32_t t = 0; t < spawned; ++t)
        pthread_join(th[t], NULL);
    free(th);
}
