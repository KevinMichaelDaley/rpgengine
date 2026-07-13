/**
 * @file lm_parallel.c
 * @brief pthread parallel-for (see lm_parallel.h).
 */
#include "ferrum/lightmap/lm_parallel.h"

#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct lm_par_arg {
    uint32_t       i0, i1;
    lm_parallel_fn fn;
    void          *ctx;
} lm_par_arg_t;

static void *lm_par_thread(void *a)
{
    lm_par_arg_t *p = (lm_par_arg_t *)a;
    p->fn(p->i0, p->i1, p->ctx);
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

    pthread_t *th = malloc((size_t)n_threads * sizeof(pthread_t));
    lm_par_arg_t *ar = malloc((size_t)n_threads * sizeof(lm_par_arg_t));
    if (!th || !ar) {
        free(th); free(ar);
        fn(0, n, ctx); /* fall back to serial */
        return;
    }

    uint32_t chunk = (n + n_threads - 1u) / n_threads;
    uint32_t spawned = 0;
    for (uint32_t t = 0; t < n_threads; ++t) {
        uint32_t i0 = t * chunk;
        if (i0 >= n)
            break;
        uint32_t i1 = i0 + chunk;
        if (i1 > n)
            i1 = n;
        ar[spawned] = (lm_par_arg_t){ i0, i1, fn, ctx };
        if (pthread_create(&th[spawned], NULL, lm_par_thread, &ar[spawned]) != 0)
            fn(i0, i1, ctx); /* run inline on spawn failure */
        else
            ++spawned;
    }
    for (uint32_t t = 0; t < spawned; ++t)
        pthread_join(th[t], NULL);
    free(th);
    free(ar);
}
