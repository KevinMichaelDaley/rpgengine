#ifndef FERRUM_PHYSICS_PHYS_JOBS_H
#define FERRUM_PHYSICS_PHYS_JOBS_H

/**
 * @file phys_jobs.h
 * @brief Physics-specific job dispatch layer.
 *
 * Maps physics pipeline stages to the engine's job system, splitting work
 * into batches and waiting on counter barriers.  Each stage has a dedicated
 * job_counter_t so multiple stages can be dispatched independently.
 */

#include <stdint.h>
#include "ferrum/job/system.h"
#include "ferrum/job/counter.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Identifies a physics pipeline stage.
 *
 * Stages 0–14 correspond to the sequential tick order in tick.c.
 */
typedef enum phys_stage_id {
    PHYS_STAGE_STEP_PLAN       = 0,
    PHYS_STAGE_TIER_CLASSIFY   = 1,
    PHYS_STAGE_SPATIAL_UPDATE  = 2,
    PHYS_STAGE_HALO_CLOSURE    = 3,
    PHYS_STAGE_AABB_UPDATE     = 4,
    PHYS_STAGE_BROADPHASE      = 5,
    PHYS_STAGE_NARROWPHASE     = 6,
    PHYS_STAGE_MANIFOLD_BUILD  = 7,
    PHYS_STAGE_STABILIZATION   = 8,
    PHYS_STAGE_CONSTRAINT_BUILD = 9,
    PHYS_STAGE_ISLAND_BUILD    = 10,
    PHYS_STAGE_TGS_SOLVE       = 11,
    PHYS_STAGE_XPBD_SOLVE      = 12,
    PHYS_STAGE_INTEGRATE       = 13,
    PHYS_STAGE_CACHE_COMMIT    = 14,
    PHYS_STAGE_COLLISION_FUSED = 15,  /**< Fused narrow→manifold→stab→constraint. */
    PHYS_STAGE_COUNT           = 16
} phys_stage_id_t;

/**
 * @brief Per-batch argument block passed to each dispatched job.
 *
 * The job function receives a pointer to one of these.  @c user_args is the
 * caller-provided context (e.g. a shared atomic counter), while @c start and
 * @c count define the slice of items this batch should process.
 *
 * Ownership: the caller must keep the batches array alive until the stage
 * wait completes.
 */
typedef struct phys_job_batch {
    void     *user_args; /**< Caller-provided context pointer. */
    uint32_t  start;     /**< First item index for this batch. */
    uint32_t  count;     /**< Number of items in this batch.   */
    uint32_t  batch_idx; /**< Zero-based batch ordinal.        */
} phys_job_batch_t;

/**
 * @brief Physics job context — wraps the engine job system with per-stage
 *        counters.
 *
 * Stack-allocatable.  Must be initialized with phys_job_context_init() and
 * torn down with phys_job_context_destroy().
 */
typedef struct phys_job_context {
    job_system_t  *job_sys;                      /**< Engine job system. */
    job_counter_t  counters[PHYS_STAGE_COUNT];   /**< Per-stage counters. */
} phys_job_context_t;

/**
 * @brief Initialize a physics job context.
 *
 * All per-stage counters are initialized to 0 (immediately satisfied).
 *
 * @param ctx  Non-NULL context to initialize.
 * @param sys  Non-NULL engine job system.
 *
 * Ownership: @p sys must outlive @p ctx.
 * Side effects: initializes mutexes inside each counter.
 */
void phys_job_context_init(phys_job_context_t *ctx, job_system_t *sys);

/**
 * @brief Destroy a physics job context, releasing counter resources.
 *
 * Must only be called when no jobs are in flight for any stage.
 *
 * @param ctx  Context to destroy (may be NULL).
 */
void phys_job_context_destroy(phys_job_context_t *ctx);

/**
 * @brief Compute a dynamic batch size that targets ~(2 * worker_count) jobs.
 *
 * Avoids both extremes: too many tiny jobs (dispatch overhead) and too few
 * jobs (poor parallelism).  The result is clamped to [min_batch, max_batch].
 *
 * @param ctx           Non-NULL physics job context (used to read worker_count).
 * @param total_items   Total number of items to process.
 * @param min_batch     Minimum items per batch (floor for parallelism).
 * @param max_batch     Maximum items per batch (cap for stack-limited stages).
 *                      Pass 0 to use total_items as the cap.
 * @return Batch size to use with phys_dispatch_stage().
 */
uint32_t phys_batch_size(const phys_job_context_t *ctx,
                         uint32_t total_items,
                         uint32_t min_batch,
                         uint32_t max_batch);

/**
 * @brief Dispatch a physics stage as batched jobs.
 *
 * Splits @p total_items into batches of @p batch_size and dispatches each
 * batch via job_dispatch_named() with a Tracy-friendly stage name.
 *
 * @param ctx         Non-NULL physics job context.
 * @param stage       Pipeline stage identifier.
 * @param fn          Job function called for each batch.
 * @param user_args   Caller context stored in each batch's user_args field.
 * @param total_items Total number of items to process (0 is valid: no jobs).
 * @param batch_size  Maximum items per batch (must be > 0).
 * @param batches     Caller-provided array with capacity >= ceil(total_items / batch_size).
 *                    Filled by this function.  Must remain valid until the
 *                    stage wait completes.
 * @return Number of jobs dispatched (0 when total_items == 0).
 *
 * Nullability: @p user_args may be NULL.
 * Error semantics: returns 0 on zero items; asserts preconditions otherwise.
 */
uint32_t phys_dispatch_stage(phys_job_context_t *ctx,
                             phys_stage_id_t stage,
                             void (*fn)(void *user_data),
                             void *user_args,
                             uint32_t total_items,
                             uint32_t batch_size,
                             phys_job_batch_t *batches);

/**
 * @brief Wait for all jobs of a stage to complete.
 *
 * Spins briefly then parks the fiber until the stage counter reaches 0.
 * Safe to call even if no jobs were dispatched for the stage.
 *
 * @param ctx    Non-NULL physics job context.
 * @param stage  Pipeline stage to wait on.
 */
void phys_wait_stage(phys_job_context_t *ctx, phys_stage_id_t stage);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_PHYS_JOBS_H */
