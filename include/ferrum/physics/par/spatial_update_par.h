#ifndef FERRUM_PHYSICS_PAR_SPATIAL_UPDATE_PAR_H
#define FERRUM_PHYSICS_PAR_SPATIAL_UPDATE_PAR_H

/**
 * @file spatial_update_par.h
 * @brief Parallel wrapper for the spatial update stage.
 *
 * Splits body AABB computation across multiple jobs (512 bodies per batch),
 * then performs sequential grid insertion after all AABBs are computed.
 *
 * Phase A (parallel): each job computes AABBs for its body range.
 *   Writing to disjoint aabbs_out indices — no contention.
 * Phase B (sequential): insert all bodies into the spatial grid.
 *   Grid insertion uses arena allocation which is not thread-safe.
 */

#include "ferrum/physics/spatial_update.h"
#include "ferrum/physics/phys_jobs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Batch size for parallel spatial update: 512 bodies per job. */
#define PHYS_SPATIAL_UPDATE_BATCH_SIZE 512u

/**
 * @brief Execute the spatial update stage in parallel.
 *
 * Clears the grid, dispatches AABB computation in parallel batches of
 * PHYS_SPATIAL_UPDATE_BATCH_SIZE, waits for completion, then inserts
 * all active bodies into the grid sequentially.
 *
 * Produces identical results to phys_stage_spatial_update().
 *
 * @param args  Stage arguments (if NULL, no-op).
 * @param ctx   Physics job context for dispatch (if NULL, falls back to
 *              sequential phys_stage_spatial_update).
 *
 * Ownership: caller owns all data in args; ctx must outlive the call.
 * Nullability: both args and ctx may be NULL.
 * Error semantics: no-op on NULL args; falls back on NULL ctx.
 * Side effects: writes to args->aabbs_out and args->grid_out.
 */
void phys_stage_spatial_update_par(const phys_spatial_update_args_t *args,
                                    phys_job_context_t *ctx);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_PAR_SPATIAL_UPDATE_PAR_H */
