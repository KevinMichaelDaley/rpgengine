/**
 * @file srd_sdf_to_svo.c
 * @brief Convert SDF grid to sparse voxel octree.
 *
 * Non-static functions (1): srd_sdf_to_svo
 */
#include "ferrum/procgen/srd/srd_sdf_to_svo.h"

#include <math.h>
#include <string.h>

/* ── Helpers ───────────────────────────────────────────────────── */

/**
 * @brief Compute the SVO depth needed so that SVO voxel size <= grid voxel size.
 *
 * SVO covers a cube of side = max(world_extent_x, y, z).
 * At depth d, cell count = 2^d, cell_size = side / 2^d.
 * We need cell_size <= voxel_size, so d >= log2(side / voxel_size).
 */
static uint32_t compute_depth(float world_side, float voxel_size) {
    if (voxel_size <= 0.0f || world_side <= 0.0f) return 4;
    float cells_needed = world_side / voxel_size;
    uint32_t depth = 0;
    uint32_t cells = 1;
    while (cells < (uint32_t)ceilf(cells_needed) && depth < NPC_SVO_MAX_DEPTH) {
        depth++;
        cells <<= 1;
    }
    return depth;
}

/**
 * @brief Mark a single voxel as SOLID in the SVO.
 *
 * Walks the octree from root, allocating child nodes as needed,
 * and sets NPC_SVO_FLAG_SOLID on the leaf.
 *
 * @param svo   SVO grid.
 * @param vx,vy,vz  Voxel coordinates within the SVO (0-based).
 * @param cells Number of cells per axis (2^depth).
 */
static void svo_mark_solid(npc_svo_grid_t *svo,
                           uint32_t vx, uint32_t vy, uint32_t vz,
                           uint32_t cells) {
    uint32_t node_idx = 0;  /* Start at root */
    uint32_t half = cells >> 1;

    uint32_t lx = vx, ly = vy, lz = vz;

    for (uint32_t d = 0; d < svo->max_depth; d++) {
        /* Determine child octant (Morton order: z*4 + y*2 + x) */
        uint32_t cx = (lx >= half) ? 1 : 0;
        uint32_t cy = (ly >= half) ? 1 : 0;
        uint32_t cz = (lz >= half) ? 1 : 0;
        uint32_t child_slot = cz * 4 + cy * 2 + cx;

        uint32_t child_node = svo->nodes[node_idx].children[child_slot];
        if (child_node == NPC_SVO_INVALID_NODE) {
            /* Allocate a new child node */
            child_node = npc_svo_alloc_node(svo);
            if (child_node == NPC_SVO_INVALID_NODE) return; /* allocation failed */
            svo->nodes[node_idx].children[child_slot] = child_node;
            svo->nodes[node_idx].occupancy |= (1u << child_slot);
            svo->nodes[child_node].parent = node_idx;
        }

        node_idx = child_node;

        /* Shift into child octant's local space */
        if (cx) lx -= half;
        if (cy) ly -= half;
        if (cz) lz -= half;
        half >>= 1;
    }

    /* Mark leaf as solid */
    svo->nodes[node_idx].flags |= NPC_SVO_FLAG_SOLID;
    svo->nodes[node_idx].material = 1; /* stone */
}

/**
 * @brief Sample the SDF at an arbitrary world position via trilinear interpolation.
 *
 * Converts world position to grid coordinates, clamps to bounds, and
 * interpolates between the 8 surrounding grid values.
 */
static float sample_sdf(const srd_sdf_grid_t *grid, float wx, float wy, float wz) {
    float inv = 1.0f / grid->voxel_size;
    float fx = (wx - grid->origin[0]) * inv;
    float fy = (wy - grid->origin[1]) * inv;
    float fz = (wz - grid->origin[2]) * inv;

    int x0 = (int)floorf(fx);
    int y0 = (int)floorf(fy);
    int z0 = (int)floorf(fz);

    /* Clamp to grid bounds */
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (z0 < 0) z0 = 0;
    int x1 = x0 + 1;
    int y1 = y0 + 1;
    int z1 = z0 + 1;
    if (x1 >= grid->nx) { x1 = grid->nx - 1; x0 = x1; }
    if (y1 >= grid->ny) { y1 = grid->ny - 1; y0 = y1; }
    if (z1 >= grid->nz) { z1 = grid->nz - 1; z0 = z1; }

    float tx = fx - (float)x0;
    float ty = fy - (float)y0;
    float tz = fz - (float)z0;
    if (tx < 0.0f) tx = 0.0f;
    if (ty < 0.0f) ty = 0.0f;
    if (tz < 0.0f) tz = 0.0f;
    if (tx > 1.0f) tx = 1.0f;
    if (ty > 1.0f) ty = 1.0f;
    if (tz > 1.0f) tz = 1.0f;

    int nx = grid->nx, ny = grid->ny;
    float v000 = grid->values[z0 * ny * nx + y0 * nx + x0];
    float v100 = grid->values[z0 * ny * nx + y0 * nx + x1];
    float v010 = grid->values[z0 * ny * nx + y1 * nx + x0];
    float v110 = grid->values[z0 * ny * nx + y1 * nx + x1];
    float v001 = grid->values[z1 * ny * nx + y0 * nx + x0];
    float v101 = grid->values[z1 * ny * nx + y0 * nx + x1];
    float v011 = grid->values[z1 * ny * nx + y1 * nx + x0];
    float v111 = grid->values[z1 * ny * nx + y1 * nx + x1];

    /* Trilinear interpolation */
    float c00 = v000 * (1-tx) + v100 * tx;
    float c10 = v010 * (1-tx) + v110 * tx;
    float c01 = v001 * (1-tx) + v101 * tx;
    float c11 = v011 * (1-tx) + v111 * tx;
    float c0  = c00  * (1-ty) + c10  * ty;
    float c1  = c01  * (1-ty) + c11  * ty;
    return c0 * (1-tz) + c1 * tz;
}

