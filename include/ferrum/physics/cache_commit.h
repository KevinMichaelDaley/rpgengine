#ifndef FERRUM_PHYSICS_CACHE_COMMIT_H
#define FERRUM_PHYSICS_CACHE_COMMIT_H

/** @file
 * @brief Stage 13: Cache Commit + Events.
 *
 * Writes solved impulses back to the manifold cache for warmstarting
 * the next frame, and emits impact events for gameplay (sound, damage).
 */

#include <stdint.h>

#include "ferrum/physics/phys_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations. */
struct phys_manifold;
struct phys_constraint;
struct phys_manifold_cache;

/**
 * @brief An impact event emitted when a contact impulse exceeds a threshold.
 *
 * Provides the body pair, contact point/normal, and impulse magnitude
 * so gameplay systems can trigger effects (sound, damage, particles).
 */
typedef struct phys_impact_event {
    uint32_t body_a;            /**< Index of body A. */
    uint32_t body_b;            /**< Index of body B. */
    phys_vec3_t point;          /**< World-space contact point. */
    phys_vec3_t normal;         /**< Contact normal (A→B). */
    float impulse_magnitude;    /**< Absolute normal impulse. */
} phys_impact_event_t;

/**
 * @brief Arguments for the cache commit stage.
 *
 * @note Ownership: caller owns all pointers. The cache is modified
 *       in place. events_out must point to a buffer of at least
 *       max_events elements.
 * @note Nullability: if args is NULL, phys_stage_cache_commit is a no-op.
 * @note Side effects: modifies cache entries and writes to events_out.
 */
typedef struct phys_cache_commit_args {
    const struct phys_manifold *manifolds;    /**< Source manifold array. */
    const struct phys_constraint *constraints;/**< Solved constraints. */
    uint32_t constraint_count;               /**< Number of constraints. */
    struct phys_manifold_cache *cache;        /**< Manifold cache to update. */
    phys_impact_event_t *events_out;         /**< Output buffer for events. */
    uint32_t *event_count_out;               /**< Output: number of events emitted. */
    uint32_t max_events;                     /**< Capacity of events_out. */
    float impact_threshold;                  /**< Minimum impulse to emit event. */
    float warmstart_decay;                   /**< Impulse decay factor (0-1, 1=no decay). */
} phys_cache_commit_args_t;

/**
 * @brief Execute Stage 13: commit solved impulses to cache and emit events.
 *
 * For each constraint:
 *  1. Finds the cached manifold by body pair.
 *  2. Writes back normal and tangent impulses for warmstarting.
 *  3. If normal impulse exceeds impact_threshold, emits an impact event.
 *
 * @param args  Stage arguments. If NULL, no-op.
 *
 * @note No allocations performed.
 * @note Error semantics: silently skips constraints whose cache entry
 *       is missing or whose point_idx is out of range.
 */
void phys_stage_cache_commit(const phys_cache_commit_args_t *args);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_CACHE_COMMIT_H */
