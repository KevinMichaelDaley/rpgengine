/**
 * @file sphere_simplify.c
 * @brief Sphere simplification radius and ratio for complex shapes.
 *
 * Sphere simplification only applies to complex shape types (mesh,
 * convex hull, compound/articulated).  Primitives (sphere, box,
 * capsule) always use exact narrowphase tests.
 *
 * TODO: Implement spherical decomposition for mesh/hull shapes.
 * Currently stubs return 0 (not eligible) for all types.
 */

#include "ferrum/physics/collision/sphere_simplify.h"

/* ── Public API ─────────────────────────────────────────────────── */

float phys_sphere_ratio(const phys_collider_t *collider,
                        const phys_sphere_t *spheres,
                        const phys_box_t *boxes,
                        const phys_capsule_t *capsules)
{
    if (!collider) return 0.0f;
    (void)spheres;
    (void)boxes;
    (void)capsules;

    switch (collider->type) {
    case PHYS_SHAPE_SPHERE:
    case PHYS_SHAPE_BOX:
    case PHYS_SHAPE_CAPSULE:
        /* Primitives have exact narrowphase tests — sphere
         * simplification should never apply.  Return 0 so the
         * sphere_simplify flag is never set for these types. */
        return 0.0f;

    case PHYS_SHAPE_COMPOUND:
    case PHYS_SHAPE_CONVEX:
    case PHYS_SHAPE_MESH:
        /* TODO: Compute circumradius/inradius ratio from the spherical
         * decomposition of the mesh/hull.  For now return 0 (not
         * eligible) until decomposition is implemented. */
        return 0.0f;

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

    case PHYS_SHAPE_BOX:
    case PHYS_SHAPE_CAPSULE:
        /* Primitives should never use sphere simplification — they have
         * exact narrowphase tests.  Return 0 to signal invalid. */
        (void)boxes;
        (void)capsules;
        return 0.0f;

    case PHYS_SHAPE_COMPOUND:
    case PHYS_SHAPE_CONVEX:
    case PHYS_SHAPE_MESH:
        /* TODO: Perform spherical decomposition of the mesh/hull and
         * return the bounding sphere radius.  For now, fall through to
         * return 0 (disabling sphere simplification until decomposition
         * is implemented). */
        return 0.0f;

    default:
        return 0.0f;
    }
}
