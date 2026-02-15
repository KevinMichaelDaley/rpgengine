/**
 * @file mesh_collider_build.c
 * @brief SAH-based BVH builder over triangles for mesh colliders.
 *
 * Top-down build with 12-bin SAH partitioning, matching the pattern
 * used by the static body BVH (static_bvh.c).  Iterative task-stack
 * approach avoids deep recursion.
 */

#include "ferrum/physics/mesh_collider.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

#include "ferrum/physics/phys_pool.h"

#define MESH_BVH_INVALID UINT32_MAX
#define MESH_BVH_SAH_BINS 12u

/* ── Build task for iterative construction ──────────────────────── */

typedef struct mesh_bvh_build_task {
    uint32_t node;
    uint32_t start;
    uint32_t count;
} mesh_bvh_build_task_t;

/* ── Helpers ────────────────────────────────────────────────────── */

static float tri_centroid_axis(const phys_aabb_t *aabb, int axis) {
    switch (axis) {
        case 0: return 0.5f * (aabb->min.x + aabb->max.x);
        case 1: return 0.5f * (aabb->min.y + aabb->max.y);
        default: return 0.5f * (aabb->min.z + aabb->max.z);
    }
}

static phys_aabb_t compute_bounds_range(const phys_aabb_t *aabbs,
                                        const uint32_t *indices,
                                        uint32_t start,
                                        uint32_t count) {
    phys_aabb_t b = aabbs[indices[start]];
    for (uint32_t i = 1; i < count; i++) {
        phys_aabb_merge(&b, &b, &aabbs[indices[start + i]]);
    }
    return b;
}

/* ── SAH partition ──────────────────────────────────────────────── */

static uint32_t partition_sah(const phys_aabb_t *aabbs,
                              uint32_t *indices,
                              uint32_t start,
                              uint32_t count) {
    /* Compute centroid bounds, choose longest axis. */
    float cmin[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
    float cmax[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

    for (uint32_t i = 0; i < count; i++) {
        const phys_aabb_t *a = &aabbs[indices[start + i]];
        for (int ax = 0; ax < 3; ax++) {
            float c = tri_centroid_axis(a, ax);
            if (c < cmin[ax]) cmin[ax] = c;
            if (c > cmax[ax]) cmax[ax] = c;
        }
    }

    float ext[3] = {cmax[0] - cmin[0], cmax[1] - cmin[1], cmax[2] - cmin[2]};
    int axis = 0;
    if (ext[1] > ext[axis]) axis = 1;
    if (ext[2] > ext[axis]) axis = 2;

    /* Degenerate: all centroids coincident. */
    if (ext[axis] <= 1e-9f) {
        return count / 2u;
    }

    /* Bin triangles along the chosen axis. */
    typedef struct sah_bin {
        phys_aabb_t bounds;
        uint32_t count;
        uint8_t valid;
    } sah_bin_t;

    sah_bin_t bins[MESH_BVH_SAH_BINS];
    memset(bins, 0, sizeof(bins));

    const float inv_extent = (float)MESH_BVH_SAH_BINS / ext[axis];
    const float minc = cmin[axis];

    for (uint32_t i = 0; i < count; i++) {
        const phys_aabb_t *a = &aabbs[indices[start + i]];
        float c = tri_centroid_axis(a, axis);
        int bi = (int)((c - minc) * inv_extent);
        if (bi < 0) bi = 0;
        if ((uint32_t)bi >= MESH_BVH_SAH_BINS) bi = (int)MESH_BVH_SAH_BINS - 1;

        sah_bin_t *b = &bins[bi];
        if (!b->valid) {
            b->bounds = *a;
            b->valid = 1;
        } else {
            phys_aabb_merge(&b->bounds, &b->bounds, a);
        }
        b->count++;
    }

    /* Prefix/suffix sweep for SAH cost evaluation. */
    phys_aabb_t prefix_bounds[MESH_BVH_SAH_BINS];
    uint32_t    prefix_count[MESH_BVH_SAH_BINS];
    uint8_t     prefix_valid[MESH_BVH_SAH_BINS];

    phys_aabb_t suffix_bounds[MESH_BVH_SAH_BINS];
    uint32_t    suffix_count[MESH_BVH_SAH_BINS];
    uint8_t     suffix_valid[MESH_BVH_SAH_BINS];

    memset(prefix_count, 0, sizeof(prefix_count));
    memset(prefix_valid, 0, sizeof(prefix_valid));
    memset(suffix_count, 0, sizeof(suffix_count));
    memset(suffix_valid, 0, sizeof(suffix_valid));

    for (uint32_t i = 0; i < MESH_BVH_SAH_BINS; i++) {
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
                    phys_aabb_merge(&prefix_bounds[i],
                                    &prefix_bounds[i], &bins[i].bounds);
                }
            }
        }
    }

    for (int i = (int)MESH_BVH_SAH_BINS - 1; i >= 0; i--) {
        if (i == (int)MESH_BVH_SAH_BINS - 1) {
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
                    phys_aabb_merge(&suffix_bounds[i],
                                    &suffix_bounds[i], &bins[i].bounds);
                }
            }
        }
    }

    /* Evaluate SAH cost at each split plane. */
    float best_cost = FLT_MAX;
    int best_split = -1;
    for (uint32_t i = 0; i + 1 < MESH_BVH_SAH_BINS; i++) {
        if (!prefix_valid[i] || !suffix_valid[i + 1]) continue;
        uint32_t lc = prefix_count[i];
        uint32_t rc = suffix_count[i + 1];
        if (lc == 0 || rc == 0) continue;
        float cost = phys_aabb_surface_area(&prefix_bounds[i]) * (float)lc +
                     phys_aabb_surface_area(&suffix_bounds[i + 1]) * (float)rc;
        if (cost < best_cost) {
            best_cost = cost;
            best_split = (int)i;
        }
    }

    /* Partition indices by the best split bin. */
    uint32_t left_count = 0;
    if (best_split >= 0) {
        uint32_t i = start;
        uint32_t j = start + count - 1;
        while (i <= j) {
            const phys_aabb_t *a = &aabbs[indices[i]];
            float c = tri_centroid_axis(a, axis);
            int bi = (int)((c - minc) * inv_extent);
            if (bi < 0) bi = 0;
            if ((uint32_t)bi >= MESH_BVH_SAH_BINS) bi = (int)MESH_BVH_SAH_BINS - 1;

            if (bi <= best_split) {
                i++;
            } else {
                uint32_t tmp = indices[i];
                indices[i] = indices[j];
                indices[j] = tmp;
                if (j == 0) break;
                j--;
            }
        }
        left_count = i - start;
    }

    /* Fallback: midpoint partition if SAH produced a degenerate split. */
    if (left_count == 0 || left_count == count) {
        const float pivot = 0.5f * (cmin[axis] + cmax[axis]);
        uint32_t i = start;
        uint32_t j = start + count - 1;
        while (i <= j) {
            float c = tri_centroid_axis(&aabbs[indices[i]], axis);
            if (c <= pivot) {
                i++;
            } else {
                uint32_t tmp = indices[i];
                indices[i] = indices[j];
                indices[j] = tmp;
                if (j == 0) break;
                j--;
            }
        }
        left_count = i - start;
        if (left_count == 0 || left_count == count) {
            left_count = count / 2u;
        }
    }

    if (left_count == 0) left_count = 1;
    if (left_count >= count) left_count = count - 1;
    return left_count;
}

