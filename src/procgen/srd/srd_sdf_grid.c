/**
 * @file srd_sdf_grid.c
 * @brief Core lifecycle and voxel access for srd_sdf_grid_t.
 *
 * Non-static functions (4): srd_sdf_grid_init, srd_sdf_grid_destroy,
 *                           srd_sdf_grid_get, srd_sdf_grid_set
 */
#include "ferrum/procgen/srd/srd_sdf_grid.h"

#include <stdlib.h>
#include <string.h>

/* ── Flat index helper ─────────────────────────────────────────── */

/** @brief Compute flat index from (x, y, z). No bounds check. */
static inline int flat_index(const srd_sdf_grid_t *g, int x, int y, int z) {
    return z * g->ny * g->nx + y * g->nx + x;
}

/** @brief Check if (x, y, z) is within grid bounds. */
static inline int in_bounds(const srd_sdf_grid_t *g, int x, int y, int z) {
    return x >= 0 && x < g->nx &&
           y >= 0 && y < g->ny &&
           z >= 0 && z < g->nz;
}

/* ── Public API ────────────────────────────────────────────────── */

int srd_sdf_grid_init(srd_sdf_grid_t *grid, int nx, int ny, int nz,
                      float voxel_size, const float origin[3]) {
    if (!grid || nx <= 0 || ny <= 0 || nz <= 0 || voxel_size <= 0.0f)
        return -1;

    int total = nx * ny * nz;
    float *values = (float *)malloc((size_t)total * sizeof(float));
    if (!values) return -1;

    /* Fill with large positive (everything starts solid / outside) */
    for (int i = 0; i < total; i++)
        values[i] = SRD_SDF_INIT_VALUE;

    grid->values = values;
    grid->nx = nx;
    grid->ny = ny;
    grid->nz = nz;
    grid->voxel_size = voxel_size;
    if (origin) {
        grid->origin[0] = origin[0];
        grid->origin[1] = origin[1];
        grid->origin[2] = origin[2];
    } else {
        grid->origin[0] = 0.0f;
        grid->origin[1] = 0.0f;
        grid->origin[2] = 0.0f;
    }

    return 0;
}

void srd_sdf_grid_destroy(srd_sdf_grid_t *grid) {
    if (!grid) return;
    free(grid->values);
    memset(grid, 0, sizeof(*grid));
}

float srd_sdf_grid_get(const srd_sdf_grid_t *grid, int x, int y, int z) {
    if (!grid || !grid->values || !in_bounds(grid, x, y, z))
        return SRD_SDF_OUTSIDE;
    return grid->values[flat_index(grid, x, y, z)];
}

void srd_sdf_grid_set(srd_sdf_grid_t *grid, int x, int y, int z, float value) {
    if (!grid || !grid->values || !in_bounds(grid, x, y, z))
        return;
    grid->values[flat_index(grid, x, y, z)] = value;
}
