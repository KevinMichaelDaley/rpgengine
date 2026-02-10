/**
 * @file sphere_simplify.c
 * @brief Sphere simplification radius and ratio utilities.
 *
 * Computes the circumradius/inradius ratio and the bounding-sphere radius
 * (circumradius) for primitive shapes. Complex shapes (mesh/convex/compound)
 * are currently unsupported and return 0.
 */

#include <math.h>

#include "ferrum/physics/collision/sphere_simplify.h"

/* ── Public API ─────────────────────────────────────────────────── */

float phys_sphere_ratio(const phys_collider_t *collider,
                        const phys_sphere_t *spheres,
                        const phys_box_t *boxes,
                        const phys_capsule_t *capsules)
{
    if (!collider) return 0.0f;

    switch (collider->type) {
    case PHYS_SHAPE_SPHERE: {
        if (!spheres) return 0.0f;
        float r = spheres[collider->shape_index].radius;
        if (r <= 0.0f) return 0.0f;
        return 1.0f;
    }

    case PHYS_SHAPE_BOX: {
        if (!boxes) return 0.0f;
        phys_vec3_t he = boxes[collider->shape_index].half_extents;
        float inr = he.x;
        if (he.y < inr) inr = he.y;
        if (he.z < inr) inr = he.z;
        if (inr <= 0.0f) return 0.0f;
        float cr = sqrtf(he.x * he.x + he.y * he.y + he.z * he.z);
        return cr / inr;
    }

    case PHYS_SHAPE_CAPSULE: {
        if (!capsules) return 0.0f;
        float r  = capsules[collider->shape_index].radius;
        float hh = capsules[collider->shape_index].half_height;
        if (r <= 0.0f) return 0.0f;
        float cr = sqrtf(r * r + (r + hh) * (r + hh));
        return cr / r;
    }

    case PHYS_SHAPE_COMPOUND:
    case PHYS_SHAPE_CONVEX:
    case PHYS_SHAPE_MESH:
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
        float cr = sqrtf(he.x * he.x + he.y * he.y + he.z * he.z);
        return cr;
    }

    case PHYS_SHAPE_CAPSULE: {
        if (!capsules) return 0.0f;
        float r  = capsules[collider->shape_index].radius;
        float hh = capsules[collider->shape_index].half_height;
        if (r <= 0.0f) return 0.0f;
        return sqrtf(r * r + (r + hh) * (r + hh));
    }

    case PHYS_SHAPE_COMPOUND:
    case PHYS_SHAPE_CONVEX:
    case PHYS_SHAPE_MESH:
    default:
        return 0.0f;
    }
}
