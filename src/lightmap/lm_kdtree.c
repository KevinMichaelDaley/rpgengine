/**
 * @file lm_kdtree.c
 * @brief Median-split 3D kd-tree over luxel positions (see lm_kdtree.h).
 */
#include "ferrum/lightmap/lm_kdtree.h"

#include <math.h>
#include <stddef.h>

static float lm_kd_coord(const vec3_t *p, uint8_t axis)
{
    return axis == 0 ? p->x : (axis == 1 ? p->y : p->z);
}

/**
 * Reorder idx[lo..hi] so idx[k] holds the k-th smallest by @p axis (quickselect
 * with median-of-mid pivot); everything before k is <=, after k is >=.
 */
static void lm_kd_nth(const vec3_t *pts, uint32_t *idx, int lo, int hi, int k,
                      uint8_t axis)
{
    while (lo < hi) {
        float pivot = lm_kd_coord(&pts[idx[lo + (hi - lo) / 2]], axis);
        int i = lo, j = hi;
        while (i <= j) {
            while (lm_kd_coord(&pts[idx[i]], axis) < pivot) ++i;
            while (lm_kd_coord(&pts[idx[j]], axis) > pivot) --j;
            if (i <= j) {
                uint32_t t = idx[i];
                idx[i] = idx[j];
                idx[j] = t;
                ++i;
                --j;
            }
        }
        if (k <= j) hi = j;
        else if (k >= i) lo = i;
        else break;
    }
}

/** Recursively build the subtree for idx[lo..hi]; return its node index or -1. */
static int32_t lm_kd_build_range(lm_kdtree_t *t, uint32_t *idx, int lo, int hi,
                                 int depth, uint32_t *next)
{
    if (lo > hi)
        return -1;
    uint8_t axis = (uint8_t)(depth % 3);
    int mid = lo + (hi - lo) / 2;
    lm_kd_nth(t->points, idx, lo, hi, mid, axis);
    int32_t ni = (int32_t)(*next);
    ++(*next);
    lm_kdnode_t *node = &t->nodes[ni];
    node->point = idx[mid];
    node->axis = axis;
    node->left = lm_kd_build_range(t, idx, lo, mid - 1, depth + 1, next);
    node->right = lm_kd_build_range(t, idx, mid + 1, hi, depth + 1, next);
    return ni;
}

bool lm_kdtree_build(lm_kdtree_t *tree, const vec3_t *points, uint32_t count,
                     arena_t *arena)
{
    tree->points = points;
    tree->count = count;
    tree->nodes = NULL;
    tree->root = -1;
    if (count == 0)
        return true;

    tree->nodes = arena_alloc(arena, _Alignof(lm_kdnode_t),
                              (size_t)count * sizeof(lm_kdnode_t));
    if (!tree->nodes)
        return false;

    /* Scratch index array lives only for the build. */
    size_t mark = arena_mark(arena);
    uint32_t *idx = arena_alloc(arena, _Alignof(uint32_t),
                                (size_t)count * sizeof(uint32_t));
    if (!idx)
        return false;
    for (uint32_t i = 0; i < count; ++i)
        idx[i] = i;

    uint32_t next = 0;
    tree->root = lm_kd_build_range(tree, idx, 0, (int)count - 1, 0, &next);
    arena_pop_to_mark(arena, mark);
    return true;
}

static void lm_kd_nearest(const lm_kdtree_t *t, int32_t ni, vec3_t q,
                          uint32_t *best, float *best_d2)
{
    if (ni < 0)
        return;
    const lm_kdnode_t *node = &t->nodes[ni];
    float d2 = vec3_distance_sq(t->points[node->point], q);
    if (d2 < *best_d2) {
        *best_d2 = d2;
        *best = node->point;
    }
    float diff = lm_kd_coord(&q, node->axis)
                 - lm_kd_coord(&t->points[node->point], node->axis);
    int32_t near = diff < 0.0f ? node->left : node->right;
    int32_t far = diff < 0.0f ? node->right : node->left;
    lm_kd_nearest(t, near, q, best, best_d2);
    if (diff * diff < *best_d2)
        lm_kd_nearest(t, far, q, best, best_d2);
}

uint32_t lm_kdtree_nearest(const lm_kdtree_t *tree, vec3_t query)
{
    if (tree->count == 0)
        return UINT32_MAX;
    uint32_t best = UINT32_MAX;
    float best_d2 = INFINITY;
    lm_kd_nearest(tree, tree->root, query, &best, &best_d2);
    return best;
}

static void lm_kd_radius(const lm_kdtree_t *t, int32_t ni, vec3_t q, float r2,
                         uint32_t *out, uint32_t cap, uint32_t *count)
{
    if (ni < 0)
        return;
    const lm_kdnode_t *node = &t->nodes[ni];
    if (vec3_distance_sq(t->points[node->point], q) <= r2) {
        if (*count < cap)
            out[*count] = node->point;
        ++(*count);
    }
    float diff = lm_kd_coord(&q, node->axis)
                 - lm_kd_coord(&t->points[node->point], node->axis);
    int32_t near = diff < 0.0f ? node->left : node->right;
    int32_t far = diff < 0.0f ? node->right : node->left;
    lm_kd_radius(t, near, q, r2, out, cap, count);
    if (diff * diff <= r2)
        lm_kd_radius(t, far, q, r2, out, cap, count);
}

uint32_t lm_kdtree_radius(const lm_kdtree_t *tree, vec3_t query, float radius,
                          uint32_t *out, uint32_t out_cap)
{
    if (tree->count == 0)
        return 0;
    uint32_t count = 0;
    lm_kd_radius(tree, tree->root, query, radius * radius, out, out_cap, &count);
    return count;
}
