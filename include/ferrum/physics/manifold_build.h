#ifndef FERRUM_PHYSICS_MANIFOLD_BUILD_H
#define FERRUM_PHYSICS_MANIFOLD_BUILD_H

/** @file
 * @brief Stage 7: Manifold Build + Cache Merge.
 *
 * Merges narrowphase contact candidates with the persistent manifold
 * cache for warmstarting, then outputs the current frame's manifolds.
 */

#include <stdint.h>

struct phys_contact_candidate;
struct phys_manifold_cache;
struct phys_manifold;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Arguments for the manifold build stage.
 *
 * Ownership: caller owns all pointed-to arrays and the cache.
 * The stage reads candidates and writes to manifolds_out /
 * manifold_count_out.
 *
 * Nullability: if args is NULL the stage is a safe no-op.
 */
typedef struct phys_manifold_build_args {
    const struct phys_contact_candidate *candidates; /**< Narrowphase output. */
    uint32_t candidate_count;         /**< Number of candidates. */
    struct phys_manifold_cache *cache; /**< Persistent manifold cache (mutated). */
    struct phys_manifold *manifolds_out; /**< Caller-allocated output buffer. */
    uint32_t *manifold_count_out;     /**< Receives number of manifolds written. */
    uint32_t max_manifolds;           /**< Capacity of manifolds_out. */
    uint64_t tick;                    /**< Current simulation tick. */
} phys_manifold_build_args_t;

/**
 * @brief Execute Stage 7: Manifold Build + Cache Merge.
 *
 * For each contact candidate:
 *   1. Get or create a cached manifold for the body pair.
 *   2. Save old impulses and feature IDs for warmstart matching.
 *   3. Clear the manifold and set body IDs.
 *   4. Add new contact points from the candidate.
 *   5. Restore impulses from old contacts matched by feature_id.
 *   6. Copy the manifold to the output buffer.
 *
 * @param args  Build arguments. NULL-safe (no-op if NULL).
 *
 * Side effects: mutates args->cache, writes to args->manifolds_out
 *               and args->manifold_count_out.
 */
void phys_stage_manifold_build(const phys_manifold_build_args_t *args);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_MANIFOLD_BUILD_H */