/* ── Public API ────────────────────────────────────────────────── */

int srd_sdf_to_svo(const srd_sdf_grid_t *grid, npc_svo_grid_t *svo_out) {
    if (!grid || !grid->values || !svo_out) return -1;

    /* Compute world extent of the grid */
    float wx = (float)grid->nx * grid->voxel_size;
    float wy = (float)grid->ny * grid->voxel_size;
    float wz = (float)grid->nz * grid->voxel_size;

    /* SVO requires a cubic domain; use the max extent */
    float world_side = wx;
    if (wy > world_side) world_side = wy;
    if (wz > world_side) world_side = wz;

    /* Compute bounds: SVO is a cube centered on the grid's center */
    float cx = grid->origin[0] + wx * 0.5f;
    float cy = grid->origin[1] + wy * 0.5f;
    float cz = grid->origin[2] + wz * 0.5f;
    float half_side = world_side * 0.5f;

    phys_aabb_t bounds;
    bounds.min.x = cx - half_side;
    bounds.min.y = cy - half_side;
    bounds.min.z = cz - half_side;
    bounds.max.x = cx + half_side;
    bounds.max.y = cy + half_side;
    bounds.max.z = cz + half_side;

    /* Compute depth */
    uint32_t depth = compute_depth(world_side, grid->voxel_size);
    if (depth < 1) depth = 1;

    /* Initialize SVO */
    if (!npc_svo_grid_init(svo_out, bounds, depth)) return -1;

    uint32_t cells = 1u << depth;
    float svo_cell_size = world_side / (float)cells;

    /* Walk every SVO cell (the finer grid). For each cell, sample the SDF
     * at all 8 corners via trilinear interpolation. Mark the cell solid
     * if the isosurface passes through it (sign change among corners). */
    float svo_ox = bounds.min.x;
    float svo_oy = bounds.min.y;
    float svo_oz = bounds.min.z;

    for (uint32_t sz = 0; sz < cells; sz++) {
        for (uint32_t sy = 0; sy < cells; sy++) {
            for (uint32_t sx = 0; sx < cells; sx++) {
                /* World-space corners of this SVO cell */
                float x0 = svo_ox + (float)sx * svo_cell_size;
                float y0 = svo_oy + (float)sy * svo_cell_size;
                float z0 = svo_oz + (float)sz * svo_cell_size;
                float x1 = x0 + svo_cell_size;
                float y1 = y0 + svo_cell_size;
                float z1 = z0 + svo_cell_size;

                /* Sample SDF at 8 corners */
                float c[8];
                c[0] = sample_sdf(grid, x0, y0, z0);
                c[1] = sample_sdf(grid, x1, y0, z0);
                c[2] = sample_sdf(grid, x0, y1, z0);
                c[3] = sample_sdf(grid, x1, y1, z0);
                c[4] = sample_sdf(grid, x0, y0, z1);
                c[5] = sample_sdf(grid, x1, y0, z1);
                c[6] = sample_sdf(grid, x0, y1, z1);
                c[7] = sample_sdf(grid, x1, y1, z1);

                /* Check for sign change (root of SDF within cell).
                 * SDF=0 is on the surface, so ≤0 counts as surface side. */
                int has_surface = 0, has_outside = 0;
                for (int i = 0; i < 8; i++) {
                    if (c[i] <= 0.0f) has_surface = 1;
                    else              has_outside = 1;
                }

                if (!has_surface || !has_outside) continue;

                svo_mark_solid(svo_out, sx, sy, sz, cells);
            }
        }
    }

    return 0;
}
