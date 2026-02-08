#ifndef FERRUM_PHYSICS_PAR_TGS_SOLVE_PAR_H
#define FERRUM_PHYSICS_PAR_TGS_SOLVE_PAR_H

/**
 * @file tgs_solve_par.h
 * @brief Parallel TGS velocity solver — one job per island.
 *
 * Equivalent to phys_stage_tgs_solve() but dispatches one job per
 * island using the physics job system.  Islands are independent
 * (disjoint body sets, disjoint constraint sets) so no synchronization
 * is needed between jobs.
 */

#include "ferrum/physics/tgs_solve.h"
#include "ferrum/physics/phys_jobs.h"
#include "ferrum/physics/phys_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parallel TGS velocity solver — one job per island.
 *
 * Initializes the velocity workspace from body state, then dispatches
 * one job per non-sleeping island.  Each job runs the iterative
 * sequential impulse solver on its island's constraints.
 *
 * @param args   Solver arguments (same as sequential version). NULL-safe (no-op).
 * @param ctx    Physics job context (non-NULL).
 * @param arena  Frame arena for temporary batch allocations (non-NULL).
 *
 * Ownership: borrows all pointers from args; does not free anything.
 * Nullability: no-op if args or ctx is NULL.
 * Side effects: modifies args->velocities and constraint lambdas.
 * Error semantics: falls back to sequential if island count is 0.
 */
void phys_stage_tgs_solve_par(const phys_tgs_solve_args_t *args,
                               phys_job_context_t *ctx,
                               phys_frame_arena_t *arena);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_PAR_TGS_SOLVE_PAR_H */
