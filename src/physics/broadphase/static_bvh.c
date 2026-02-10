/**
 * @file static_bvh.c
 * @brief SAH-based BVH builder for static geometry.
 */

#include "ferrum/physics/static_bvh.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

#include "ferrum/physics/phys_pool.h"

#define STATIC_BVH_INVALID_INDEX UINT32_MAX
#define STATIC_BVH_SAH_BINS 12u

typedef struct static_bvh_build_task {
    uint32_t node;
    uint32_t start;
    uint32_t count;
} static_bvh_build_task_t;

static float aabb_centroid_axis(const phys_aabb_t *aabb, int axis) {
    const float cx = 0.5f * (aabb->min.x + aabb->max.x);
    const float cy = 0.5f * (aabb->min.y + aabb->max.y);
    const float cz = 0.5f * (aabb->min.z + aabb->max.z);
    switch (axis) {
        case 0: return cx;
        case 1: return cy;
        default: return cz;
    }
}

static phys_aabb_t compute_bounds_range(const phys_aabb_t *items,
                                       const uint32_t *indices,
                                       uint32_t start,
                                       uint32_t count) {
    phys_aabb_t b = items[indices[start]];
    for (uint32_t i = 1; i < count; i++) {
        phys_aabb_merge(&b, &b, &items[indices[start + i]]);
    }
    return b;
}

