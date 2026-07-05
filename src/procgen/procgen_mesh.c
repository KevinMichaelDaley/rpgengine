/**
 * @file procgen_mesh.c
 * @brief Greedy mesh generation from an SVO grid.
 *
 * Scans all six axis-aligned directions and merges adjacent coplanar
 * faces into maximal axis-aligned rectangles.  This produces a compact
 * triangle mesh suitable for GPU rendering.
 */

#include "ferrum/procgen/procgen_svo_builder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Internal helpers ──────────────────────────────────────────── */

static void ensure_capacity(procgen_mesh_t *mesh, uint32_t needed) {
    if (needed > mesh->vertex_cap) {
        uint32_t new_cap = mesh->vertex_cap ? mesh->vertex_cap * 2 : 65536;
        if (new_cap < needed) new_cap = needed;
        float *new_data = realloc(mesh->vertices, new_cap * sizeof(float));
        if (!new_data) return;
        mesh->vertices  = new_data;
        mesh->vertex_cap = new_cap;
    }
}

/**
 * @brief Emit two triangles forming an axis-aligned quad.
 *
 * @param axis  0 = X-normal (quad in YZ plane),
 *              1 = Y-normal (quad in XZ plane),
 *              2 = Z-normal (quad in XY plane).
 */
static void emit_quad(procgen_mesh_t *mesh,
                      float x_min, float y_min, float z_min,
                      float x_max, float y_max, float z_max,
                      int axis, int sign) {
    ensure_capacity(mesh, mesh->vertex_count + 18);
    float *v = mesh->vertices + mesh->vertex_count;

    switch (axis) {
    case 0: {  /* YZ plane at X */
        float xx = (sign > 0) ? x_max : x_min;
        v[0] = xx; v[1] = y_min; v[2] = z_min;
        v[3] = xx; v[4] = y_max; v[5] = z_min;
        v[6] = xx; v[7] = y_max; v[8] = z_max;
        v[9] = xx; v[10] = y_min; v[11] = z_min;
        v[12]= xx; v[13] = y_max; v[14] = z_max;
        v[15]= xx; v[16] = y_min; v[17] = z_max;
        break;
    }
    case 1: {  /* XZ plane at Y */
        float yy = (sign > 0) ? y_max : y_min;
        v[0] = x_min; v[1] = yy; v[2] = z_min;
        v[3] = x_max; v[4] = yy; v[5] = z_min;
        v[6] = x_max; v[7] = yy; v[8] = z_max;
        v[9] = x_min; v[10] = yy; v[11] = z_min;
        v[12]= x_max; v[13] = yy; v[14] = z_max;
        v[15]= x_min; v[16] = yy; v[17] = z_max;
        break;
    }
    default: {  /* XY plane at Z */
        float zz = (sign > 0) ? z_max : z_min;
        v[0] = x_min; v[1] = y_min; v[2] = zz;
        v[3] = x_max; v[4] = y_min; v[5] = zz;
        v[6] = x_max; v[7] = y_max; v[8] = zz;
        v[9] = x_min; v[10] = y_min; v[11] = zz;
        v[12]= x_max; v[13] = y_max; v[14] = zz;
        v[15]= x_min; v[16] = y_max; v[17] = zz;
        break;
    }
    }
    mesh->vertex_count += 18;
}

/* ── SVO query (exposed here for mesh generation) ──────────────── */

static int is_solid_at(const npc_svo_grid_t *grid, int x, int y, int z) {
    if (!grid) return 0;
    uint32_t cells = 1u << grid->max_depth;
    if (x < 0 || y < 0 || z < 0
        || (uint32_t)x >= cells
        || (uint32_t)y >= cells
        || (uint32_t)z >= cells) {
        return 0;
    }
    uint32_t node = 0;
    uint32_t c    = cells;
    for (uint32_t d = 0; d < grid->max_depth; d++) {
        c >>= 1;
        uint32_t cx = ((uint32_t)z / c) & 1;
        uint32_t cy = ((uint32_t)y / c) & 1;
        uint32_t cz2 = ((uint32_t)x / c) & 1;
        uint32_t ci = (cx << 2) | (cy << 1) | cz2;
        uint32_t ch = grid->nodes[node].children[ci];
        if (ch == NPC_SVO_INVALID_NODE) return 0;
        node = ch;
        if (grid->nodes[node].flags & NPC_SVO_FLAG_SOLID) return 1;
    }
    return 0;
}

/* ── Greedy scan helpers ───────────────────────────────────────── */

/**
 * @brief Allocate a 2D boolean array for one slice of the grid.
 */
static int *alloc_slice(uint32_t cells) {
    size_t count = (size_t)cells * (size_t)cells;
    int *data = calloc(count, sizeof(int));
    return data;
}

static void free_slice(int *data) { free(data); }

/**
 * @brief Greedy-merge one axis-aligned direction.
 *
 * For each slice perpendicular to @p primary_axis, build a mask of
 * solid cells whose neighbor in @p direction (+dir) is air, then
 * merge adjacent cells into maximal rectangles and emit quads.
 */
