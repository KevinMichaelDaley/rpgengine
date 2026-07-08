/**
 * @file srd_sdf_grid_stamp.c
 * @brief Stamp primitive SDFs into the grid (box, sphere) with CSG ops.
 *
 * Non-static functions (4): srd_sdf_grid_stamp_box,
 *                           srd_sdf_grid_subtract_box,
 *                           srd_sdf_grid_stamp_sphere,
 *                           srd_sdf_grid_subtract_sphere
 */
#include "ferrum/procgen/srd/srd_sdf_grid.h"

#include <math.h>

/* ── SDF primitives ────────────────────────────────────────────── */

/**
 * @brief Evaluate axis-aligned box SDF at a point.
 *
 * sdf(p) = max(|px - cx| - hx, |py - cy| - hy, |pz - cz| - hz)
 * Negative inside, positive outside, zero on surface.
 */
static inline float box_sdf(float px, float py, float pz,
                            float cx, float cy, float cz,
                            float hx, float hy, float hz) {
    float dx = fabsf(px - cx) - hx;
    float dy = fabsf(py - cy) - hy;
    float dz = fabsf(pz - cz) - hz;
    /* For an exact SDF we'd compute the exterior distance differently,
     * but for grid stamping the L-infinity approximation is sufficient
     * and much cheaper. The interior distance is exact. */
    float m = dx;
    if (dy > m) m = dy;
    if (dz > m) m = dz;
    return m;
}

/**
 * @brief Evaluate sphere SDF at a point.
 *
 * sdf(p) = length(p - center) - radius
 */
static inline float sphere_sdf(float px, float py, float pz,
                               float cx, float cy, float cz,
                               float radius) {
    float dx = px - cx;
    float dy = py - cy;
    float dz = pz - cz;
    return sqrtf(dx * dx + dy * dy + dz * dz) - radius;
}

/* ── Stamp helpers ─────────────────────────────────────────────── */

/**
 * @brief Compute the voxel AABB that encloses a world-space AABB,
 *        clamped to grid bounds.
 */
static void compute_voxel_range(const srd_sdf_grid_t *grid,
                                float wmin_x, float wmin_y, float wmin_z,
                                float wmax_x, float wmax_y, float wmax_z,
                                int *x0, int *y0, int *z0,
                                int *x1, int *y1, int *z1) {
    float inv = 1.0f / grid->voxel_size;

    *x0 = (int)floorf((wmin_x - grid->origin[0]) * inv);
    *y0 = (int)floorf((wmin_y - grid->origin[1]) * inv);
    *z0 = (int)floorf((wmin_z - grid->origin[2]) * inv);

    *x1 = (int)ceilf((wmax_x - grid->origin[0]) * inv);
    *y1 = (int)ceilf((wmax_y - grid->origin[1]) * inv);
    *z1 = (int)ceilf((wmax_z - grid->origin[2]) * inv);

    /* Clamp to grid bounds */
    if (*x0 < 0) *x0 = 0;
    if (*y0 < 0) *y0 = 0;
    if (*z0 < 0) *z0 = 0;
    if (*x1 >= grid->nx) *x1 = grid->nx - 1;
    if (*y1 >= grid->ny) *y1 = grid->ny - 1;
    if (*z1 >= grid->nz) *z1 = grid->nz - 1;
}

/* ── Public API ────────────────────────────────────────────────── */

void srd_sdf_grid_stamp_box(srd_sdf_grid_t *grid,
                            float cx, float cy, float cz,
                            float hx, float hy, float hz) {
    if (!grid || !grid->values) return;

    /* Compute affected voxel range (expand by 1 voxel for SDF margin) */
    float margin = grid->voxel_size;
    int x0, y0, z0, x1, y1, z1;
    compute_voxel_range(grid,
                        cx - hx - margin, cy - hy - margin, cz - hz - margin,
                        cx + hx + margin, cy + hy + margin, cz + hz + margin,
                        &x0, &y0, &z0, &x1, &y1, &z1);

    for (int vz = z0; vz <= z1; vz++) {
        for (int vy = y0; vy <= y1; vy++) {
            for (int vx = x0; vx <= x1; vx++) {
                float wx = grid->origin[0] + (float)vx * grid->voxel_size;
                float wy = grid->origin[1] + (float)vy * grid->voxel_size;
                float wz = grid->origin[2] + (float)vz * grid->voxel_size;

                float sd = box_sdf(wx, wy, wz, cx, cy, cz, hx, hy, hz);
                int idx = vz * grid->ny * grid->nx + vy * grid->nx + vx;

                /* CSG union: min(current, new) */
                if (sd < grid->values[idx])
                    grid->values[idx] = sd;
            }
        }
    }
}

