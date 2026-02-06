#ifndef FERRUM_PHYSICS_PAR_BROADPHASE_PAR_H
#define FERRUM_PHYSICS_PAR_BROADPHASE_PAR_H

/**
 * @file broadphase_par.h
 * @brief Parallel broadphase — splits active body processing across jobs.
 *
 * Equivalent to phys_stage_broadphase() but distributes work across
 * multiple jobs using the physics job system.  Each job processes a
 * range of active bodies, queries the spatial grid (read-only), and
 * writes pairs to the shared output buffer via atomic indexing.
 */

#include "ferrum/physics/broadphase.h"
#include "ferrum/physics/phys_jobs.h"

struct phys_frame_arena;

#ifdef __cplusplus
extern "C" {
#endif

/** Number of active bodies processed per parallel batch. */
#define PHYS_BROADPHASE_PAR_BATCH_SIZE 64

/**
 * @brief Parallel broadphase — splits active body processing across jobs.
 *
 * Collects all active tier body indices into a flat array, splits the
 * array into batches, and dispatches each batch as a job.  Each job
 * queries the spatial grid (read-only) and writes pairs to pairs_out
 * using an atomic pair counter, avoiding the need for per-batch merge.
 *
 * @param args   Broadphase arguments (non-NULL); same as sequential version.
 * @param ctx    Physics job context (non-NULL).
 * @param arena  Frame arena for transient allocations (non-NULL).
 *
 * Ownership: borrows all pointers from args; does not free anything.
 * Nullability: no-op if args, ctx, or arena is NULL.
 * Side effects: writes to args->pairs_out and args->pair_count_out.
 * Error semantics: falls back to sequential if arena allocation fails.
 */
void phys_stage_broadphase_par(const phys_broadphase_args_t *args,
                                phys_job_context_t *ctx,
                                struct phys_frame_arena *arena);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_PAR_BROADPHASE_PAR_H */
