/**
 * @file chunk_tree_build.c
 * @brief Adaptive chunk-partition build + count (see chunk_tree.h).
 */
#include "ferrum/renderer/chunk/chunk_tree.h"

#include <math.h>
#include <string.h>

#include "ferrum/memory/arena.h"

/* Whether cubic cell [min, min+size] (grown by the leaf margin) intersects any
 * detail box (grown by detail_pad). Detail boxes are flat [x,y,z] triples. */
static int cell_hits_detail(const float min[3], float size, float margin,
                            const float *dmin, const float *dmax,
                            uint32_t n_detail, float pad)
{
    if (n_detail == 0u || dmin == NULL || dmax == NULL) return 0;
    float lo[3], hi[3];
    for (int a = 0; a < 3; ++a) { lo[a] = min[a] - margin; hi[a] = min[a] + size + margin; }
    for (uint32_t d = 0; d < n_detail; ++d) {
        const float *mn = &dmin[d * 3], *mx = &dmax[d * 3];
        if (lo[0] <= mx[0] + pad && hi[0] >= mn[0] - pad &&
            lo[1] <= mx[1] + pad && hi[1] >= mn[1] - pad &&
            lo[2] <= mx[2] + pad && hi[2] >= mn[2] - pad)
            return 1;
    }
    return 0;
}

/* A cell subdivides when it can still halve to >= min_chunk AND it straddles
 * detail. Shared by the count and emit passes so both agree exactly. */
static int cell_splits(const float min[3], float size, float min_chunk, float margin,
                       const float *dmin, const float *dmax, uint32_t n_detail, float pad)
{
    if (size < 2.0f * min_chunk - 1e-3f) return 0;   /* children would be < min_chunk. */
    return cell_hits_detail(min, size, margin, dmin, dmax, n_detail, pad);
}

/* Recursively COUNT the nodes + leaves a cell expands to (mirrors emit). */
static void count_cell(const float min[3], float size, float min_chunk, float margin,
                       const float *dmin, const float *dmax, uint32_t n_detail, float pad,
                       uint32_t *n_nodes, uint32_t *n_leaves)
{
    if (!cell_splits(min, size, min_chunk, margin, dmin, dmax, n_detail, pad)) {
        ++(*n_leaves);
        return;
    }
    float h = size * 0.5f;
    for (int k = 0; k < 2; ++k) for (int j = 0; j < 2; ++j) for (int i = 0; i < 2; ++i) {
        float cmn[3] = { min[0] + h * (float)i, min[1] + h * (float)j, min[2] + h * (float)k };
        *n_nodes += 1u;   /* the child node itself. */
        count_cell(cmn, h, min_chunk, margin, dmin, dmax, n_detail, pad, n_nodes, n_leaves);
    }
}

/* Recursively EMIT a cell's node @p ni and its subtree (children appended). */
static void emit_cell(chunk_tree_t *t, uint32_t ni, float min_chunk,
                      const float *dmin, const float *dmax, uint32_t n_detail, float pad)
{
    chunk_tree_node_t *nd = &t->nodes[ni];
    if (!cell_splits(nd->min, nd->size, min_chunk, t->margin, dmin, dmax, n_detail, pad)) {
        nd->first_child = -1;
        nd->leaf_index = (int32_t)t->leaf_count;
        t->leaf_node[t->leaf_count] = (int32_t)ni;
        ++t->leaf_count;
        return;
    }
    float h = nd->size * 0.5f;
    int32_t base = (int32_t)t->n_nodes;
    nd->first_child = base;
    nd->leaf_index = -1;
    float pmin[3] = { nd->min[0], nd->min[1], nd->min[2] };
    for (int k = 0; k < 2; ++k) for (int j = 0; j < 2; ++j) for (int i = 0; i < 2; ++i) {
        chunk_tree_node_t *c = &t->nodes[t->n_nodes++];
        c->min[0] = pmin[0] + h * (float)i;
        c->min[1] = pmin[1] + h * (float)j;
        c->min[2] = pmin[2] + h * (float)k;
        c->size = h; c->first_child = -1; c->leaf_index = -1;
    }
    for (int ci = 0; ci < 8; ++ci)
        emit_cell(t, (uint32_t)(base + ci), min_chunk, dmin, dmax, n_detail, pad);
}