void srd_sdf_grid_subtract_box(srd_sdf_grid_t *grid,
                               float cx, float cy, float cz,
                               float hx, float hy, float hz) {
    if (!grid || !grid->values) return;

    float margin = grid->voxel_size;
    int x0, y0, z0, x1, y1, z1;
    compute_voxel_range(grid,
                        cx - hx - margin, cy - hy - margin, cz - hz - margin,
                        cx + hx + margin, cy + hy + margin, cz + hz + margin,
                        &x0, &y0, &z0, &x1, &y1, &z1);

    for (int vz = z0; vz <= z1; vz++) {
        for (int vy = y0; vy <= y1; vy++) {
            for (int vx = x0; vx <= x1; vx++) {
                float wx = grid->origin[0] + (float)vx * grid->voxel_size;
                float wy = grid->origin[1] + (float)vy * grid->voxel_size;
                float wz = grid->origin[2] + (float)vz * grid->voxel_size;

                float sd = box_sdf(wx, wy, wz, cx, cy, cz, hx, hy, hz);
                int idx = vz * grid->ny * grid->nx + vy * grid->nx + vx;

                /* CSG subtraction: max(current, -new) */
                float neg_sd = -sd;
                if (neg_sd > grid->values[idx])
                    grid->values[idx] = neg_sd;
            }
        }
    }
}

void srd_sdf_grid_stamp_sphere(srd_sdf_grid_t *grid,
                               float cx, float cy, float cz,
                               float radius) {
    if (!grid || !grid->values) return;

    float margin = grid->voxel_size;
    int x0, y0, z0, x1, y1, z1;
    compute_voxel_range(grid,
                        cx - radius - margin, cy - radius - margin, cz - radius - margin,
                        cx + radius + margin, cy + radius + margin, cz + radius + margin,
                        &x0, &y0, &z0, &x1, &y1, &z1);

    for (int vz = z0; vz <= z1; vz++) {
        for (int vy = y0; vy <= y1; vy++) {
            for (int vx = x0; vx <= x1; vx++) {
                float wx = grid->origin[0] + (float)vx * grid->voxel_size;
                float wy = grid->origin[1] + (float)vy * grid->voxel_size;
                float wz = grid->origin[2] + (float)vz * grid->voxel_size;

                float sd = sphere_sdf(wx, wy, wz, cx, cy, cz, radius);
                int idx = vz * grid->ny * grid->nx + vy * grid->nx + vx;

                /* CSG union: min(current, new) */
                if (sd < grid->values[idx])
                    grid->values[idx] = sd;
            }
        }
    }
}

void srd_sdf_grid_subtract_sphere(srd_sdf_grid_t *grid,
                                  float cx, float cy, float cz,
                                  float radius) {
    if (!grid || !grid->values) return;

    float margin = grid->voxel_size;
    int x0, y0, z0, x1, y1, z1;
    compute_voxel_range(grid,
                        cx - radius - margin, cy - radius - margin, cz - radius - margin,
                        cx + radius + margin, cy + radius + margin, cz + radius + margin,
                        &x0, &y0, &z0, &x1, &y1, &z1);

    for (int vz = z0; vz <= z1; vz++) {
        for (int vy = y0; vy <= y1; vy++) {
            for (int vx = x0; vx <= x1; vx++) {
                float wx = grid->origin[0] + (float)vx * grid->voxel_size;
                float wy = grid->origin[1] + (float)vy * grid->voxel_size;
                float wz = grid->origin[2] + (float)vz * grid->voxel_size;

                float sd = sphere_sdf(wx, wy, wz, cx, cy, cz, radius);
                int idx = vz * grid->ny * grid->nx + vy * grid->nx + vx;

                /* CSG subtraction: max(current, -new) */
                float neg_sd = -sd;
                if (neg_sd > grid->values[idx])
                    grid->values[idx] = neg_sd;
            }
        }
    }
}
