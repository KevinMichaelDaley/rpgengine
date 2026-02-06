/**
 * @file sphere_simplify.c
 * @brief Bounding-sphere ratio computation for sphere simplification.
 *
 * Computes circumradius / inradius for each primitive shape type.
 * Used at asset load to flag near-spherical shapes for cheaper
 * sphere-sphere narrowphase at T2+ distances.
 */

#include <math.h>

#include "ferrum/physics/collision/sphere_simplify.h"

/* ── Helpers ────────────────────────────────────────────────────── */

/** Return the minimum of three floats. */
static float min3f(float a, float b, float c)
{
    float m = a;
    if (b < m) m = b;
    if (c < m) m = c;
    return m;
}

/* ── Public API ─────────────────────────────────────────────────── */

float phys_sphere_ratio(const phys_collider_t *collider,
                        const phys_sphere_t *spheres,
                        const phys_box_t *boxes,
                        const phys_capsule_t *capsules)
{
    if (!collider) return 0.0f;

    switch (collider->type) {
    case PHYS_SHAPE_SPHERE:
        /* Sphere is perfectly spherical: ratio = 1.0. */
        (void)spheres;
        return 1.0f;

    case PHYS_SHAPE_BOX: {
        if (!boxes) return 0.0f;
        phys_vec3_t he = boxes[collider->shape_index].half_extents;
        float circumradius = sqrtf(he.x * he.x + he.y * he.y + he.z * he.z);
        float inradius = min3f(he.x, he.y, he.z);
        if (inradius <= 0.0f) return 0.0f;
        return circumradius / inradius;
    }

    case PHYS_SHAPE_CAPSULE: {
        if (!capsules) return 0.0f;
        float r  = capsules[collider->shape_index].radius;
        float hh = capsules[collider->shape_index].half_height;
        if (r <= 0.0f) return 0.0f;
        /* Circumradius: distance from center to tip corner. */
        float total_half = r + hh;
        float circumradius = sqrtf(r * r + total_half * total_half);
        return circumradius / r;
    }

    default:
        return 0.0f;
    }
}

float phys_sphere_simplify_radius(const phys_collider_t *collider,
                                   const phys_sphere_t *spheres,
                                   const phys_box_t *boxes,
                                   const phys_capsule_t *capsules)
{
    if (!collider) return 0.0f;

    switch (collider->type) {
    case PHYS_SHAPE_SPHERE:
        if (!spheres) return 0.0f;
        return spheres[collider->shape_index].radius;

    case PHYS_SHAPE_BOX: {
        if (!boxes) return 0.0f;
        phys_vec3_t he = boxes[collider->shape_index].half_extents;
        return sqrtf(he.x * he.x + he.y * he.y + he.z * he.z);
    }

    case PHYS_SHAPE_CAPSULE: {
        if (!capsules) return 0.0f;
        float r  = capsules[collider->shape_index].radius;
        float hh = capsules[collider->shape_index].half_height;
        float total_half = r + hh;
        return sqrtf(r * r + total_half * total_half);
    }

    default:
        return 0.0f;
    }
}