bool chunk_tree_build(chunk_tree_t *t, phys_aabb_t bounds,
                      float min_chunk, float max_chunk, float margin,
                      const float *detail_min, const float *detail_max,
                      uint32_t n_detail, float detail_pad, struct arena *arena)
{
    if (t == NULL || arena == NULL || min_chunk <= 0.0f || max_chunk <= 0.0f) return false;
    if (bounds.max.x < bounds.min.x || bounds.max.y < bounds.min.y ||
        bounds.max.z < bounds.min.z) return false;
    if (min_chunk > max_chunk) min_chunk = max_chunk;

    memset(t, 0, sizeof *t);
    t->min[0] = bounds.min.x; t->min[1] = bounds.min.y; t->min[2] = bounds.min.z;
    t->base_size = max_chunk;
    t->margin = margin;
    float ext[3] = { bounds.max.x - bounds.min.x, bounds.max.y - bounds.min.y,
                     bounds.max.z - bounds.min.z };
    for (int a = 0; a < 3; ++a) {
        int n = (int)ceilf(ext[a] / max_chunk);
        t->gdims[a] = n < 1 ? 1 : n;
    }
    uint32_t n_roots = (uint32_t)t->gdims[0] * (uint32_t)t->gdims[1] * (uint32_t)t->gdims[2];

    /* Pass 1: exact node + leaf counts (roots + their subtrees). */
    uint32_t n_nodes = n_roots, n_leaves = 0;
    for (int k = 0; k < t->gdims[2]; ++k) for (int j = 0; j < t->gdims[1]; ++j)
    for (int i = 0; i < t->gdims[0]; ++i) {
        float rmn[3] = { t->min[0] + max_chunk * (float)i,
                         t->min[1] + max_chunk * (float)j,
                         t->min[2] + max_chunk * (float)k };
        count_cell(rmn, max_chunk, min_chunk, margin, detail_min, detail_max,
                   n_detail, detail_pad, &n_nodes, &n_leaves);
    }

    t->nodes = arena_alloc(arena, _Alignof(chunk_tree_node_t),
                           (size_t)n_nodes * sizeof(chunk_tree_node_t));
    t->leaf_node = arena_alloc(arena, _Alignof(int32_t), (size_t)n_leaves * sizeof(int32_t));
    if (t->nodes == NULL || t->leaf_node == NULL) return false;

    /* Pass 2: create the root cells (contiguous, grid order) then subdivide each so
     * chunk_tree_of_point can index a root directly from the base grid. */
    t->n_nodes = n_roots;
    for (int k = 0; k < t->gdims[2]; ++k) for (int j = 0; j < t->gdims[1]; ++j)
    for (int i = 0; i < t->gdims[0]; ++i) {
        uint32_t ri = (uint32_t)((k * t->gdims[1] + j) * t->gdims[0] + i);
        chunk_tree_node_t *r = &t->nodes[ri];
        r->min[0] = t->min[0] + max_chunk * (float)i;
        r->min[1] = t->min[1] + max_chunk * (float)j;
        r->min[2] = t->min[2] + max_chunk * (float)k;
        r->size = max_chunk; r->first_child = -1; r->leaf_index = -1;
    }
    for (uint32_t r = 0; r < n_roots; ++r)
        emit_cell(t, r, min_chunk, detail_min, detail_max, n_detail, detail_pad);
    return true;
}

uint32_t chunk_tree_count(const chunk_tree_t *t)
{
    return t == NULL ? 0u : t->leaf_count;
}
