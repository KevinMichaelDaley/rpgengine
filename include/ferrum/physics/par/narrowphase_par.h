#ifndef FERRUM_PHYSICS_PAR_NARROWPHASE_PAR_H
#define FERRUM_PHYSICS_PAR_NARROWPHASE_PAR_H

/**
 * @file narrowphase_par.h
 * @brief Parallel narrowphase wrapper.
 *
 * Splits broadphase pairs into batches of 64 and dispatches them
 * as parallel jobs via the physics job system.  Each job uses
 * atomic fetch-add to claim output slots in the shared candidate
 * buffer, so no per-batch pre-allocation is needed.
 *
 * The sequential implementation phys_stage_narrowphase() is used
 * internally for each batch's pair range.
 */

#include "ferrum/physics/narrowphase.h"
#include "ferrum/physics/phys_jobs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Execute narrowphase in parallel using batched jobs.
 *
 * Splits args->pairs into batches of 64, dispatches each batch
 * as a job, and waits for completion.  Results are written to
 * args->candidates_out / args->candidate_count_out exactly as
 * phys_stage_narrowphase() would, though candidate ordering may
 * differ.
 *
 * @param args  Narrowphase arguments (NULL-safe, no-op if NULL).
 *              Ownership: caller owns all pointed-to arrays.
 * @param ctx   Physics job context (NULL-safe, falls back to
 *              sequential if NULL).
 *
 * Side effects: writes to args->candidates_out and
 *               args->candidate_count_out.
 */
void phys_stage_narrowphase_par(const phys_narrowphase_args_t *args,
                                phys_job_context_t *ctx);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_PAR_NARROWPHASE_PAR_H */
