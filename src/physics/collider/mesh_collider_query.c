/**
 * @file mesh_collider_query.c
 * @brief AABB overlap query against a mesh BVH.
 *
 * Stack-based traversal with linear fallback if the stack overflows.
 */

#include "ferrum/physics/mesh_collider.h"

#include <math.h>
#include <stddef.h>

#define MESH_BVH_QUERY_STACK_CAP 128u

uint32_t phys_mesh_bvh_query_aabb(const phys_mesh_bvh_t *bvh,
                                   const phys_aabb_t *query_aabb,
                                   uint32_t *out_tri_ids,
                                   uint32_t max_results) {
    if (!bvh || !bvh->nodes || bvh->node_count == 0 || !query_aabb ||
        !out_tri_ids || max_results == 0) {
        return 0;
    }

    /* Reject NaN/inf queries. */
    if (isnan(query_aabb->min.x) || isnan(query_aabb->min.y) ||
        isnan(query_aabb->min.z) || isnan(query_aabb->max.x) ||
        isnan(query_aabb->max.y) || isnan(query_aabb->max.z) ||
        isinf(query_aabb->min.x) || isinf(query_aabb->min.y) ||
        isinf(query_aabb->min.z) || isinf(query_aabb->max.x) ||
        isinf(query_aabb->max.y) || isinf(query_aabb->max.z)) {
        return 0;
    }

    if (bvh->root >= bvh->node_count) {
        return 0;
    }

    uint32_t stack[MESH_BVH_QUERY_STACK_CAP];
    uint32_t sp = 0;
    stack[sp++] = bvh->root;

    uint32_t out_count = 0;
    bool fallback_linear = false;

    while (sp) {
        uint32_t node_idx = stack[--sp];
        if (node_idx >= bvh->node_count) continue;

        const phys_mesh_bvh_node_t *n = &bvh->nodes[node_idx];
        if (!phys_aabb_overlap(&n->bounds, query_aabb)) continue;

        if (phys_mesh_bvh_node_is_leaf(n)) {
            if (out_count < max_results) {
                out_tri_ids[out_count++] = n->tri_index;
            } else {
                return out_count;
            }
        } else {
            if (sp + 2u > MESH_BVH_QUERY_STACK_CAP) {
                fallback_linear = true;
                break;
            }
            stack[sp++] = n->left;
            stack[sp++] = n->right;
        }
    }

    if (!fallback_linear) return out_count;

    /* Fallback: linear scan of all leaves. */
    out_count = 0;
    for (uint32_t i = 0; i < bvh->node_count; i++) {
        const phys_mesh_bvh_node_t *n = &bvh->nodes[i];
        if (!phys_mesh_bvh_node_is_leaf(n)) continue;
        if (!phys_aabb_overlap(&n->bounds, query_aabb)) continue;
        if (out_count >= max_results) return out_count;
        out_tri_ids[out_count++] = n->tri_index;
    }

    return out_count;
}
