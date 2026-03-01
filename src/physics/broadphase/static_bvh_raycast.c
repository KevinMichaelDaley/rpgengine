/**
 * @file static_bvh_raycast.c
 * @brief Ray traversal for the static BVH.
 *
 * Provides phys_static_bvh_raycast() which traverses the BVH tree,
 * testing ray-AABB intersections at each node and collecting leaf
 * item IDs that the ray potentially intersects.
 *
 * Non-static functions (1 of 4 max):
 *   1. phys_static_bvh_raycast
 *
 * Static helpers:
 *   - ray_aabb_intersect_  — slab-based ray-AABB test
 */

#include "ferrum/physics/static_bvh.h"

#include <math.h>
#include <stddef.h>

/* ── Ray-AABB slab test ────────────────────────────────────────── */

/**
 * @brief Test if a ray intersects an AABB using the slab method.
 *
 * Returns true if the ray overlaps the AABB within [0, max_distance].
 * Handles zero-component directions by treating the slab as infinite
 * (no division by zero).
 *
 * @param origin     Ray origin.
 * @param inv_dir    1/direction (precomputed for efficiency).
 * @param max_dist   Maximum ray distance.
 * @param aabb       AABB to test against.
 * @return true if ray intersects the AABB.
 */
static bool ray_aabb_intersect_(const float origin[3],
                                const float inv_dir[3],
                                float max_dist,
                                const phys_aabb_t *aabb) {
    float tmin = 0.0f;
    float tmax = max_dist;

    for (int i = 0; i < 3; i++) {
        float lo = ((&aabb->min.x)[i] - origin[i]) * inv_dir[i];
        float hi = ((&aabb->max.x)[i] - origin[i]) * inv_dir[i];

        /* Swap if direction is negative. */
        if (lo > hi) {
            float tmp = lo;
            lo = hi;
            hi = tmp;
        }

        if (lo > tmin) tmin = lo;
        if (hi < tmax) tmax = hi;

        if (tmin > tmax) return false;
    }

    return true;
}

/* ── Public API ────────────────────────────────────────────────── */

uint32_t phys_static_bvh_raycast(const phys_static_bvh_t *bvh,
                                 const float origin[3],
                                 const float direction[3],
                                 float max_distance,
                                 uint32_t *out_item_ids,
                                 uint32_t max_results) {
    if (!bvh || bvh->node_count == 0 || !out_item_ids || max_results == 0) {
        return 0;
    }
    if (!direction || max_distance <= 0.0f) return 0;

    /* Precompute inverse direction (handle near-zero with large value). */
    float inv_dir[3];
    for (int i = 0; i < 3; i++) {
        float d = direction[i];
        inv_dir[i] = (fabsf(d) < 1e-12f) ? 1e12f * (d >= 0.0f ? 1.0f : -1.0f) : 1.0f / d;
    }

    /* Iterative stack-based traversal. Max depth for a balanced BVH with
     * 2^20 (~1M) leaves is 20. Use 64 for safety. */
    uint32_t stack[64];
    uint32_t stack_top = 0;
    stack[stack_top++] = bvh->root;

    uint32_t count = 0;

    while (stack_top > 0) {
        uint32_t idx = stack[--stack_top];
        if (idx >= bvh->node_count) continue;

        const phys_static_bvh_node_t *node = &bvh->nodes[idx];

        /* Test ray against this node's AABB. */
        if (!ray_aabb_intersect_(origin, inv_dir, max_distance, &node->bounds)) {
            continue;
        }

        if (phys_static_bvh_node_is_leaf(node)) {
            /* Leaf: collect item_id. */
            if (count < max_results) {
                out_item_ids[count++] = node->item_id;
            }
        } else {
            /* Internal node: push children onto the stack. */
            if (stack_top < 62) {
                stack[stack_top++] = node->left;
                stack[stack_top++] = node->right;
            }
        }
    }

    return count;
}
