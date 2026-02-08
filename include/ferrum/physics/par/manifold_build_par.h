#ifndef FERRUM_PHYSICS_PAR_MANIFOLD_BUILD_PAR_H
#define FERRUM_PHYSICS_PAR_MANIFOLD_BUILD_PAR_H

/**
 * @file manifold_build_par.h
 * @brief Parallel manifold build — splits contact candidates across jobs.
 *
 * Equivalent to phys_stage_manifold_build() but distributes candidate
 * processing across multiple jobs with a batch size of 32. Manifold
 * cache access is synchronized via a mutex; output slot allocation
 * uses atomic fetch-add.
 */

#include "ferrum/physics/manifold_build.h"
#include "ferrum/physics/phys_jobs.h"
#include "ferrum/physics/phys_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Number of contact candidates processed per parallel batch. */
#define PHYS_MANIFOLD_BUILD_BATCH_SIZE 32

/**
 * @brief Parallel manifold build — splits candidates across jobs.
 *
 * Equivalent to phys_stage_manifold_build() but distributes work
 * across multiple jobs using the physics job system. Each batch of
 * 32 candidates is processed independently, with the manifold cache
 * protected by a mutex and output indices claimed atomically.
 *
 * @param args  Same args as sequential version.
 * @param ctx   Physics job context (non-NULL).
 *
 * Ownership: borrows all pointers from args; does not free anything.
 * Nullability: no-op if args or ctx is NULL.
 * Side effects: mutates args->cache, writes to args->manifolds_out
 *               and args->manifold_count_out.
 * Error semantics: sets manifold_count_out to 0 on invalid input.
 */
void phys_stage_manifold_build_par(const phys_manifold_build_args_t *args,
                                    phys_job_context_t *ctx,
                                    phys_frame_arena_t *arena);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_PAR_MANIFOLD_BUILD_PAR_H */
