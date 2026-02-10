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

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_STATIC_BVH_H */
