#ifndef FERRUM_PHYSICS_PAR_INTEGRATE_PAR_H
#define FERRUM_PHYSICS_PAR_INTEGRATE_PAR_H

/**
 * @file integrate_par.h
 * @brief Parallel integration + sleep detection.
 *
 * Equivalent to phys_stage_integrate() but distributes bodies across
 * multiple jobs using the physics job system.  Each batch of 512 bodies
 * writes to its own disjoint slice of bodies_out — no contention.
 */

#include "ferrum/physics/integrate.h"
#include "ferrum/physics/phys_jobs.h"
#include "ferrum/physics/phys_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Number of bodies processed per parallel batch. */
#define PHYS_INTEGRATE_BATCH_SIZE 512

/**
 * @brief Parallel integration + sleep detection.
 *
 * Splits bodies into batches of PHYS_INTEGRATE_BATCH_SIZE and dispatches
 * each batch as a job.  Each job integrates bodies for its slice,
 * reading bodies_in + velocities (read-only shared) and writing to
 * bodies_out[start..start+count).
 *
 * @param args   Integration arguments (non-NULL).  Same as sequential version.
 * @param ctx    Physics job context (non-NULL).
 * @param arena  Frame arena for temporary batch allocations (non-NULL).
 *
 * Ownership: borrows all pointers from args; does not free anything.
 * Nullability: no-op if args or ctx is NULL.
 * Side effects: writes to args->bodies_out[0..body_count-1].
 * Error semantics: no-op on NULL inputs or zero body count.
 */
void phys_stage_integrate_par(const phys_integrate_args_t *args,
                               phys_job_context_t *ctx,
                               phys_frame_arena_t *arena);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_PAR_INTEGRATE_PAR_H */
