/**
 * @file gjk_epa.h
 * @brief GJK intersection test and EPA penetration depth for convex shapes.
 *
 * GJK (Gilbert-Johnson-Keerthi) tests whether two convex shapes overlap
 * and computes closest points when separated.  EPA (Expanding Polytope
 * Algorithm) computes penetration depth and contact normal when shapes
 * overlap.
 *
 * Shapes are abstracted via support function callbacks so GJK/EPA work
 * with any convex primitive (sphere, box, capsule, convex hull).
 *
 * Public types (2):
 *   1. phys_gjk_support_fn  — support function callback
 *   2. phys_gjk_result_t    — GJK/EPA output
 */

#ifndef FERRUM_PHYSICS_GJK_EPA_H
#define FERRUM_PHYSICS_GJK_EPA_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/physics/phys_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Support function callback for GJK/EPA.
 *
 * Given a direction vector, returns the point on the shape's surface
 * furthest along that direction (in world space).
 *
 * @param shape_data  Opaque pointer to shape-specific data.
 * @param dir         Search direction (world space, not necessarily unit).
 * @return Support point in world space.
 */
typedef phys_vec3_t (*phys_gjk_support_fn)(const void *shape_data,
                                           phys_vec3_t dir);

/**
 * @brief Result of a GJK/EPA query.
 *
 * If `intersecting` is false, `distance` is the separation distance and
 * `closest_a` / `closest_b` are the closest points on each shape.
 *
 * If `intersecting` is true and EPA succeeds, `penetration` is the
 * overlap depth, `normal` points from A toward B, and `closest_a` /
 * `closest_b` are the deepest contact points.
 */
typedef struct phys_gjk_result {
    bool intersecting;       /**< True if shapes overlap. */
    float distance;          /**< Separation distance (valid when !intersecting). */
    float penetration;       /**< Penetration depth (valid when intersecting). */
    phys_vec3_t normal;      /**< Contact normal, A→B (valid when intersecting). */
    phys_vec3_t closest_a;   /**< Closest/contact point on shape A (world space). */
    phys_vec3_t closest_b;   /**< Closest/contact point on shape B (world space). */
} phys_gjk_result_t;

/* ── Public API (2 functions) ──────────────────────────────────── */

/**
 * @brief Run GJK to test intersection between two convex shapes.
 *
 * Returns whether the shapes intersect.  When separated, also computes
 * the closest points and separation distance.
 *
 * @param support_a   Support function for shape A.
 * @param shape_a     Opaque data for shape A.
 * @param support_b   Support function for shape B.
 * @param shape_b     Opaque data for shape B.
 * @param result      Output result (non-NULL).
 * @return true if shapes intersect, false if separated.
 *
 * Side effects: writes to *result.
 */
bool phys_gjk_intersect(phys_gjk_support_fn support_a, const void *shape_a,
                         phys_gjk_support_fn support_b, const void *shape_b,
                         phys_gjk_result_t *result);

/**
 * @brief Run EPA to find penetration depth after GJK detected overlap.
 *
 * Must only be called after phys_gjk_intersect returns true.  Expands
 * the GJK simplex into a polytope and finds the minimum translation
 * vector (MTV).
 *
 * @param support_a   Support function for shape A.
 * @param shape_a     Opaque data for shape A.
 * @param support_b   Support function for shape B.
 * @param shape_b     Opaque data for shape B.
 * @param result      In/out: must contain valid GJK simplex on entry;
 *                    penetration/normal/closest points filled on exit.
 * @return true on success, false if EPA fails to converge.
 *
 * Side effects: writes to *result.
 */
bool phys_epa_penetration(phys_gjk_support_fn support_a, const void *shape_a,
                           phys_gjk_support_fn support_b, const void *shape_b,
                           phys_gjk_result_t *result);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_GJK_EPA_H */
