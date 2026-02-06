#ifndef FERRUM_PHYSICS_AABB_H
#define FERRUM_PHYSICS_AABB_H

/** @file
 * @brief Axis-Aligned Bounding Box (AABB) structure and computation.
 *
 * Provides AABB construction from primitive shapes (sphere, box, capsule),
 * overlap testing, merging, and query operations. All functions are
 * NULL-safe — passing NULL for pointer arguments is a no-op or returns
 * a zero/false default.
 */

#include <stdbool.h>

#include "ferrum/physics/phys_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── AABB structure ─────────────────────────────────────────────── */

/**
 * @brief Axis-Aligned Bounding Box.
 *
 * Stored as world-space minimum and maximum corners.
 * Ownership: value type, no heap allocation.
 */
typedef struct phys_aabb {
    phys_vec3_t min; /**< Minimum corner (lowest x, y, z). */
    phys_vec3_t max; /**< Maximum corner (highest x, y, z). */
} phys_aabb_t;

_Static_assert(sizeof(phys_aabb_t) == 24, "phys_aabb_t must be 24 bytes");

/* ── Shape construction ─────────────────────────────────────────── */

/**
 * @brief Compute AABB for a sphere.
 * @param aabb   Output AABB (if NULL, no-op).
 * @param center Sphere center in world space.
 * @param radius Sphere radius (should be >= 0).
 *
 * No side effects beyond writing *aabb.
 */
void phys_aabb_from_sphere(phys_aabb_t *aabb, phys_vec3_t center, float radius);

/**
 * @brief Compute AABB for a rotated box.
 *
 * Constructs the world-aligned bounding box for a box with given
 * half-extents, rotated by @p rotation and centered at @p center.
 *
 * @param aabb         Output AABB (if NULL, no-op).
 * @param center       Box center in world space.
 * @param rotation     Box orientation as a unit quaternion.
 * @param half_extents Half-extents along local X, Y, Z axes.
 *
 * No side effects beyond writing *aabb.
 */
void phys_aabb_from_box(phys_aabb_t *aabb, phys_vec3_t center,
                        phys_quat_t rotation, phys_vec3_t half_extents);

/**
 * @brief Compute AABB for a rotated capsule.
 *
 * The capsule is modeled as a line segment along local +Y of length
 * 2 * @p half_height, with hemisphere caps of @p radius at each end.
 *
 * @param aabb        Output AABB (if NULL, no-op).
 * @param center      Capsule center in world space.
 * @param rotation    Capsule orientation as a unit quaternion.
 * @param radius      Capsule radius (should be >= 0).
 * @param half_height Half the cylinder segment length (should be >= 0).
 *
 * No side effects beyond writing *aabb.
 */
void phys_aabb_from_capsule(phys_aabb_t *aabb, phys_vec3_t center,
                            phys_quat_t rotation, float radius,
                            float half_height);

/* ── Operations ─────────────────────────────────────────────────── */

/**
 * @brief Test whether two AABBs overlap (including touching edges).
 * @param a First AABB (if NULL, returns false).
 * @param b Second AABB (if NULL, returns false).
 * @return true if AABBs overlap or touch, false otherwise.
 */
bool phys_aabb_overlap(const phys_aabb_t *a, const phys_aabb_t *b);

/**
 * @brief Merge two AABBs into a bounding AABB.
 * @param out Output AABB (if NULL, no-op). May alias @p a or @p b.
 * @param a   First input AABB (if NULL, no-op).
 * @param b   Second input AABB (if NULL, no-op).
 *
 * No side effects beyond writing *out.
 */
void phys_aabb_merge(phys_aabb_t *out, const phys_aabb_t *a,
                     const phys_aabb_t *b);

/**
 * @brief Expand an AABB by a uniform margin in all directions.
 * @param aabb   AABB to expand in place (if NULL, no-op).
 * @param margin Amount to expand (negative values shrink).
 *
 * No side effects beyond writing *aabb.
 */
void phys_aabb_expand(phys_aabb_t *aabb, float margin);

/**
 * @brief Compute the center point of an AABB.
 * @param aabb Input AABB (if NULL, returns zero vector).
 * @return Center point (midpoint of min and max).
 */
phys_vec3_t phys_aabb_center(const phys_aabb_t *aabb);

/* ── Queries ────────────────────────────────────────────────────── */

/**
 * @brief Compute the full extents (size) of an AABB.
 * @param aabb Input AABB (if NULL, returns zero vector).
 * @return (max - min) — the width, height, and depth.
 */
phys_vec3_t phys_aabb_extents(const phys_aabb_t *aabb);

/**
 * @brief Compute the surface area of an AABB.
 * @param aabb Input AABB (if NULL, returns 0).
 * @return Surface area = 2*(dx*dy + dx*dz + dy*dz).
 */
float phys_aabb_surface_area(const phys_aabb_t *aabb);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_AABB_H */
