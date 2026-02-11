#ifndef FERRUM_PHYSICS_PAR_TIER_CLASSIFY_PAR_H
#define FERRUM_PHYSICS_PAR_TIER_CLASSIFY_PAR_H

/**
 * @file tier_classify_par.h
 * @brief Parallel tier classification — splits bodies across jobs.
 *
 * Equivalent to phys_stage_tier_classify() but distributes work across
 * multiple jobs using the physics job system.  Each batch of 1024 bodies
 * is classified independently into thread-local tier lists, then
 * per-batch results are merged into the final output.
 */

#include "ferrum/physics/tier_classify.h"
#include "ferrum/physics/phys_jobs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Number of bodies processed per parallel batch. */
#define PHYS_TIER_CLASSIFY_BATCH_SIZE 64

/**
 * @brief Parallel tier classification — splits bodies across jobs.
 *
 * Equivalent to phys_stage_tier_classify() but distributes work across
 * multiple jobs using the physics job system.  Each batch of 1024 bodies
 * is classified independently, then per-batch results are merged.
 *
 * @param args  Same args as sequential version (non-NULL).
 * @param ctx   Physics job context (non-NULL).
 *
 * Ownership: borrows all pointers from args; does not free anything.
 * Nullability: no-op if args or ctx is NULL.
 * Side effects: allocates from args->arena; populates args->tier_lists_out.
 * Error semantics: falls back to sequential if arena allocation fails.
 */
void phys_stage_tier_classify_par(const phys_tier_classify_args_t *args,
                                   phys_job_context_t *ctx);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_PAR_TIER_CLASSIFY_PAR_H */