static uint32_t partition_range_sah(const phys_aabb_t *items,
                                   uint32_t *indices,
                                   uint32_t start,
                                   uint32_t count) {
    /* Compute centroid bounds and choose split axis. */
    float cmin[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
    float cmax[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

    for (uint32_t i = 0; i < count; i++) {
        const phys_aabb_t *a = &items[indices[start + i]];
        const float cx = 0.5f * (a->min.x + a->max.x);
        const float cy = 0.5f * (a->min.y + a->max.y);
        const float cz = 0.5f * (a->min.z + a->max.z);
        if (cx < cmin[0]) cmin[0] = cx;
        if (cy < cmin[1]) cmin[1] = cy;
        if (cz < cmin[2]) cmin[2] = cz;
        if (cx > cmax[0]) cmax[0] = cx;
        if (cy > cmax[1]) cmax[1] = cy;
        if (cz > cmax[2]) cmax[2] = cz;
    }

    float ext[3] = {cmax[0] - cmin[0], cmax[1] - cmin[1], cmax[2] - cmin[2]};
    int axis = 0;
    if (ext[1] > ext[axis]) axis = 1;
    if (ext[2] > ext[axis]) axis = 2;

    if (ext[axis] <= 1e-9f) {
        return count / 2u;
    }

    typedef struct sah_bin {
        phys_aabb_t bounds;
        uint32_t count;
        uint8_t valid;
    } sah_bin_t;

    sah_bin_t bins[STATIC_BVH_SAH_BINS];
    memset(bins, 0, sizeof(bins));

    const float inv_extent = (float)STATIC_BVH_SAH_BINS / ext[axis];
    const float minc = cmin[axis];

    for (uint32_t i = 0; i < count; i++) {
        const uint32_t item_index = indices[start + i];
        const phys_aabb_t *a = &items[item_index];
        float c = aabb_centroid_axis(a, axis);
        int bi = (int)((c - minc) * inv_extent);
        if (bi < 0) bi = 0;
        if ((uint32_t)bi >= STATIC_BVH_SAH_BINS) bi = (int)STATIC_BVH_SAH_BINS - 1;

        sah_bin_t *b = &bins[bi];
        if (!b->valid) {
            b->bounds = *a;
            b->valid = 1;
        } else {
            phys_aabb_merge(&b->bounds, &b->bounds, a);
        }
        b->count++;
    }

    /* Prefix/suffix accumulations. */
    phys_aabb_t prefix_bounds[STATIC_BVH_SAH_BINS];
    uint32_t prefix_count[STATIC_BVH_SAH_BINS];
    uint8_t prefix_valid[STATIC_BVH_SAH_BINS];

    phys_aabb_t suffix_bounds[STATIC_BVH_SAH_BINS];
    uint32_t suffix_count[STATIC_BVH_SAH_BINS];
    uint8_t suffix_valid[STATIC_BVH_SAH_BINS];

    memset(prefix_count, 0, sizeof(prefix_count));
    memset(prefix_valid, 0, sizeof(prefix_valid));
    memset(suffix_count, 0, sizeof(suffix_count));
    memset(suffix_valid, 0, sizeof(suffix_valid));

    for (uint32_t i = 0; i < STATIC_BVH_SAH_BINS; i++) {
        if (i == 0) {
            if (bins[i].valid) {
                prefix_bounds[i] = bins[i].bounds;
                prefix_valid[i] = 1;
            }
            prefix_count[i] = bins[i].count;
        } else {
            prefix_bounds[i] = prefix_bounds[i - 1];
            prefix_count[i] = prefix_count[i - 1] + bins[i].count;
            prefix_valid[i] = prefix_valid[i - 1];
            if (bins[i].valid) {
                if (!prefix_valid[i - 1]) {
                    prefix_bounds[i] = bins[i].bounds;
                    prefix_valid[i] = 1;
                } else {
                    phys_aabb_merge(&prefix_bounds[i], &prefix_bounds[i], &bins[i].bounds);
                }
            }
        }
    }

    for (int i = (int)STATIC_BVH_SAH_BINS - 1; i >= 0; i--) {
        if (i == (int)STATIC_BVH_SAH_BINS - 1) {
            if (bins[i].valid) {
                suffix_bounds[i] = bins[i].bounds;
                suffix_valid[i] = 1;
            }
            suffix_count[i] = bins[i].count;
        } else {
            suffix_bounds[i] = suffix_bounds[i + 1];
            suffix_count[i] = suffix_count[i + 1] + bins[i].count;
            suffix_valid[i] = suffix_valid[i + 1];
            if (bins[i].valid) {
                if (!suffix_valid[i + 1]) {
                    suffix_bounds[i] = bins[i].bounds;
                    suffix_valid[i] = 1;
                } else {
                    phys_aabb_merge(&suffix_bounds[i], &suffix_bounds[i], &bins[i].bounds);
                }
            }
        }
    }

    /* Find best split between bins. */
    float best_cost = FLT_MAX;
    int best_split = -1;
    for (uint32_t i = 0; i + 1 < STATIC_BVH_SAH_BINS; i++) {
        if (!prefix_valid[i] || !suffix_valid[i + 1]) {
            continue;
        }
        uint32_t lc = prefix_count[i];
        uint32_t rc = suffix_count[i + 1];
        if (lc == 0 || rc == 0) {
            continue;
        }
        float cost = phys_aabb_surface_area(&prefix_bounds[i]) * (float)lc +
                     phys_aabb_surface_area(&suffix_bounds[i + 1]) * (float)rc;
        if (cost < best_cost) {
            best_cost = cost;
            best_split = (int)i;
        }
    }

    /* Partition by best_split (bin index). Fallback to a simple pivot/median. */
    uint32_t left_count = 0;
    if (best_split >= 0) {
        uint32_t i = start;
        uint32_t j = start + count - 1;
        while (i <= j) {
            const phys_aabb_t *a = &items[indices[i]];
            float c = aabb_centroid_axis(a, axis);
            int bi = (int)((c - minc) * inv_extent);
            if (bi < 0) bi = 0;
            if ((uint32_t)bi >= STATIC_BVH_SAH_BINS) bi = (int)STATIC_BVH_SAH_BINS - 1;

            if (bi <= best_split) {
                i++;
            } else {
                uint32_t tmp = indices[i];
                indices[i] = indices[j];
                indices[j] = tmp;
                if (j == 0) {
                    break;
                }
                j--;
            }
        }
        left_count = i - start;
    }

    if (left_count == 0 || left_count == count) {
        const float pivot = 0.5f * (cmin[axis] + cmax[axis]);
        uint32_t i = start;
        uint32_t j = start + count - 1;
        while (i <= j) {
            const phys_aabb_t *a = &items[indices[i]];
            float c = aabb_centroid_axis(a, axis);
            if (c <= pivot) {
                i++;
            } else {
                uint32_t tmp = indices[i];
                indices[i] = indices[j];
                indices[j] = tmp;
                if (j == 0) {
                    break;
                }
                j--;
            }
        }
        left_count = i - start;
        if (left_count == 0 || left_count == count) {
            left_count = count / 2u;
        }
    }

    if (left_count == 0) {
        left_count = 1;
    }
    if (left_count >= count) {
        left_count = count - 1;
    }
    return left_count;
}

void phys_static_bvh_build(phys_static_bvh_t *out_bvh,
                          const phys_aabb_t *item_aabbs,
                          const uint32_t *item_ids,
                          uint32_t item_count,
                          phys_frame_arena_t *arena) {
    if (!out_bvh) {
        return;
    }

    out_bvh->nodes = NULL;
    out_bvh->node_count = 0;
    out_bvh->root = STATIC_BVH_INVALID_INDEX;

    if (item_count == 0) {
        return;
    }
    if (!item_aabbs || !arena) {
        return;
    }

    const uint32_t max_nodes = 2u * item_count - 1u;

    phys_static_bvh_node_t *nodes = phys_frame_arena_alloc(
        arena, max_nodes * sizeof(*nodes), _Alignof(phys_static_bvh_node_t));
    uint32_t *indices = phys_frame_arena_alloc(
        arena, item_count * sizeof(*indices), _Alignof(uint32_t));
    static_bvh_build_task_t *stack = phys_frame_arena_alloc(
        arena, max_nodes * sizeof(*stack), _Alignof(static_bvh_build_task_t));

    if (!nodes || !indices || !stack) {
        return;
    }

    for (uint32_t i = 0; i < item_count; i++) {
        indices[i] = i;
    }

    uint32_t next_node = 1;
    uint32_t sp = 0;
    stack[sp++] = (static_bvh_build_task_t){.node = 0, .start = 0, .count = item_count};

    while (sp) {
        static_bvh_build_task_t task = stack[--sp];
        phys_static_bvh_node_t *n = &nodes[task.node];

        n->bounds = compute_bounds_range(item_aabbs, indices, task.start, task.count);

        if (task.count == 1) {
            uint32_t item_index = indices[task.start];
            n->left = STATIC_BVH_INVALID_INDEX;
            n->right = STATIC_BVH_INVALID_INDEX;
            n->item_id = item_ids ? item_ids[item_index] : item_index;
            continue;
        }

        /* Internal node: allocate two children. */
        uint32_t left_node = next_node++;
        uint32_t right_node = next_node++;
        n->left = left_node;
        n->right = right_node;
        n->item_id = STATIC_BVH_INVALID_INDEX;

        uint32_t left_count = partition_range_sah(item_aabbs, indices, task.start, task.count);
        uint32_t right_count = task.count - left_count;

        /* Push children (right first so left is processed next). */
        stack[sp++] = (static_bvh_build_task_t){
            .node = right_node,
            .start = task.start + left_count,
            .count = right_count,
        };
        stack[sp++] = (static_bvh_build_task_t){
            .node = left_node,
            .start = task.start,
            .count = left_count,
        };

        /* stack capacity is max_nodes and sp is bounded by tree size. */
    }

    out_bvh->nodes = nodes;
    out_bvh->node_count = next_node;
    out_bvh->root = 0;
}

/* ── Queries / helpers ───────────────────────────────────────────── */

#define STATIC_BVH_QUERY_STACK_CAP 128u

static uint32_t static_bvh_grid_hash(int32_t x, int32_t y, int32_t z) {
    return ((uint32_t)x * 73856093u) ^ ((uint32_t)y * 19349663u) ^
           ((uint32_t)z * 83492791u);
}

uint32_t phys_static_bvh_query_aabb(const phys_static_bvh_t *bvh,
                                   const phys_aabb_t *query_aabb,
                                   uint32_t *out_item_ids,
                                   uint32_t max_results) {
    if (!bvh || !bvh->nodes || bvh->node_count == 0 || !query_aabb ||
        !out_item_ids || max_results == 0) {
        return 0;
    }

    if (isnan(query_aabb->min.x) || isnan(query_aabb->min.y) ||
        isnan(query_aabb->min.z) || isnan(query_aabb->max.x) ||
        isnan(query_aabb->max.y) || isnan(query_aabb->max.z) ||
        isinf(query_aabb->min.x) || isinf(query_aabb->min.y) ||
        isinf(query_aabb->min.z) || isinf(query_aabb->max.x) ||
        isinf(query_aabb->max.y) || isinf(query_aabb->max.z)) {
        return 0;
    }

    uint32_t stack[STATIC_BVH_QUERY_STACK_CAP];
    uint32_t sp = 0;

    if (bvh->root >= bvh->node_count) {
        return 0;
    }

    stack[sp++] = bvh->root;

    uint32_t out_count = 0;

    bool fallback_linear = false;

    while (sp) {
        uint32_t node_idx = stack[--sp];
        if (node_idx >= bvh->node_count) {
            continue;
        }

        const phys_static_bvh_node_t *n = &bvh->nodes[node_idx];
        if (!phys_aabb_overlap(&n->bounds, query_aabb)) {
            continue;
        }

        if (phys_static_bvh_node_is_leaf(n)) {
            if (out_count < max_results) {
                out_item_ids[out_count++] = n->item_id;
            } else {
                return out_count;
            }
        } else {
            if (sp + 2u > STATIC_BVH_QUERY_STACK_CAP) {
                fallback_linear = true;
                break;
            }
            stack[sp++] = n->left;
            stack[sp++] = n->right;
        }
    }

    if (!fallback_linear) {
        return out_count;
    }

    /* Fallback: linear scan of leaves (correct but slower). */
    out_count = 0;
    for (uint32_t i = 0; i < bvh->node_count; ++i) {
        const phys_static_bvh_node_t *n = &bvh->nodes[i];
        if (!phys_static_bvh_node_is_leaf(n)) {
            continue;
        }
        if (!phys_aabb_overlap(&n->bounds, query_aabb)) {
            continue;
        }
        if (out_count >= max_results) {
            return out_count;
        }
        out_item_ids[out_count++] = n->item_id;
    }

    return out_count;
}

void phys_static_bvh_build_bucket_flags(const phys_static_bvh_t *bvh,
                                       uint32_t bucket_count,
                                       float cell_size,
                                       uint8_t *out_bucket_flags) {
    if (!out_bucket_flags || bucket_count == 0) {
        return;
    }

    memset(out_bucket_flags, 0, (size_t)bucket_count * sizeof(uint8_t));

    if (!bvh || !bvh->nodes || bvh->node_count == 0 || cell_size <= 0.0f) {
        return;
    }

    const float inv_cell_size = 1.0f / cell_size;
    const uint32_t mask = bucket_count - 1u;

    for (uint32_t i = 0; i < bvh->node_count; ++i) {
        const phys_static_bvh_node_t *n = &bvh->nodes[i];
        if (!phys_static_bvh_node_is_leaf(n)) {
            continue;
        }

        const phys_aabb_t *aabb = &n->bounds;

        if (isnan(aabb->min.x) || isnan(aabb->min.y) || isnan(aabb->min.z) ||
            isnan(aabb->max.x) || isnan(aabb->max.y) || isnan(aabb->max.z) ||
            isinf(aabb->min.x) || isinf(aabb->min.y) || isinf(aabb->min.z) ||
            isinf(aabb->max.x) || isinf(aabb->max.y) || isinf(aabb->max.z)) {
            continue;
        }

        int32_t min_cx = (int32_t)floorf(aabb->min.x * inv_cell_size);
        int32_t min_cy = (int32_t)floorf(aabb->min.y * inv_cell_size);
        int32_t min_cz = (int32_t)floorf(aabb->min.z * inv_cell_size);
        int32_t max_cx = (int32_t)floorf(aabb->max.x * inv_cell_size);
        int32_t max_cy = (int32_t)floorf(aabb->max.y * inv_cell_size);
        int32_t max_cz = (int32_t)floorf(aabb->max.z * inv_cell_size);

        int32_t max_cells_per_axis = (int32_t)bucket_count;
        if ((max_cx - min_cx) > max_cells_per_axis) {
            max_cx = min_cx + max_cells_per_axis;
        }
        if ((max_cy - min_cy) > max_cells_per_axis) {
            max_cy = min_cy + max_cells_per_axis;
        }
        if ((max_cz - min_cz) > max_cells_per_axis) {
            max_cz = min_cz + max_cells_per_axis;
        }

        for (int32_t cz = min_cz; cz <= max_cz; ++cz) {
            for (int32_t cy = min_cy; cy <= max_cy; ++cy) {
                for (int32_t cx = min_cx; cx <= max_cx; ++cx) {
                    uint32_t hash = static_bvh_grid_hash(cx, cy, cz);
                    uint32_t idx = hash & mask;
                    out_bucket_flags[idx] = 1u;
                }
            }
        }
    }
}
