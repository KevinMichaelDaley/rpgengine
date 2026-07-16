/**
 * @file gi_probe_grid.c
 * @brief Uniform accel grid for nearest-probe lookup (see gi_probe_grid.h).
 */
#include "ferrum/renderer/gi/gi_probe_grid.h"

#include <string.h>

/* Clamp an axis coordinate to a cell index in [0, dim). */
static int32_t clamp_axis(float v, float origin, float cell, int32_t dim)
{
    int32_t i = (int32_t)((v - origin) / cell);
    if (i < 0) i = 0;
    if (i >= dim) i = dim - 1;
    return i;
}

uint32_t gi_probe_grid_cell(const gi_probe_grid_t *grid, float x, float y, float z)
{
    int32_t ix = clamp_axis(x, grid->origin[0], grid->cell_size, grid->dims[0]);
    int32_t iy = clamp_axis(y, grid->origin[1], grid->cell_size, grid->dims[1]);
    int32_t iz = clamp_axis(z, grid->origin[2], grid->cell_size, grid->dims[2]);
    return (uint32_t)((iz * grid->dims[1] + iy) * grid->dims[0] + ix);
}

bool gi_probe_grid_build(gi_probe_grid_t *grid, const gi_probe_set_t *set,
                         const float aabb_min[3], const float aabb_max[3],
                         float cell_size, uint32_t *cell_start_backing,
                         uint32_t cell_start_cap, uint32_t *probe_idx_backing,
                         uint32_t probe_idx_cap)
{
    if (grid == NULL || set == NULL || aabb_min == NULL || aabb_max == NULL ||
        cell_start_backing == NULL || probe_idx_backing == NULL ||
        cell_size <= 0.0f)
        return false;

    for (int a = 0; a < 3; ++a) {
        grid->origin[a] = aabb_min[a];
        float span = aabb_max[a] - aabb_min[a];
        int32_t d = (int32_t)(span / cell_size);      /* floor. */
        if ((float)d * cell_size < span) d++;         /* ceil to cover the edge. */
        grid->dims[a] = d < 1 ? 1 : d;
    }
    grid->cell_size = cell_size;
    grid->ncells = (uint32_t)grid->dims[0] * (uint32_t)grid->dims[1] *
                   (uint32_t)grid->dims[2];
    if (cell_start_cap < grid->ncells + 1u || probe_idx_cap < set->count)
        return false;
    grid->cell_start = cell_start_backing;
    grid->probe_idx = probe_idx_backing;

    /* Counting sort of probes into cells (CSR). */
    uint32_t *start = grid->cell_start;
    memset(start, 0, (grid->ncells + 1u) * sizeof(uint32_t));
    for (uint32_t i = 0; i < set->count; ++i) {
        uint32_t c = gi_probe_grid_cell(grid, set->pos[i*3], set->pos[i*3+1],
                                        set->pos[i*3+2]);
        start[c + 1u]++;                     /* counts, shifted by one. */
    }
    for (uint32_t c = 1; c <= grid->ncells; ++c)
        start[c] += start[c - 1u];           /* prefix sum -> cell starts. */
    for (uint32_t i = 0; i < set->count; ++i) {
        uint32_t c = gi_probe_grid_cell(grid, set->pos[i*3], set->pos[i*3+1],
                                        set->pos[i*3+2]);
        grid->probe_idx[start[c]++] = i;     /* scatter (advances start[c]). */
    }
    /* Restore CSR starts (the scatter shifted each up to the next cell's start). */
    for (uint32_t c = grid->ncells; c >= 1u; --c)
        start[c] = start[c - 1u];
    start[0] = 0u;
    return true;
}

uint32_t gi_probe_grid_gather(const gi_probe_grid_t *grid, float x, float y,
                              float z, uint32_t *out, uint32_t cap)
{
    if (grid == NULL || out == NULL || cap == 0u)
        return 0u;
    int32_t ix = clamp_axis(x, grid->origin[0], grid->cell_size, grid->dims[0]);
    int32_t iy = clamp_axis(y, grid->origin[1], grid->cell_size, grid->dims[1]);
    int32_t iz = clamp_axis(z, grid->origin[2], grid->cell_size, grid->dims[2]);

    uint32_t n = 0;
    for (int32_t dz = -1; dz <= 1; ++dz)
    for (int32_t dy = -1; dy <= 1; ++dy)
    for (int32_t dx = -1; dx <= 1; ++dx) {
        int32_t cx = ix + dx, cy = iy + dy, cz = iz + dz;
        if (cx < 0 || cx >= grid->dims[0] || cy < 0 || cy >= grid->dims[1] ||
            cz < 0 || cz >= grid->dims[2])
            continue;
        uint32_t c = (uint32_t)((cz * grid->dims[1] + cy) * grid->dims[0] + cx);
        for (uint32_t p = grid->cell_start[c]; p < grid->cell_start[c + 1u]; ++p) {
            if (n >= cap)
                return n;
            out[n++] = grid->probe_idx[p];
        }
    }
    return n;
}
