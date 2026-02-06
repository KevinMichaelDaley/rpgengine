/**
 * @file aabb_ops.c
 * @brief AABB operations: overlap, merge, expand, center.
 *
 * 4 non-static functions.
 */

#include "ferrum/physics/aabb.h"

#include <stddef.h>

#include "ferrum/math/vec3.h"

bool phys_aabb_overlap(const phys_aabb_t *a, const phys_aabb_t *b)
{
    if (!a || !b) { return false; }

    /* Separated if any axis has no overlap. Touching counts as overlap. */
    if (a->max.x < b->min.x || a->min.x > b->max.x) { return false; }
    if (a->max.y < b->min.y || a->min.y > b->max.y) { return false; }
    if (a->max.z < b->min.z || a->min.z > b->max.z) { return false; }

    return true;
}

void phys_aabb_merge(phys_aabb_t *out, const phys_aabb_t *a,
                     const phys_aabb_t *b)
{
    if (!out || !a || !b) { return; }

    out->min.x = (a->min.x < b->min.x) ? a->min.x : b->min.x;
    out->min.y = (a->min.y < b->min.y) ? a->min.y : b->min.y;
    out->min.z = (a->min.z < b->min.z) ? a->min.z : b->min.z;

    out->max.x = (a->max.x > b->max.x) ? a->max.x : b->max.x;
    out->max.y = (a->max.y > b->max.y) ? a->max.y : b->max.y;
    out->max.z = (a->max.z > b->max.z) ? a->max.z : b->max.z;
}

void phys_aabb_expand(phys_aabb_t *aabb, float margin)
{
    if (!aabb) { return; }

    phys_vec3_t m = {margin, margin, margin};
    aabb->min = vec3_sub(aabb->min, m);
    aabb->max = vec3_add(aabb->max, m);
}

phys_vec3_t phys_aabb_center(const phys_aabb_t *aabb)
{
    if (!aabb) { return (phys_vec3_t){0.0f, 0.0f, 0.0f}; }

    return vec3_scale(vec3_add(aabb->min, aabb->max), 0.5f);
}
