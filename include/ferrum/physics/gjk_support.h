/**
 * @file gjk_support.h
 * @brief Shared GJK support function data types and implementations.
 *
 * Support functions map a direction to the furthest point on a convex
 * shape's surface.  These are used by both the narrowphase (GJK/EPA)
 * and the dynamic-dynamic CCD sweep (bisection + GJK at interpolated
 * poses).
 *
 * Public types (4):
 *   1. phys_gjk_sphere_data_t
 *   2. phys_gjk_box_data_t
 *
 * Public functions (4):
 *   1. phys_gjk_support_sphere
 *   2. phys_gjk_support_box
 *   3. phys_gjk_support_capsule
 *   4. phys_gjk_support_hull
 */

#ifndef FERRUM_PHYSICS_GJK_SUPPORT_H
#define FERRUM_PHYSICS_GJK_SUPPORT_H

#include "ferrum/physics/phys_types.h"
#include "ferrum/physics/gjk_epa.h"
#include "ferrum/physics/convex_hull.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Support function data structures ──────────────────────────── */

/** Sphere support data: center + radius in world space. */
typedef struct phys_gjk_sphere_data {
    phys_vec3_t center;
    float radius;
} phys_gjk_sphere_data_t;

/** Box support data: center + orientation + half-extents in world space. */
typedef struct phys_gjk_box_data {
    phys_vec3_t center;
    phys_quat_t rotation;
    phys_vec3_t half_extents;
} phys_gjk_box_data_t;

/** Capsule support data: center + orientation + dimensions. */
typedef struct phys_gjk_capsule_data {
    phys_vec3_t center;
    phys_quat_t rotation;
    float radius;
    float half_height;
} phys_gjk_capsule_data_t;

/** Convex hull support data: hull + world-space transform. */
typedef struct phys_gjk_hull_data {
    const phys_convex_hull_t *hull;
    phys_vec3_t center;
    phys_quat_t rotation;
} phys_gjk_hull_data_t;

/* ── Support functions (compatible with phys_gjk_support_fn) ──── */

/**
 * @brief GJK support function for a sphere.
 * @param data  Pointer to phys_gjk_sphere_data_t.
 * @param dir   Search direction (world space).
 * @return Furthest point on the sphere surface along dir.
 */
phys_vec3_t phys_gjk_support_sphere(const void *data, phys_vec3_t dir);

/**
 * @brief GJK support function for an oriented box.
 * @param data  Pointer to phys_gjk_box_data_t.
 * @param dir   Search direction (world space).
 * @return Furthest point on the box surface along dir.
 */
phys_vec3_t phys_gjk_support_box(const void *data, phys_vec3_t dir);

/**
 * @brief GJK support function for a capsule.
 * @param data  Pointer to phys_gjk_capsule_data_t.
 * @param dir   Search direction (world space).
 * @return Furthest point on the capsule surface along dir.
 */
phys_vec3_t phys_gjk_support_capsule(const void *data, phys_vec3_t dir);

/**
 * @brief GJK support function for a convex hull.
 * @param data  Pointer to phys_gjk_hull_data_t.
 * @param dir   Search direction (world space).
 * @return Furthest point on the hull surface along dir.
 */
phys_vec3_t phys_gjk_support_hull(const void *data, phys_vec3_t dir);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PHYSICS_GJK_SUPPORT_H */