/* ── Public build API ───────────────────────────────────────────── */

void phys_mesh_bvh_build(phys_mesh_bvh_t *out_bvh,
                          const phys_triangle_t *triangles,
                          uint32_t tri_count,
                          phys_frame_arena_t *arena) {
    if (!out_bvh) return;

    out_bvh->nodes      = NULL;
    out_bvh->node_count = 0;
    out_bvh->root       = MESH_BVH_INVALID;
    out_bvh->triangles  = triangles;
    out_bvh->tri_count  = tri_count;

    if (tri_count == 0 || !triangles || !arena) return;

    const uint32_t max_nodes = 2u * tri_count - 1u;

    /* Allocate nodes, index array, per-triangle AABBs, and build stack. */
    phys_mesh_bvh_node_t *nodes = phys_frame_arena_alloc(
        arena, max_nodes * sizeof(*nodes), _Alignof(phys_mesh_bvh_node_t));
    uint32_t *indices = phys_frame_arena_alloc(
        arena, tri_count * sizeof(*indices), _Alignof(uint32_t));
    phys_aabb_t *tri_aabbs = phys_frame_arena_alloc(
        arena, tri_count * sizeof(*tri_aabbs), _Alignof(phys_aabb_t));
    mesh_bvh_build_task_t *stack = phys_frame_arena_alloc(
        arena, max_nodes * sizeof(*stack), _Alignof(mesh_bvh_build_task_t));

    if (!nodes || !indices || !tri_aabbs || !stack) return;

    /* Precompute per-triangle AABBs and initialize index array. */
    for (uint32_t i = 0; i < tri_count; i++) {
        tri_aabbs[i] = phys_triangle_aabb(&triangles[i]);
        indices[i] = i;
    }

    /* Iterative top-down build. */
    uint32_t next_node = 1;
    uint32_t sp = 0;
    stack[sp++] = (mesh_bvh_build_task_t){.node = 0, .start = 0, .count = tri_count};

    while (sp) {
        mesh_bvh_build_task_t task = stack[--sp];
        phys_mesh_bvh_node_t *n = &nodes[task.node];

        n->bounds = compute_bounds_range(tri_aabbs, indices, task.start, task.count);

        if (task.count == 1) {
            /* Leaf. */
            n->left      = MESH_BVH_INVALID;
            n->right     = MESH_BVH_INVALID;
            n->tri_index = indices[task.start];
            continue;
        }

        /* Internal node. */
        uint32_t left_node  = next_node++;
        uint32_t right_node = next_node++;
        n->left      = left_node;
        n->right     = right_node;
        n->tri_index = MESH_BVH_INVALID;

        uint32_t left_count = partition_sah(tri_aabbs, indices,
                                             task.start, task.count);
        uint32_t right_count = task.count - left_count;

        /* Push children (right first so left processed next). */
        stack[sp++] = (mesh_bvh_build_task_t){
            .node = right_node,
            .start = task.start + left_count,
            .count = right_count,
        };
        stack[sp++] = (mesh_bvh_build_task_t){
            .node = left_node,
            .start = task.start,
            .count = left_count,
        };
    }

    out_bvh->nodes      = nodes;
    out_bvh->node_count = next_node;
    out_bvh->root       = 0;
}
