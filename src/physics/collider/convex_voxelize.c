/**
 * @file convex_voxelize.c
 * @brief Voxelization of triangle meshes for convex decomposition.
 *
 * Converts a triangle mesh into a 3D voxel grid.  Each voxel is
 * marked as FILLED if it lies on the mesh surface or inside the
 * mesh volume (determined by ray-casting parity along +X).
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

/* ── Voxel grid index helper ──────────────────────────────────── */

static inline uint32_t voxel_idx(uint32_t x, uint32_t y, uint32_t z,
                                  uint32_t res) {
    return z * res * res + y * res + x;
}

/* ── Ray-triangle intersection (Möller–Trumbore, +X ray) ───────── */

/**
 * Test ray from (ox, oy, oz) along +X against triangle.
 * Returns the intersection t parameter, or -1 if no hit.
 */
static float ray_triangle_t(float ox, float oy, float oz,
                             const phys_triangle_t *tri) {
    phys_vec3_t e1 = {
        tri->v[1].x - tri->v[0].x,
        tri->v[1].y - tri->v[0].y,
        tri->v[1].z - tri->v[0].z,
    };
    phys_vec3_t e2 = {
        tri->v[2].x - tri->v[0].x,
        tri->v[2].y - tri->v[0].y,
        tri->v[2].z - tri->v[0].z,
    };
    /* h = cross((1,0,0), e2) = (0, -e2.z, e2.y) */
    float hy = -e2.z;
    float hz = e2.y;

    float a = e1.y * hy + e1.z * hz;  /* e1 · h (hx=0) */
    if (fabsf(a) < 1e-8f) return -1.0f;

    float f = 1.0f / a;
    float sx = ox - tri->v[0].x;
    float sy = oy - tri->v[0].y;
    float sz = oz - tri->v[0].z;

    float u = f * (sy * hy + sz * hz);  /* s · h (hx=0) */
    if (u < 0.0f || u > 1.0f) return -1.0f;

    /* q = cross(s, e1) */
    float qx = sy * e1.z - sz * e1.y;
    float qy = sz * e1.x - sx * e1.z;
    float qz = sx * e1.y - sy * e1.x;

    float v = f * qx;  /* dir · q = (1,0,0)·q = qx */
    if (v < 0.0f || u + v > 1.0f) return -1.0f;

    float t = f * (e2.x * qx + e2.y * qy + e2.z * qz);
    return t > 1e-6f ? t : -1.0f;
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
            if (gx < ax) { ax = gx; } if (gx > bx) { bx = gx; }
            if (gy < ay) { ay = gy; } if (gy > by) { by = gy; }
            if (gz < az) { az = gz; } if (gz > bz) { bz = gz; }
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
                                   (uint32_t)iz, res)] = 1;
                }
            }
        }
    }
}

/* ── Interior voxelization via ray-casting parity ──────────────── */

/**
 * @brief Voxelize mesh interior using scanline ray-casting parity.
 *
 * For each (y, z) row, cast a ray along +X and collect all triangle
 * intersection t values.  Walk voxels left-to-right, counting
 * crossings before each voxel center.  Odd crossing count = inside.
 */
void voxelize_mesh(uint8_t *grid, uint32_t res,
                   phys_vec3_t min_corner, float cell_size,
                   const phys_triangle_t *triangles, uint32_t tri_count) {
    /* First mark surface voxels. */
    voxelize_surface(grid, res, min_corner, cell_size, triangles, tri_count);

    /* Allocate crossing buffer: worst case, every triangle hits. */
    float *crossings = malloc(tri_count * sizeof(float));
    if (!crossings) return;

    for (uint32_t iz = 0; iz < res; iz++) {
        for (uint32_t iy = 0; iy < res; iy++) {
            float ray_oy = min_corner.y + ((float)iy + 0.5f) * cell_size;
            float ray_oz = min_corner.z + ((float)iz + 0.5f) * cell_size;
            float ray_ox = min_corner.x - cell_size;

            /* Collect all crossing t values along this scanline. */
            uint32_t ncross = 0;
            for (uint32_t t = 0; t < tri_count; t++) {
                float hit_t = ray_triangle_t(ray_ox, ray_oy, ray_oz,
                                              &triangles[t]);
                if (hit_t > 0.0f) {
                    crossings[ncross++] = hit_t;
                }
            }

            /* Sort crossings (insertion sort — typically few crossings). */
            for (uint32_t i = 1; i < ncross; i++) {
                float key = crossings[i];
                int j = (int)i - 1;
                while (j >= 0 && crossings[j] > key) {
                    crossings[j + 1] = crossings[j];
                    j--;
                }
                crossings[j + 1] = key;
            }

            /* Walk voxels, tracking parity. */
            uint32_t cross_idx = 0;
            for (uint32_t ix = 0; ix < res; ix++) {
                if (grid[voxel_idx(ix, iy, iz, res)]) continue;

                /* Voxel center distance from ray origin along +X. */
                float voxel_t = (min_corner.x + ((float)ix + 0.5f) * cell_size)
                                - ray_ox;

                /* Count crossings before this voxel center. */
                while (cross_idx < ncross && crossings[cross_idx] < voxel_t) {
                    cross_idx++;
                }
                /* Odd parity = inside. */
                if (cross_idx & 1) {
                    grid[voxel_idx(ix, iy, iz, res)] = 1;
                }
            }
        }
    }

    free(crossings);
}