static uint32_t greedy_merge_axis(const npc_svo_grid_t *grid,
                                   procgen_mesh_t       *mesh,
                                   int                   axis,
                                   int                   sign) {
    uint32_t cells     = 1u << grid->max_depth;
    float    voxel     = grid->voxel_size;
    float    origin_x  = grid->world_bounds.min.x;
    float    origin_y  = grid->world_bounds.min.y;
    float    origin_z  = grid->world_bounds.min.z;
    uint32_t triangles = 0;

    /* Iterate over slices perpendicular to axis. */
    /* The loop order: for axis 0 (X), slice varies X from 0..cells-1;
       for axis 1 (Y), slice varies Y; for axis 2 (Z), slice varies Z. */
    for (uint32_t slice = 0; slice < cells; slice++) {
        int *mask = alloc_slice(cells);

        /* Build mask: true where this voxel is solid and neighbor is air. */
        for (uint32_t b = 0; b < cells; b++) {
            for (uint32_t a = 0; a < cells; a++) {
                int sx = (int)slice, sy = (int)b, sz = (int)a;
                int nx = sx, ny = sy, nz = sz;
                if (axis == 0)      { sx = (int)slice; nx = sx + sign; }
                else if (axis == 1) { sy = (int)slice; ny = sy + sign; }
                else                { sz = (int)slice; nz = sz + sign; }

                int solid = is_solid_at(grid, sx, sy, sz);
                int neighbor_air = (sign > 0)
                    ? (slice + 1 >= cells || !is_solid_at(grid, nx, ny, nz))
                    : (slice == 0 || !is_solid_at(grid, nx, ny, nz));
                mask[b * cells + a] = solid && neighbor_air;
            }
        }

        /* Scan mask and emit merged quads. */
        for (uint32_t b = 0; b < cells; b++) {
            for (uint32_t a = 0; a < cells; a++) {
                if (!mask[b * cells + a]) continue;

                /* Extend in the a-direction. */
                uint32_t a_end = a;
                while (a_end + 1 < cells && mask[b * cells + (a_end + 1)]) {
                    a_end++;
                }

                /* Extend in the b-direction. */
                uint32_t b_end = b;
                int can_extend = 1;
                while (b_end + 1 < cells && can_extend) {
                    for (uint32_t check_a = a; check_a <= a_end; check_a++) {
                        if (!mask[(b_end + 1) * cells + check_a]) {
                            can_extend = 0;
                            break;
                        }
                    }
                    if (can_extend) b_end++;
                }

                /* Compute world-space quad bounds. */
                float ax_min, ax_max, bx_min, bx_max, fixed;
                if (axis == 0) {
                    ax_min = origin_x + (float)a      * voxel;
                    ax_max = origin_x + (float)(a_end + 1) * voxel;
                    bx_min = origin_y + (float)b      * voxel;
                    bx_max = origin_y + (float)(b_end + 1) * voxel;
                    fixed  = origin_x + (float)(slice + (sign > 0 ? 1 : 0)) * voxel;
                    if (sign > 0) {  /* +X */
                        emit_quad(mesh, fixed, bx_min, ax_min, fixed, bx_max, ax_max, axis, sign);
                    } else {
                        emit_quad(mesh, fixed, bx_max, ax_min, fixed, bx_min, ax_max, axis, sign);
                    }
                } else if (axis == 1) {
                    ax_min = origin_x + (float)a      * voxel;
                    ax_max = origin_x + (float)(a_end + 1) * voxel;
                    bx_min = origin_z + (float)b      * voxel;
                    bx_max = origin_z + (float)(b_end + 1) * voxel;
                    fixed  = origin_y + (float)(slice + (sign > 0 ? 1 : 0)) * voxel;
                    emit_quad(mesh, ax_min, fixed, bx_min, ax_max, fixed, bx_max, axis, sign);
                } else {
                    ax_min = origin_x + (float)a      * voxel;
                    ax_max = origin_x + (float)(a_end + 1) * voxel;
                    bx_min = origin_y + (float)b      * voxel;
                    bx_max = origin_y + (float)(b_end + 1) * voxel;
                    fixed  = origin_z + (float)(slice + (sign > 0 ? 1 : 0)) * voxel;
                    emit_quad(mesh, ax_min, bx_min, fixed, ax_max, bx_max, fixed, axis, sign);
                }
                triangles += 2;

                /* Mark merged region as consumed. */
                for (uint32_t cb = b; cb <= b_end; cb++) {
                    for (uint32_t ca = a; ca <= a_end; ca++) {
                        mask[cb * cells + ca] = 0;
                    }
                }
            }
        }
        free_slice(mask);
    }

    return triangles;
}

/* ── Public API ────────────────────────────────────────────────── */

void procgen_mesh_init(procgen_mesh_t *mesh) {
    memset(mesh, 0, sizeof(*mesh));
}

void procgen_mesh_destroy(procgen_mesh_t *mesh) {
    free(mesh->vertices);
    memset(mesh, 0, sizeof(*mesh));
}

uint32_t procgen_mesh_from_svo(const npc_svo_grid_t *grid,
                                procgen_mesh_t       *mesh) {
    uint32_t total_tris = 0;

    total_tris += greedy_merge_axis(grid, mesh, 1,  1);  /* +Y (top faces)   */
    total_tris += greedy_merge_axis(grid, mesh, 1, -1);  /* -Y (bottom faces) */
    total_tris += greedy_merge_axis(grid, mesh, 0,  1);  /* +X (right faces)  */
    total_tris += greedy_merge_axis(grid, mesh, 0, -1);  /* -X (left faces)   */
    total_tris += greedy_merge_axis(grid, mesh, 2,  1);  /* +Z (front faces)  */
    total_tris += greedy_merge_axis(grid, mesh, 2, -1);  /* -Z (back faces)   */

    return total_tris;
}
