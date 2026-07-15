/**
 * @file chunk_grid_bounds.c
 * @brief chunk_grid per-chunk AABBs + AABB overlap test (see chunk_grid.h).
 */
#include "ferrum/renderer/chunk/chunk_grid.h"

#include <stddef.h>

void chunk_grid_bounds(const chunk_grid_t *g, uint32_t index,
                       phys_aabb_t *inner, phys_aabb_t *outer)
{
    int ijk[3];
    chunk_grid_ijk(g, index, ijk);
    float lo[3], hi[3];
    for (int a = 0; a < 3; ++a) {
        lo[a] = g->min[a] + (float)ijk[a] * g->chunk_size;
        hi[a] = lo[a] + g->chunk_size;
    }
    if (inner) {
        *inner = (phys_aabb_t){ { lo[0], lo[1], lo[2] }, { hi[0], hi[1], hi[2] } };
    }
    if (outer) {
        float m = g->margin;
        *outer = (phys_aabb_t){ { lo[0]-m, lo[1]-m, lo[2]-m },
                                { hi[0]+m, hi[1]+m, hi[2]+m } };
    }
}

bool chunk_grid_overlaps_aabb(const chunk_grid_t *g, uint32_t index, phys_aabb_t box)
{
    phys_aabb_t outer;
    chunk_grid_bounds(g, index, NULL, &outer);
    return box.min.x <= outer.max.x && box.max.x >= outer.min.x
        && box.min.y <= outer.max.y && box.max.y >= outer.min.y
        && box.min.z <= outer.max.z && box.max.z >= outer.min.z;
}
