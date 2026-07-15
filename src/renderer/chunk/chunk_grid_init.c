/**
 * @file chunk_grid_init.c
 * @brief chunk_grid construction + count (see chunk_grid.h).
 */
#include "ferrum/renderer/chunk/chunk_grid.h"

#include <stddef.h>

#include <math.h>

bool chunk_grid_init(chunk_grid_t *g, phys_aabb_t bounds, float chunk_size, float margin)
{
    if (g == NULL || chunk_size <= 0.0f)
        return false;

    const float mn[3] = { bounds.min.x, bounds.min.y, bounds.min.z };
    const float mx[3] = { bounds.max.x, bounds.max.y, bounds.max.z };
    for (int a = 0; a < 3; ++a)
        if (mx[a] < mn[a])
            return false;

    g->chunk_size = chunk_size;
    g->margin = margin;
    for (int a = 0; a < 3; ++a) {
        g->min[a] = mn[a];
        float extent = mx[a] - mn[a];
        int n = (int)ceilf(extent / chunk_size);
        g->dims[a] = n < 1 ? 1 : n;   /* always at least one chunk per axis */
    }
    return true;
}

uint32_t chunk_grid_count(const chunk_grid_t *g)
{
    if (g == NULL)
        return 0u;
    return (uint32_t)g->dims[0] * (uint32_t)g->dims[1] * (uint32_t)g->dims[2];
}
