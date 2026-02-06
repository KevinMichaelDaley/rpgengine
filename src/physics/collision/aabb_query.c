/**
 * @file aabb_query.c
 * @brief AABB query functions: extents, surface_area.
 *
 * 2 non-static functions.
 */

#include "ferrum/physics/aabb.h"

#include <stddef.h>

#include "ferrum/math/vec3.h"

phys_vec3_t phys_aabb_extents(const phys_aabb_t *aabb)
{
    if (!aabb) { return (phys_vec3_t){0.0f, 0.0f, 0.0f}; }

    return vec3_sub(aabb->max, aabb->min);
}

float phys_aabb_surface_area(const phys_aabb_t *aabb)
{
    if (!aabb) { return 0.0f; }

    phys_vec3_t d = vec3_sub(aabb->max, aabb->min);
    return 2.0f * (d.x * d.y + d.x * d.z + d.y * d.z);
}
