#ifndef FERRUM_PHYSICS_BROADPHASE_H
#define FERRUM_PHYSICS_BROADPHASE_H

/** @file
 * @brief Broadphase collision detection stage.
 *
 * Iterates active tier bodies, queries the spatial grid for AABB
 * overlap candidates, performs precise AABB overlap tests, and
 * outputs collision pairs.  Static-static pairs are excluded.
 *
 * All public functions are NULL-safe.
 */

#include <stdint.h>

struct phys_aabb;
struct phys_spatial_grid;
struct phys_tier_lists;
struct phys_frame_arena;
struct phys_body;
struct phys_static_bvh;

#ifdef __cplusplus
extern "C" {
#endif

/* ── Collision pair ─────────────────────────────────────────────── */

/**
 * @brief A pair of body indices identified by the broadphase.
 *
 * Invariant: body_a < body_b (canonical ordering, no duplicates).
 */
typedef struct phys_collision_pair {
    uint32_t body_a; /**< Lower body index. */
    uint32_t body_b; /**< Higher body index. */
} phys_collision_pair_t;

/* ── Broadphase arguments ──────────────────────────────────────── */

/**
 * @brief Input/output arguments for the broadphase stage.
 *
 * Ownership: the caller owns all pointed-to arrays.  The stage
 * only reads bodies, aabbs, grid, and tier_lists; it writes to
 * pairs_out and pair_count_out.
 *
 * Nullability: if args is NULL or any required field is NULL,
 * the stage is a safe no-op.
 */
typedef struct phys_broadphase_args {
    const struct phys_body *bodies;           /**< Body array (read-only). */
    const struct phys_aabb *aabbs;            /**< Per-body AABB array. */
    const struct phys_spatial_grid *grid;     /**< Spatial hash grid. */
    const struct phys_tier_lists *tier_lists; /**< Active tier lists. */

    /** Optional: static BVH for generating dynamic-vs-static pairs when static
     * bodies are excluded from the spatial grid.
     *
     * When non-NULL, broadphase will NOT emit pairs against static bodies found
     * via the grid query; static pairs are emitted via BVH instead.
     */
    const struct phys_static_bvh *static_bvh;

    /** Optional: per-grid-bucket (hash bucket) flags indicating presence of any
     * static BVH leaf. When provided and sized to grid->cell_count, broadphase
     * can skip BVH queries for bodies whose AABB touches only unflagged buckets.
     */
    const uint8_t *static_bucket_flags;
    uint32_t static_bucket_flag_count;

    phys_collision_pair_t *pairs_out; /**< Caller-allocated output buffer. */
    uint32_t max_pairs;               /**< Capacity of pairs_out. */
    uint32_t *pair_count_out;         /**< Receives final pair count. */
} phys_broadphase_args_t;

/* ── Public API ─────────────────────────────────────────────────── */

/**
 * @brief Execute the broadphase stage.
 *
 * For each body in active tiers (T0–T4), queries the spatial grid
 * for candidate overlaps, performs precise AABB overlap tests, skips
 * static-static pairs, and writes unique (body_a < body_b) pairs to
 * the output buffer.
 *
 * @param args  Broadphase arguments (NULL-safe, no-op if NULL).
 *
 * Side effects: writes to args->pairs_out and args->pair_count_out.
 */
void phys_stage_broadphase(const phys_broadphase_args_t *args);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_BROADPHASE_H */
