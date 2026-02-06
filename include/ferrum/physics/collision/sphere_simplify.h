#ifndef FERRUM_PHYSICS_COLLISION_SPHERE_SIMPLIFY_H
#define FERRUM_PHYSICS_COLLISION_SPHERE_SIMPLIFY_H

/** @file
 * @brief Sphere simplification utilities for tiered narrowphase.
 *
 * At asset load, near-spherical shapes (bounding-sphere ratio < 1.3)
 * get a sphere_simplify flag.  At T2+ distances, the narrowphase
 * uses cheap sphere-sphere tests instead of full shape collision.
 *
 * The bounding-sphere ratio is circumradius / inradius:
 * - Sphere: always 1.0
 * - Box: length(half_extents) / min(half_extents.x, y, z)
 * - Capsule: sqrt(r² + (r+hh)²) / r
 */

#include "ferrum/physics/collider.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Compute the bounding-sphere ratio for a collider's shape.
 *
 * Returns circumradius / inradius for the shape referenced by the
 * collider.  A value of 1.0 means perfectly spherical.  Values < 1.3
 * are candidates for sphere simplification at distance.
 *
 * @param collider  Collider to evaluate (non-NULL).
 * @param spheres   Sphere shape pool (may be NULL if collider is not a sphere).
 * @param boxes     Box shape pool (may be NULL if collider is not a box).
 * @param capsules  Capsule shape pool (may be NULL if collider is not a capsule).
 * @return Bounding-sphere ratio (>= 1.0), or 0.0f on error.
 *
 * Ownership: reads from the pools; does not modify anything.
 * Nullability: collider must be non-NULL; the relevant shape pool for
 *              the collider's type must also be non-NULL.
 * Error semantics: returns 0.0f if collider is NULL or the shape type
 *                  is unsupported.
 * Side effects: none.
 */
float phys_sphere_ratio(const phys_collider_t *collider,
                        const phys_sphere_t *spheres,
                        const phys_box_t *boxes,
                        const phys_capsule_t *capsules);

/**
 * @brief Return the bounding sphere radius (circumradius) for a collider.
 *
 * Used during sphere-simplified narrowphase dispatch at T2+ to get the
 * bounding sphere radius for sphere-sphere testing.
 *
 * @param collider  Collider to evaluate (non-NULL).
 * @param spheres   Sphere shape pool (may be NULL if not a sphere).
 * @param boxes     Box shape pool (may be NULL if not a box).
 * @param capsules  Capsule shape pool (may be NULL if not a capsule).
 * @return Bounding sphere radius (circumradius), or 0.0f on error.
 *
 * Ownership: reads from the pools; does not modify anything.
 * Nullability: collider must be non-NULL; the relevant shape pool must
 *              also be non-NULL.
 * Error semantics: returns 0.0f if collider is NULL or shape type is
 *                  unsupported.
 * Side effects: none.
 */
float phys_sphere_simplify_radius(const phys_collider_t *collider,
                                   const phys_sphere_t *spheres,
                                   const phys_box_t *boxes,
                                   const phys_capsule_t *capsules);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_COLLISION_SPHERE_SIMPLIFY_H */
