#ifndef FERRUM_PHYSICS_PAR_CONSTRAINT_BUILD_PAR_H
#define FERRUM_PHYSICS_PAR_CONSTRAINT_BUILD_PAR_H

/**
 * @file constraint_build_par.h
 * @brief Parallel wrapper for the constraint build stage.
 *
 * Splits manifold processing across multiple jobs (32 manifolds per batch).
 * Each job atomically claims output slots in the constraint array via
 * fetch-add on a shared atomic counter, so manifold ordering within
 * a batch is preserved but batches may interleave.
 *
 * Produces the same total constraint count as the sequential
 * phys_stage_constraint_build().
 */

#include "ferrum/physics/constraint_stage.h"
#include "ferrum/physics/phys_jobs.h"
#include "ferrum/physics/phys_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Batch size for parallel constraint build: 32 manifolds per job. */
#define PHYS_CONSTRAINT_BUILD_BATCH_SIZE 32u

/**
 * @brief Execute the constraint build stage in parallel.
 *
 * Dispatches manifold processing in parallel batches of
 * PHYS_CONSTRAINT_BUILD_BATCH_SIZE.  Each job processes its manifold
 * range and atomically claims output constraint slots.
 *
 * Produces identical constraint count to phys_stage_constraint_build().
 *
 * @param args  Stage arguments (if NULL, no-op).
 * @param ctx   Physics job context for dispatch (if NULL, falls back to
 *              sequential phys_stage_constraint_build).
 *
 * Ownership: caller owns all data in args; ctx must outlive the call.
 * Nullability: both args and ctx may be NULL.
 * Error semantics: no-op on NULL args; falls back on NULL ctx.
 * Side effects: writes to args->constraints_out and *args->constraint_count_out.
 */
void phys_stage_constraint_build_par(const phys_constraint_build_args_t *args,
                                      phys_job_context_t *ctx,
                                      phys_frame_arena_t *arena);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_PAR_CONSTRAINT_BUILD_PAR_H */
