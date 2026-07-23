/**
 * @file chunk_tree.h
 * @brief ADAPTIVE chunk partition of a scene AABB (rpg-zw99). Where the uniform
 *        @ref chunk_grid tiles every region at one size, this subdivides a coarse
 *        base grid toward DETAIL: cells overlapping a "detail" AABB (a building)
 *        split octree-style down to a minimum edge, while flat/empty cells stay
 *        large. Each leaf is a CHUNK -- its own extent, so at a fixed grid
 *        resolution (near_dim) a small building leaf gets a fine voxel and a big
 *        flat/terrain/mountain leaf gets a coarse one. Same query surface as
 *        chunk_grid (count / of_point / bounds / overlaps) so the baker + streamer
 *        can swap it in. Pure geometry, no GL.
 *
 * Ownership: nodes/leaf tables are allocated from the caller's arena (no free).
 * Nullability: pointer args required. Errors: build returns false on a NULL grid,
 * bad bounds, min/max <= 0, or arena exhaustion. Leaves tile the bounds with no
 * gaps; the final cell on an axis may extend past bounds max (empty space).
 */
#ifndef FERRUM_RENDERER_CHUNK_CHUNK_TREE_H
#define FERRUM_RENDERER_CHUNK_CHUNK_TREE_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/physics/aabb.h"

#ifdef __cplusplus
extern "C" {
#endif

struct arena;

/** One node of the adaptive partition. Internal nodes point at 8 contiguous
 *  children; leaf nodes carry a dense leaf index into the chunk arrays. */
typedef struct chunk_tree_node {
    float   min[3];       /**< Node cell minimum corner. */
    float   size;         /**< Cubic cell edge (metres). */
    int32_t first_child;  /**< Index of child 0 (8 contiguous), or -1 for a leaf. */
    int32_t leaf_index;   /**< Dense [0,leaf_count) for leaves, else -1. */
} chunk_tree_node_t;

/** An adaptive partition: a coarse base grid of root cells, each optionally
 *  subdivided into an octree of finer cells near detail. */
typedef struct chunk_tree {
    chunk_tree_node_t *nodes;   /**< Node pool (arena). nodes[0..root_count) = roots. */
    uint32_t           n_nodes;
    uint32_t           leaf_count; /**< Number of leaf chunks (the chunk count). */
    int32_t           *leaf_node;  /**< [leaf_count] leaf index -> node index (arena). */
    float              min[3];     /**< Base-grid origin (bounds min). */
    int32_t            gdims[3];   /**< Base (root) grid cell count per axis. */
    float              base_size;  /**< Root cell edge (== max_chunk). */
    float              margin;     /**< Overlap added to every side of a leaf's outer box. */
} chunk_tree_t;

/**
 * @brief Build the adaptive partition. The base grid tiles @p bounds in
 *        @p max_chunk cells; any cell whose octree cell overlaps a detail AABB
 *        (@p detail_min/@p detail_max, @p n_detail boxes, expanded by @p margin)
 *        subdivides by halving until its edge would fall below @p min_chunk. A
 *        @p detail_pad grows every detail box first (so a chunk's fine band also
 *        covers just OUTSIDE a building -- its street-facing facade).
 * @return false on NULL grid, bad bounds, min_chunk/max_chunk <= 0, min > max, or
 *         arena exhaustion. @p n_detail == 0 yields a uniform max_chunk grid.
 */
bool chunk_tree_build(chunk_tree_t *t, phys_aabb_t bounds,
                      float min_chunk, float max_chunk, float margin,
                      const float *detail_min, const float *detail_max,
                      uint32_t n_detail, float detail_pad, struct arena *arena);

/** @brief Leaf (chunk) count. */
uint32_t chunk_tree_count(const chunk_tree_t *t);

/** @brief Dense leaf index of the leaf whose INNER cell contains (@p x,@p y,@p z),
 *         or UINT32_MAX if the point is outside the base grid. O(tree depth). */
uint32_t chunk_tree_of_point(const chunk_tree_t *t, float x, float y, float z);

/**
 * @brief Inner (no-margin) and outer (margin-expanded) AABBs of leaf @p leaf.
 *        Either out-pointer may be NULL. Out-of-range @p leaf leaves them unset.
 */
void chunk_tree_bounds(const chunk_tree_t *t, uint32_t leaf,
                       phys_aabb_t *inner, phys_aabb_t *outer);

/** @brief True if @p box intersects leaf @p leaf's OUTER (margin) box. */
bool chunk_tree_overlaps_aabb(const chunk_tree_t *t, uint32_t leaf, phys_aabb_t box);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_CHUNK_CHUNK_TREE_H */
