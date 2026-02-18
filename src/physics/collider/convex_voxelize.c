/**
 * @file convex_voxelize.c
 * @brief Voxelization of triangle meshes for convex decomposition.
 *
 * Converts a triangle mesh into a 3D voxel grid.  Each voxel is
 * marked as FILLED if it lies on the mesh surface or inside the
 * mesh volume.
 *
 * For watertight meshes, interior is determined by flood-filling
 * the exterior from grid corner (0,0,0) and inverting: any voxel
 * not reached by the flood and not on the surface is interior.
 *
 * Non-static functions (2):
 *   1. voxelize_mesh
 *   2. voxelize_surface
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/physics/mesh_collider.h"
#include "ferrum/physics/phys_types.h"

/* ── Voxel grid constants ─────────────────────────────────────── */

/** Surface / filled voxel marker. */
#define VOXEL_FILLED 1u

/** Exterior voxel marker (used during flood fill, then inverted). */
#define VOXEL_EXTERIOR 2u

/* ── Voxel grid index helper ──────────────────────────────────── */

static inline uint32_t voxel_idx(uint32_t x, uint32_t y, uint32_t z,
                                  uint32_t res) {
    return z * res * res + y * res + x;
}

/* ── Flood-fill stack entry ───────────────────────────────────── */

typedef struct voxel_coord {
    uint16_t x, y, z;
} voxel_coord_t;

/**
 * @brief Flood-fill exterior voxels from grid corner (0,0,0).
 *
 * Marks all empty voxels reachable from the grid boundary as
 * VOXEL_EXTERIOR using an explicit stack (no recursion).
 * Surface voxels (value == VOXEL_FILLED) act as barriers.
 */
static void flood_fill_exterior(uint8_t *grid, uint32_t res) {
    uint32_t total = res * res * res;

    /* Pre-allocate stack — worst case every voxel is exterior. */
    voxel_coord_t *stack = malloc(total * sizeof(voxel_coord_t));
    if (!stack) return;
    uint32_t top = 0;

    /* Seed from (0,0,0).  The grid has a margin around the mesh
     * so corner voxels are guaranteed to be outside the mesh. */
    if (grid[0] == 0) {
        grid[0] = VOXEL_EXTERIOR;
        stack[top++] = (voxel_coord_t){0, 0, 0};
    }

    /* 6-connected flood fill. */
    while (top > 0) {
        voxel_coord_t c = stack[--top];
        int dx[6] = {-1, 1, 0, 0, 0, 0};
        int dy[6] = {0, 0, -1, 1, 0, 0};
        int dz[6] = {0, 0, 0, 0, -1, 1};

        for (int d = 0; d < 6; d++) {
            int nx = (int)c.x + dx[d];
            int ny = (int)c.y + dy[d];
            int nz = (int)c.z + dz[d];

            if (nx < 0 || nx >= (int)res) continue;
            if (ny < 0 || ny >= (int)res) continue;
            if (nz < 0 || nz >= (int)res) continue;

            uint32_t idx = voxel_idx((uint32_t)nx, (uint32_t)ny,
                                      (uint32_t)nz, res);
            if (grid[idx] == 0) {
                grid[idx] = VOXEL_EXTERIOR;
                stack[top++] = (voxel_coord_t){
                    (uint16_t)nx, (uint16_t)ny, (uint16_t)nz};
            }
        }
    }

    free(stack);
}

/* ── Surface voxelization ──────────────────────────────────────── */

/**
 * @brief Mark voxels that the mesh surface passes through.
 *
 * For each triangle, rasterize it into the voxel grid by marking
 * all voxels whose cell overlaps the triangle's AABB.
 */
void voxelize_surface(uint8_t *grid, uint32_t res,
                      phys_vec3_t min_corner, float cell_size,
                      const phys_triangle_t *triangles, uint32_t tri_count) {
    float inv_cell = 1.0f / cell_size;

    for (uint32_t t = 0; t < tri_count; t++) {
        const phys_triangle_t *tri = &triangles[t];
        /* Triangle AABB in grid coordinates. */
        float ax = (tri->v[0].x - min_corner.x) * inv_cell;
        float ay = (tri->v[0].y - min_corner.y) * inv_cell;
        float az = (tri->v[0].z - min_corner.z) * inv_cell;
        float bx = ax, by = ay, bz = az;
        for (int vi = 1; vi < 3; vi++) {
            float gx = (tri->v[vi].x - min_corner.x) * inv_cell;
            float gy = (tri->v[vi].y - min_corner.y) * inv_cell;
            float gz = (tri->v[vi].z - min_corner.z) * inv_cell;
            if (gx < ax) { ax = gx; }
            if (gx > bx) { bx = gx; }
            if (gy < ay) { ay = gy; }
            if (gy > by) { by = gy; }
            if (gz < az) { az = gz; }
            if (gz > bz) { bz = gz; }
        }
        int x0 = (int)ax; if (x0 < 0) x0 = 0;
        int y0 = (int)ay; if (y0 < 0) y0 = 0;
        int z0 = (int)az; if (z0 < 0) z0 = 0;
        int x1 = (int)bx; if (x1 >= (int)res) x1 = (int)res - 1;
        int y1 = (int)by; if (y1 >= (int)res) y1 = (int)res - 1;
        int z1 = (int)bz; if (z1 >= (int)res) z1 = (int)res - 1;

        for (int iz = z0; iz <= z1; iz++) {
            for (int iy = y0; iy <= y1; iy++) {
                for (int ix = x0; ix <= x1; ix++) {
                    grid[voxel_idx((uint32_t)ix, (uint32_t)iy,
                                   (uint32_t)iz, res)] = VOXEL_FILLED;
                }
            }
        }
    }
}

/* ── Interior voxelization via exterior flood fill ─────────────── */

/**
 * @brief Voxelize mesh interior using flood-fill inversion.
 *
 * Algorithm (assumes watertight mesh):
 *   1. Mark surface voxels via triangle AABB rasterization.
 *   2. Flood-fill exterior from corner (0,0,0) — surface voxels
 *      act as barriers, so flood cannot leak inside.
 *   3. Invert: any voxel not marked as exterior or surface is
 *      interior → mark as filled.
 *
 * The grid must have a margin around the mesh AABB so that
 * corner (0,0,0) is guaranteed to be outside the mesh.
 */
void voxelize_mesh(uint8_t *grid, uint32_t res,
                   phys_vec3_t min_corner, float cell_size,
                   const phys_triangle_t *triangles, uint32_t tri_count) {
    /* Step 1: mark surface voxels. */
    voxelize_surface(grid, res, min_corner, cell_size, triangles, tri_count);

    /* Step 2: flood-fill exterior from corner. */
    flood_fill_exterior(grid, res);

    /* Step 3: invert — unmarked voxels are interior. */
    uint32_t total = res * res * res;
    for (uint32_t i = 0; i < total; i++) {
        if (grid[i] == 0) {
            /* Not surface and not exterior → interior. */
            grid[i] = VOXEL_FILLED;
        } else if (grid[i] == VOXEL_EXTERIOR) {
            /* Exterior → empty. */
            grid[i] = 0;
        }
        /* VOXEL_FILLED stays as-is. */
    }
}
