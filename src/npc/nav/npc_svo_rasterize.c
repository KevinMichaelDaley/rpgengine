/**
 * @file npc_svo_rasterize.c
 * @brief Triangle-to-voxel rasterization into the SVO.
 *
 * Uses conservative rasterization: any voxel whose AABB overlaps the
 * triangle AABB is marked SOLID. This is safe for navigation (may
 * slightly over-estimate solids, never under-estimate).
 *
 * Non-static functions (2 of 4 max):
 *   1. npc_svo_rasterize_triangle
 *   2. npc_svo_rasterize_mesh
 *
 * Static helpers:
 *   - world_to_voxel_range_  — compute integer voxel range for an AABB
 *   - ensure_leaf_           — walk/create SVO path to a leaf voxel
 *   - mark_voxel_solid_      — set SOLID flag on a leaf
 */

#include "ferrum/npc/npc_svo.h"

#include <math.h>
#include <string.h>

/* ── Static helpers ─────────────────────────────────────────────── */

/**
 * @brief Convert an AABB to the inclusive integer voxel coordinate range
 *        at max depth.
 */
static void world_to_voxel_range_(const npc_svo_grid_t *grid,
                                  phys_aabb_t aabb,
                                  uint32_t *out_min_x,
                                  uint32_t *out_min_y,
                                  uint32_t *out_min_z,
                                  uint32_t *out_max_x,
                                  uint32_t *out_max_y,
                                  uint32_t *out_max_z) {
    uint32_t cells = 1u << grid->max_depth;
    float    sx    = grid->world_bounds.max.x - grid->world_bounds.min.x;
    float    sy    = grid->world_bounds.max.y - grid->world_bounds.min.y;
    float    sz    = grid->world_bounds.max.z - grid->world_bounds.min.z;

    aabb.min.x = fmaxf(aabb.min.x, grid->world_bounds.min.x);
    aabb.min.y = fmaxf(aabb.min.y, grid->world_bounds.min.y);
    aabb.min.z = fmaxf(aabb.min.z, grid->world_bounds.min.z);
    aabb.max.x = fminf(aabb.max.x, grid->world_bounds.max.x);
    aabb.max.y = fminf(aabb.max.y, grid->world_bounds.max.y);
    aabb.max.z = fminf(aabb.max.z, grid->world_bounds.max.z);

    uint32_t min_x = (uint32_t)((aabb.min.x - grid->world_bounds.min.x) / sx * (float)cells);
    uint32_t min_y = (uint32_t)((aabb.min.y - grid->world_bounds.min.y) / sy * (float)cells);
    uint32_t min_z = (uint32_t)((aabb.min.z - grid->world_bounds.min.z) / sz * (float)cells);
    uint32_t max_x = (uint32_t)((aabb.max.x - grid->world_bounds.min.x) / sx * (float)cells);
    uint32_t max_y = (uint32_t)((aabb.max.y - grid->world_bounds.min.y) / sy * (float)cells);
    uint32_t max_z = (uint32_t)((aabb.max.z - grid->world_bounds.min.z) / sz * (float)cells);

    if (min_x >= cells) min_x = cells - 1;
    if (min_y >= cells) min_y = cells - 1;
    if (min_z >= cells) min_z = cells - 1;
    if (max_x >= cells) max_x = cells - 1;
    if (max_y >= cells) max_y = cells - 1;
    if (max_z >= cells) max_z = cells - 1;

    *out_min_x = min_x;
    *out_min_y = min_y;
    *out_min_z = min_z;
    *out_max_x = max_x;
    *out_max_y = max_y;
    *out_max_z = max_z;
}


/**
 * @brief Walk the SVO from root, creating nodes as needed, to reach the
 *        leaf voxel at (x,y,z) at max depth.
 *
 * @return Leaf node index, or NPC_SVO_INVALID_NODE on allocation failure.
 */
static uint32_t ensure_leaf_(npc_svo_grid_t *grid,
                             uint32_t vx, uint32_t vy, uint32_t vz) {
    uint32_t node_idx = 0; /* root */
    uint32_t cells    = 1u << grid->max_depth;

    for (uint32_t d = 0; d < grid->max_depth; d++) {
        cells >>= 1;
        uint32_t cx = (vx / cells) & 1;
        uint32_t cy = (vy / cells) & 1;
        uint32_t cz = (vz / cells) & 1;
        uint32_t child_idx = (cz << 2) | (cy << 1) | cx;

        uint32_t child = grid->nodes[node_idx].children[child_idx];

        if (child == NPC_SVO_INVALID_NODE) {
            child = npc_svo_alloc_node(grid);
            if (child == NPC_SVO_INVALID_NODE) {
                return NPC_SVO_INVALID_NODE;
            }
            /* NB: alloc_node may realloc the pool, so index grid->nodes fresh
             * here rather than caching a npc_svo_node_t* across the alloc. */
            grid->nodes[node_idx].children[child_idx] = child;
            grid->nodes[child].parent = node_idx;
            grid->nodes[node_idx].occupancy |= (1u << child_idx);
        }
        node_idx = child;
    }
    return node_idx;
}

/**
 * @brief Mark a single voxel (by coordinates) as SOLID.
 */
static void mark_voxel_solid_(npc_svo_grid_t *grid,
                              uint32_t vx, uint32_t vy, uint32_t vz) {
    uint32_t leaf = ensure_leaf_(grid, vx, vy, vz);
    if (leaf != NPC_SVO_INVALID_NODE) {
        grid->nodes[leaf].flags |= NPC_SVO_FLAG_SOLID;
    }
}

/* ── Public API ─────────────────────────────────────────────────── */

void npc_svo_rasterize_triangle(npc_svo_grid_t *grid,
                                const phys_triangle_t *tri) {
    if (!grid || !tri) return;

    /* Compute triangle AABB. */
    phys_aabb_t tri_aabb;
    tri_aabb.min = tri->v[0];
    tri_aabb.max = tri->v[0];
    for (int i = 1; i < 3; i++) {
        phys_vec3_t p = tri->v[i];
        if (p.x < tri_aabb.min.x) tri_aabb.min.x = p.x;
        if (p.y < tri_aabb.min.y) tri_aabb.min.y = p.y;
        if (p.z < tri_aabb.min.z) tri_aabb.min.z = p.z;
        if (p.x > tri_aabb.max.x) tri_aabb.max.x = p.x;
        if (p.y > tri_aabb.max.y) tri_aabb.max.y = p.y;
        if (p.z > tri_aabb.max.z) tri_aabb.max.z = p.z;
    }

    uint32_t min_x, min_y, min_z, max_x, max_y, max_z;
    world_to_voxel_range_(grid, tri_aabb,
                          &min_x, &min_y, &min_z,
                          &max_x, &max_y, &max_z);

    for (uint32_t z = min_z; z <= max_z; z++) {
        for (uint32_t y = min_y; y <= max_y; y++) {
            for (uint32_t x = min_x; x <= max_x; x++) {
                mark_voxel_solid_(grid, x, y, z);
            }
        }
    }
}

void npc_svo_rasterize_mesh(npc_svo_grid_t *grid,
                            const phys_triangle_t *triangles,
                            uint32_t tri_count) {
    if (!grid || !triangles || tri_count == 0) return;

    for (uint32_t i = 0; i < tri_count; i++) {
        npc_svo_rasterize_triangle(grid, &triangles[i]);
    }
}
