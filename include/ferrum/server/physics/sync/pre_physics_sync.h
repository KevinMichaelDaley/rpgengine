#ifndef FERRUM_SERVER_PHYSICS_SYNC_PRE_PHYSICS_SYNC_H
#define FERRUM_SERVER_PHYSICS_SYNC_PRE_PHYSICS_SYNC_H

/**
 * @file pre_physics_sync.h
 * @brief Parallel pre-physics ECS→physics sync pass.
 *
 * Stage 3 of the server tick: iterates dirty ECS entities in parallel
 * batches and writes kinematic intent (velocity, position) directly
 * into the physics double-buffer (`bodies_next[]`).  Each entity maps
 * to a unique body_index, so parallel writes are safe without locking.
 *
 * The caller builds an array of sync records (SOA-friendly), marks
 * dirty ones, and hands the batch to the sync pass.
 */

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/physics/phys_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations. */
struct phys_body_pool;
struct job_system;
struct job_counter;

/**
 * @brief One record describing the kinematic intent for a single entity.
 *
 * Stored in a flat SOA-friendly array.  Only records with `dirty != 0`
 * are applied during the sync pass.
 */
typedef struct phys_sync_record {
    uint32_t    body_index;     /**< Physics body slot index. */
    uint32_t    entity_index;   /**< ECS entity index (written to body.entity_index). */
    phys_vec3_t linear_vel;     /**< Desired linear velocity. */
    phys_vec3_t position;       /**< Desired position (kinematic target). */
    uint8_t     dirty;          /**< Non-zero ⇒ apply this record. */
    uint8_t     _pad[3];
} phys_sync_record_t;

/**
 * @brief Arguments for the pre-physics sync pass.
 */
typedef struct phys_pre_physics_sync_args {
    /** Array of sync records (caller-owned, stable for duration of sync). */
    const phys_sync_record_t *records;
    /** Number of records in the array. */
    uint32_t record_count;
    /** Physics body pool whose `bodies_next` will be written. */
    struct phys_body_pool *body_pool;
} phys_pre_physics_sync_args_t;

/**
 * @brief Run the pre-physics sync pass sequentially (single-threaded).
 *
 * Iterates all records; for each with `dirty != 0`, writes
 * `linear_vel`, `position`, and `entity_index` into `bodies_next[body_index]`.
 * Non-dirty records are skipped.  Out-of-range or inactive body indices
 * are silently skipped.
 *
 * @param args  Sync arguments (non-NULL).
 * @return 0 on success, -1 on invalid arguments.
 */
int phys_pre_physics_sync(const phys_pre_physics_sync_args_t *args);

/**
 * @brief Arguments for the parallel pre-physics sync pass.
 */
typedef struct phys_pre_physics_sync_par_args {
    /** Array of sync records (caller-owned, stable for duration of sync). */
    const phys_sync_record_t *records;
    /** Number of records in the array. */
    uint32_t record_count;
    /** Physics body pool whose `bodies_next` will be written. */
    struct phys_body_pool *body_pool;
    /** Job system for dispatching parallel batches. */
    struct job_system *jobs;
    /** Maximum records per job batch (0 = use default 64). */
    uint32_t batch_size;
} phys_pre_physics_sync_par_args_t;

/**
 * @brief Run the pre-physics sync pass in parallel via the job system.
 *
 * Splits records into batches of `batch_size` and dispatches each as
 * a job.  Waits for all batches to complete before returning.
 *
 * @param args  Sync arguments (non-NULL, jobs non-NULL).
 * @return 0 on success, -1 on invalid arguments.
 */
int phys_pre_physics_sync_par(const phys_pre_physics_sync_par_args_t *args);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_SERVER_PHYSICS_SYNC_PRE_PHYSICS_SYNC_H */
