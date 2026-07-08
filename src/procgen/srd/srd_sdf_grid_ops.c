/**
 * @file srd_sdf_grid_ops.c
 * @brief Grid-wide operations: fill, copy, count.
 *
 * Non-static functions (3): srd_sdf_grid_fill, srd_sdf_grid_copy,
 *                           srd_sdf_grid_count_negative
 */
#include "ferrum/procgen/srd/srd_sdf_grid.h"

#include <stdlib.h>
#include <string.h>

void srd_sdf_grid_fill(srd_sdf_grid_t *grid, float value) {
    if (!grid || !grid->values) return;
    int total = grid->nx * grid->ny * grid->nz;
    for (int i = 0; i < total; i++)
        grid->values[i] = value;
}

int srd_sdf_grid_copy(srd_sdf_grid_t *dst, const srd_sdf_grid_t *src) {
    if (!dst || !src || !src->values) return -1;

    int total = src->nx * src->ny * src->nz;
    float *values = (float *)malloc((size_t)total * sizeof(float));
    if (!values) return -1;

    memcpy(values, src->values, (size_t)total * sizeof(float));

    dst->values = values;
    dst->nx = src->nx;
    dst->ny = src->ny;
    dst->nz = src->nz;
    dst->voxel_size = src->voxel_size;
    dst->origin[0] = src->origin[0];
    dst->origin[1] = src->origin[1];
    dst->origin[2] = src->origin[2];

    return 0;
}

int srd_sdf_grid_count_negative(const srd_sdf_grid_t *grid) {
    if (!grid || !grid->values) return 0;
    int total = grid->nx * grid->ny * grid->nz;
    int count = 0;
    for (int i = 0; i < total; i++) {
        if (grid->values[i] < 0.0f) count++;
    }
    return count;
}
