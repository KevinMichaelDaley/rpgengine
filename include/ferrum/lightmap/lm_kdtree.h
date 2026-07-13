/**
 * @file lm_kdtree.h
 * @brief Static 3D kd-tree over luxel positions for the lightmap baker's
 *        near-field patch queries.
 *
 * The progressive-shooting radiosity solver needs, for a source patch, the set
 * of nearby receiver patches (near field handled patch-to-patch; the far field
 * is folded into the SVO). This is a build-once, query-many median-split kd-tree
 * over borrowed point positions: build reorders an internal index array and
 * stores nodes in a caller-supplied arena.
 *
 * Ownership: the tree BORROWS the @p points array (must outlive the tree) and
 * allocates its nodes from @p arena (freed by resetting/popping the arena).
 * Nullability: all pointer args non-NULL; @p count may be 0 (empty tree ->
 * nearest returns UINT32_MAX, radius returns 0). Errors: build returns false if
 * the arena cannot fit the nodes. Not thread-safe to build; queries are const
 * and reentrant. Offline / not perf-critical.
 */
#ifndef FERRUM_LIGHTMAP_LM_KDTREE_H
#define FERRUM_LIGHTMAP_LM_KDTREE_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/math/vec3.h"
#include "ferrum/memory/arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/** One kd-tree node (median point + child links). */
typedef struct lm_kdnode {
    uint32_t point;       /**< Index into the points array at this node. */
    int32_t  left, right; /**< Child node indices, or -1. */
    uint8_t  axis;        /**< Split axis 0=x, 1=y, 2=z. */
} lm_kdnode_t;

/** A built kd-tree over a borrowed point array. */
typedef struct lm_kdtree {
    const vec3_t *points; /**< Borrowed positions. */
    lm_kdnode_t  *nodes;  /**< @p count nodes, from the arena. */
    uint32_t      count;  /**< Number of points / nodes. */
    int32_t       root;   /**< Root node index, or -1 if empty. */
} lm_kdtree_t;

/**
 * @brief Build a balanced kd-tree over @p points[0..count). Nodes come from
 *        @p arena. Returns false on arena exhaustion; true (with count==0,
 *        root==-1) for an empty set.
 */
bool lm_kdtree_build(lm_kdtree_t *tree, const vec3_t *points, uint32_t count,
                     arena_t *arena);

/**
 * @brief Index of the point nearest @p query, or UINT32_MAX if the tree is
 *        empty.
 */
uint32_t lm_kdtree_nearest(const lm_kdtree_t *tree, vec3_t query);

/**
 * @brief Collect the indices of all points within @p radius of @p query into
 *        @p out (up to @p out_cap). Returns the number that WOULD match (may
 *        exceed out_cap; only the first out_cap are written).
 */
uint32_t lm_kdtree_radius(const lm_kdtree_t *tree, vec3_t query, float radius,
                          uint32_t *out, uint32_t out_cap);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_LM_KDTREE_H */
