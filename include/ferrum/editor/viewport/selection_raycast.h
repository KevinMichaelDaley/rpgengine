/**
 * @file selection_raycast.h
 * @brief Raycast intersection tests for entity picking and box selection.
 *
 * Provides ray-AABB, ray-sphere, and frustum-AABB intersection tests
 * used by the viewport selection system.
 *
 * Ownership: no allocations; all data passed by pointer or value.
 * Nullability: all pointer params must be non-NULL.
 * Error semantics: intersection functions return bool; frustum_from_camera
 *                  returns 0 on success, -1 on error.
 * Side effects: none (pure math).
 *
 * Public types: editor_frustum_t, pick_candidate_t (2-type rule).
 */
#ifndef FERRUM_EDITOR_VIEWPORT_SELECTION_RAYCAST_H
#define FERRUM_EDITOR_VIEWPORT_SELECTION_RAYCAST_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "ferrum/math/vec3.h"

/* Forward declarations. */
struct editor_camera;
struct editor_ray;

/* ---- Types ---- */

/**
 * @brief Six-plane view frustum for culling/selection tests.
 *
 * Each plane is stored as (nx, ny, nz, d) where the plane equation
 * is: nx*x + ny*y + nz*z + d >= 0 for points inside.
 */
typedef struct editor_frustum {
    float planes[6][4];  /**< Six frustum planes [left,right,bottom,top,near,far]. */
} editor_frustum_t;

/**
 * @brief A candidate entity for raycast picking.
 */
typedef struct pick_candidate {
    uint32_t entity_id;   /**< Entity ID. */
    vec3_t   aabb_min;    /**< AABB minimum corner. */
    vec3_t   aabb_max;    /**< AABB maximum corner. */
} pick_candidate_t;

/* ---- Ray intersection ---- */

/**
 * @brief Test ray-AABB intersection (slab method).
 *
 * @param ray       Ray (non-NULL).
 * @param aabb_min  AABB minimum corner.
 * @param aabb_max  AABB maximum corner.
 * @param t_hit     Output: distance along ray to entry point (non-NULL).
 * @return true if ray intersects the AABB.
 */
bool ray_intersect_aabb(const struct editor_ray *ray,
                         vec3_t aabb_min, vec3_t aabb_max, float *t_hit);

/**
 * @brief Test ray-sphere intersection.
 *
 * @param ray     Ray (non-NULL).
 * @param center  Sphere center.
 * @param radius  Sphere radius (>= 0).
 * @param t_hit   Output: distance along ray to nearest hit (non-NULL).
 * @return true if ray intersects the sphere.
 */
bool ray_intersect_sphere(const struct editor_ray *ray,
                           vec3_t center, float radius, float *t_hit);

/* ---- Frustum ---- */

/**
 * @brief Extract frustum planes from a camera's view-projection matrix.
 *
 * @param cam     Camera (non-NULL).
 * @param aspect  Viewport aspect ratio.
 * @param out     Output frustum (non-NULL).
 * @return 0 on success, -1 on error.
 */
int editor_frustum_from_camera(const struct editor_camera *cam,
                                float aspect, editor_frustum_t *out);

/**
 * @brief Test frustum-AABB intersection.
 *
 * @param frustum   Frustum (non-NULL).
 * @param aabb_min  AABB minimum corner.
 * @param aabb_max  AABB maximum corner.
 * @return true if AABB intersects or is inside the frustum.
 */
bool frustum_intersect_aabb(const editor_frustum_t *frustum,
                             vec3_t aabb_min, vec3_t aabb_max);

/* ---- Picking ---- */

/**
 * @brief Find the nearest entity hit by a ray.
 *
 * Tests ray against each candidate's AABB and returns the closest hit.
 *
 * @param ray          Ray (non-NULL).
 * @param candidates   Array of pick candidates (may be NULL if count is 0).
 * @param count        Number of candidates.
 * @param out_id       Output: entity ID of nearest hit (non-NULL).
 * @return true if any candidate was hit.
 */
bool pick_nearest_entity(const struct editor_ray *ray,
                          const pick_candidate_t *candidates, uint32_t count,
                          uint32_t *out_id);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_VIEWPORT_SELECTION_RAYCAST_H */
