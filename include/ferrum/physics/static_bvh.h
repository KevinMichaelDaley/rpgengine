#ifndef FERRUM_PHYSICS_STATIC_BVH_H
#define FERRUM_PHYSICS_STATIC_BVH_H

/** @file
 * @brief Static BVH (Bounding Volume Hierarchy) for static geometry.
 *
 * Provides a simple SAH-based BVH builder over a set of AABBs.
 * The BVH is intended for large static worlds where a grid broadphase
 * is inefficient.
 *
 * Ownership: the BVH does not own its memory. The builder allocates the
 * node array (and temporary scratch) from a caller-provided arena.
 *
 * Nullability: all public functions are NULL-safe.
 */

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/physics/aabb.h"

struct phys_frame_arena;

#ifdef __cplusplus
extern "C" {
#endif

/* ── Node ───────────────────────────────────────────────────────── */

/**
 * @brief A BVH node.
 *
 * Leaf nodes have left == right == UINT32_MAX and store an item_id.
 * Internal nodes store child indices and have item_id == UINT32_MAX.
 */
typedef struct phys_static_bvh_node {
    phys_aabb_t bounds; /**< Bounding box of this node. */
    uint32_t left;      /**< Left child index, or UINT32_MAX if leaf. */
    uint32_t right;     /**< Right child index, or UINT32_MAX if leaf. */
    uint32_t item_id;   /**< Leaf item id (defaults to item index). */
} phys_static_bvh_node_t;

/* ── BVH container ──────────────────────────────────────────────── */

/**
 * @brief Static BVH container.
 *
 * The node array is laid out as a binary tree addressed by indices.
 *
 * Invariant: if node_count > 0 then root < node_count.
 */
typedef struct phys_static_bvh {
    phys_static_bvh_node_t *nodes; /**< Arena-allocated node array. */
    uint32_t node_count;           /**< Number of valid nodes. */
    uint32_t root;                 /**< Root node index, or UINT32_MAX if empty. */
} phys_static_bvh_t;

/* ── Helpers ────────────────────────────────────────────────────── */

/**
 * @brief Return true if @p node is a leaf.
 */
static inline bool phys_static_bvh_node_is_leaf(const phys_static_bvh_node_t *node) {
    return node && node->left == UINT32_MAX;
}

/* ── Build API ──────────────────────────────────────────────────── */

/**
 * @brief Build a static BVH from a set of AABBs.
 *
 * This is a top-down SAH (Surface Area Heuristic) build with binning.
 *
 * @param out_bvh    Output BVH (NULL-safe no-op if NULL).
 * @param item_aabbs Input AABB array of length @p item_count (may be NULL if item_count==0).
 * @param item_ids   Optional array of length @p item_count. If NULL, leaf item_id defaults to the item index.
 * @param item_count Number of items.
 * @param arena      Arena used for node + scratch allocations (must be non-NULL when item_count>0).
 *
 * Side effects: allocates from @p arena.
 */
void phys_static_bvh_build(phys_static_bvh_t *out_bvh,
                          const phys_aabb_t *item_aabbs,
                          const uint32_t *item_ids,
                          uint32_t item_count,
                          struct phys_frame_arena *arena);

/* ── Query API ──────────────────────────────────────────────────── */

/**
 * @brief Query the BVH for leaf items whose bounds overlap @p query_aabb.
 *
 * @param bvh          BVH to query (NULL-safe; returns 0 if NULL/empty).
 * @param query_aabb   Query AABB (NULL-safe; returns 0 if NULL).
 * @param out_item_ids Output array of item IDs.
 * @param max_results  Capacity of @p out_item_ids.
 *
 * @return Number of item IDs written (may be < total overlaps if capped).
 */
uint32_t phys_static_bvh_query_aabb(const phys_static_bvh_t *bvh,
                                   const phys_aabb_t *query_aabb,
                                   uint32_t *out_item_ids,
                                   uint32_t max_results);

/**
 * @brief Precompute which spatial-grid hash buckets intersect any static BVH leaf.
 *
 * This builds a flag array indexed by the grid's hash bucket index (same hashing
 * as the dynamic spatial grid), so broadphase can skip BVH queries when a body's
 * AABB touches only buckets with no static geometry.
 *
 * @param bvh              BVH to rasterize (NULL-safe; fills zeros if NULL/empty).
 * @param bucket_count     Number of hash buckets in the spatial grid.
 * @param cell_size        Spatial grid cell size.
 * @param out_bucket_flags Output array of length @p bucket_count (filled with 0/1).
 */
void phys_static_bvh_build_bucket_flags(const phys_static_bvh_t *bvh,
                                       uint32_t bucket_count,
                                       float cell_size,
                                       uint8_t *out_bucket_flags);

/**
 * @brief Raycast through the BVH and collect leaf item IDs.
 *
 * Traverses the BVH using slab-based ray-AABB tests, returning the
 * item IDs of all leaves whose bounding boxes are intersected by the ray.
 * These are broadphase candidates; the caller must perform narrowphase
 * tests against the actual collider shapes.
 *
 * @param bvh          BVH to query (NULL-safe; returns 0).
 * @param origin       Ray origin (float[3]).
 * @param direction    Normalized ray direction (float[3]).
 * @param max_distance Maximum ray distance (must be > 0).
 * @param out_item_ids Output array for leaf item IDs.
 * @param max_results  Capacity of @p out_item_ids.
 * @return Number of item IDs written.
 */
uint32_t phys_static_bvh_raycast(const phys_static_bvh_t *bvh,
                                 const float origin[3],
                                 const float direction[3],
                                 float max_distance,
                                 uint32_t *out_item_ids,
                                 uint32_t max_results);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_STATIC_BVH_H */
