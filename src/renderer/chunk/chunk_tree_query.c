/**
 * @file chunk_tree_query.c
 * @brief Adaptive chunk-partition point/box queries (see chunk_tree.h).
 */
#include "ferrum/renderer/chunk/chunk_tree.h"

#include <math.h>
#include <stddef.h>

uint32_t chunk_tree_of_point(const chunk_tree_t *t, float x, float y, float z)
{
    if (t == NULL || t->nodes == NULL || t->base_size <= 0.0f) return UINT32_MAX;
    /* Locate the base-grid root cell. */
    int gi = (int)floorf((x - t->min[0]) / t->base_size);
    int gj = (int)floorf((y - t->min[1]) / t->base_size);
    int gk = (int)floorf((z - t->min[2]) / t->base_size);
    if (gi < 0 || gi >= t->gdims[0] || gj < 0 || gj >= t->gdims[1] ||
        gk < 0 || gk >= t->gdims[2])
        return UINT32_MAX;
    uint32_t ni = (uint32_t)((gk * t->gdims[1] + gj) * t->gdims[0] + gi);

    /* Descend the octree: at each internal node pick the octant containing p. */
    for (;;) {
        const chunk_tree_node_t *nd = &t->nodes[ni];
        if (nd->first_child < 0) return (uint32_t)nd->leaf_index;
        float h = nd->size * 0.5f;
        int oi = (x >= nd->min[0] + h) ? 1 : 0;
        int oj = (y >= nd->min[1] + h) ? 1 : 0;
        int ok = (z >= nd->min[2] + h) ? 1 : 0;
        ni = (uint32_t)nd->first_child + (uint32_t)((ok * 2 + oj) * 2 + oi);
    }
}

void chunk_tree_bounds(const chunk_tree_t *t, uint32_t leaf,
                       phys_aabb_t *inner, phys_aabb_t *outer)
{
    if (t == NULL || t->leaf_node == NULL || leaf >= t->leaf_count) return;
    const chunk_tree_node_t *nd = &t->nodes[t->leaf_node[leaf]];
    if (inner != NULL) {
        inner->min.x = nd->min[0]; inner->min.y = nd->min[1]; inner->min.z = nd->min[2];
        inner->max.x = nd->min[0] + nd->size;
        inner->max.y = nd->min[1] + nd->size;
        inner->max.z = nd->min[2] + nd->size;
    }
    if (outer != NULL) {
        float m = t->margin;
        outer->min.x = nd->min[0] - m; outer->min.y = nd->min[1] - m; outer->min.z = nd->min[2] - m;
        outer->max.x = nd->min[0] + nd->size + m;
        outer->max.y = nd->min[1] + nd->size + m;
        outer->max.z = nd->min[2] + nd->size + m;
    }
}

bool chunk_tree_overlaps_aabb(const chunk_tree_t *t, uint32_t leaf, phys_aabb_t box)
{
    if (t == NULL || t->leaf_node == NULL || leaf >= t->leaf_count) return false;
    phys_aabb_t outer;
    chunk_tree_bounds(t, leaf, NULL, &outer);
    return box.min.x <= outer.max.x && box.max.x >= outer.min.x &&
           box.min.y <= outer.max.y && box.max.y >= outer.min.y &&
           box.min.z <= outer.max.z && box.max.z >= outer.min.z;
}
