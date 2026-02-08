#ifndef FERRUM_PHYSICS_PAR_STABILIZATION_PAR_H
#define FERRUM_PHYSICS_PAR_STABILIZATION_PAR_H

/**
 * @file stabilization_par.h
 * @brief Parallel stabilization hint computation.
 *
 * Equivalent to phys_stage_stabilization() but distributes manifolds
 * across multiple jobs using the physics job system.  Each batch of
 * 64 manifolds writes to its own disjoint slice of hints_out — no
 * contention, no merge step required.
 */

#include "ferrum/physics/stabilization.h"
#include "ferrum/physics/phys_jobs.h"
#include "ferrum/physics/phys_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Number of manifolds processed per parallel batch. */
#define PHYS_STABILIZATION_BATCH_SIZE 64

/**
 * @brief Parallel stabilization hint computation.
 *
 * Splits manifolds into batches of PHYS_STABILIZATION_BATCH_SIZE and
 * dispatches each batch as a job.  Each job computes hints for its
 * slice of manifolds, writing directly to hints_out[start..start+count).
 *
 * @param args   Stage arguments (non-NULL).  Same as sequential version.
 * @param ctx    Physics job context (non-NULL).
 * @param arena  Frame arena for temporary batch allocations (non-NULL).
 *
 * Ownership: borrows all pointers from args; does not free anything.
 * Nullability: no-op if args or ctx is NULL.
 * Side effects: writes to args->hints_out[0..manifold_count-1].
 * Error semantics: no-op on NULL inputs or zero manifold count.
 */
void phys_stage_stabilization_par(const phys_stabilization_args_t *args,
                                   phys_job_context_t *ctx,
                                   phys_frame_arena_t *arena);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_PAR_STABILIZATION_PAR_H */
