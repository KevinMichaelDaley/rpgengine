/**
 * @file chunk_grid_index.c
 * @brief chunk_grid index <-> ijk mapping + point lookup (see chunk_grid.h).
 */
#include "ferrum/renderer/chunk/chunk_grid.h"

#include <math.h>

uint32_t chunk_grid_index(const chunk_grid_t *g, int i, int j, int k)
{
    return (uint32_t)i
         + (uint32_t)j * (uint32_t)g->dims[0]
         + (uint32_t)k * (uint32_t)g->dims[0] * (uint32_t)g->dims[1];
}

void chunk_grid_ijk(const chunk_grid_t *g, uint32_t index, int out_ijk[3])
{
    uint32_t nx = (uint32_t)g->dims[0];
    uint32_t ny = (uint32_t)g->dims[1];
    out_ijk[0] = (int)(index % nx);
    out_ijk[1] = (int)((index / nx) % ny);
    out_ijk[2] = (int)(index / (nx * ny));
}

uint32_t chunk_grid_of_point(const chunk_grid_t *g, float x, float y, float z)
{
    const float p[3] = { x, y, z };
    int c[3];
    for (int a = 0; a < 3; ++a) {
        c[a] = (int)floorf((p[a] - g->min[a]) / g->chunk_size);
        if (c[a] < 0 || c[a] >= g->dims[a])
            return UINT32_MAX;    /* outside the grid */
    }
    return chunk_grid_index(g, c[0], c[1], c[2]);
}
