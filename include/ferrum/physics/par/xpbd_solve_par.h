#ifndef FERRUM_PHYSICS_PAR_XPBD_SOLVE_PAR_H
#define FERRUM_PHYSICS_PAR_XPBD_SOLVE_PAR_H

/**
 * @file xpbd_solve_par.h
 * @brief Parallel XPBD position solver — splits constraints across jobs.
 *
 * Equivalent to phys_stage_xpbd_solve() but distributes constraint
 * solving across multiple jobs using the physics job system.  Each
 * batch of 128 constraints is solved independently per Jacobi iteration
 * (read from start-of-iteration positions, write corrections to own slots).
 */

#include "ferrum/physics/xpbd_solve.h"
#include "ferrum/physics/phys_jobs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Number of constraints processed per parallel batch. */
#define PHYS_XPBD_SOLVE_BATCH_SIZE 128

/**
 * @brief Parallel XPBD position solver — splits constraints across jobs.
 *
 * Equivalent to phys_stage_xpbd_solve() but distributes constraint
 * solving across multiple jobs.  Each Jacobi iteration dispatches
 * batches of 128 constraints as independent jobs.
 *
 * @param args  Same args as sequential version.  NULL-safe (no-op).
 * @param ctx   Physics job context (non-NULL).
 *
 * Ownership: borrows all pointers from args; does not free anything.
 * Nullability: no-op if args or ctx is NULL.
 * Side effects: modifies bodies_out, velocities_out, and constraint lambdas.
 * Error semantics: falls back to sequential if ctx is NULL.
 */
void phys_stage_xpbd_solve_par(const phys_xpbd_solve_args_t *args,
                                phys_job_context_t *ctx);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_PAR_XPBD_SOLVE_PAR_H */
