/**
 * @file lm_parallel.h
 * @brief Minimal pthread parallel-for for the offline baker's per-index passes.
 *
 * The bake passes (path-traced gather, direct, ...) are embarrassingly parallel
 * over luxels/elements, so this splits an index range [0,n) into contiguous
 * chunks across a thread pool and runs a callback per chunk. Offline only --
 * uses pthreads directly (not the fiber job system) since the baker is a
 * standalone tool.
 *
 * Ownership: none. Nullability: @p fn non-NULL. Errors: falls back to serial on
 * allocation / thread-spawn failure. Side effects: whatever @p fn does.
 */
#ifndef FERRUM_LIGHTMAP_LM_PARALLEL_H
#define FERRUM_LIGHTMAP_LM_PARALLEL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Process the half-open index sub-range [@p i0, @p i1). Called once per chunk,
 *  concurrently across threads; must only touch per-index (non-shared) state. */
typedef void (*lm_parallel_fn)(uint32_t i0, uint32_t i1, void *ctx);

/**
 * @brief Resolve a thread count: returns @p requested if > 0, else the number of
 *        online CPUs (>= 1).
 */
uint32_t lm_parallel_threads(uint32_t requested);

/**
 * @brief Run @p fn over [0,@p n) split into @p n_threads contiguous chunks
 *        (0 = auto = online CPUs), joining before return. Serial for n_threads<=1.
 */
void lm_parallel_for(uint32_t n, lm_parallel_fn fn, void *ctx, uint32_t n_threads);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_LM_PARALLEL_H */
