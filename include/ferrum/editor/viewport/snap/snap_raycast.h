/**
 * @file snap_raycast.h
 * @brief Ray-triangle and ray-mesh intersection for surface snap modes.
 *
 * Provides general-purpose ray-triangle intersection (Möller–Trumbore,
 * no upper t bound) and ray-vs-snap-mesh iteration for finding the
 * nearest face hit on an entity's geometry.
 *
 * Ownership: no allocations; all data passed by pointer or value.
 * Nullability: all pointer params must be non-NULL.
 * Error semantics: functions return false on miss.
 * Side effects: none (pure math).
 *
 * Public types: snap_target_mode_t, snap_hit_t (2-type rule).
 */
#ifndef FERRUM_EDITOR_VIEWPORT_SNAP_RAYCAST_H
#define FERRUM_EDITOR_VIEWPORT_SNAP_RAYCAST_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "ferrum/math/vec3.h"
#include "ferrum/math/mat4.h"

/* Forward declarations. */
struct snap_mesh;

/**
 * @brief Surface snap target mode.
 */
typedef enum snap_target_mode {
    SNAP_TARGET_FACE    = 0,  /**< Snap to face center, orient to face normal. */
    SNAP_TARGET_VERTEX  = 1,  /**< Snap to nearest vertex, orient to vertex normal. */
    SNAP_TARGET_SURFACE = 2   /**< Face snap + offset along normal by AABB extent. */
} snap_target_mode_t;

/**
 * @brief Result of a surface snap raycast.
 */
typedef struct snap_hit {
    vec3_t   position;       /**< World-space snap target position. */
    vec3_t   normal;         /**< World-space surface normal at hit. */
    uint32_t entity_id;      /**< Entity that was hit. */
    uint32_t face_index;     /**< Triangle index (face_index * 3 = first index). */
    float    distance;       /**< Ray parameter (distance along ray direction). */
    bool     valid;          /**< True if a hit was found. */
} snap_hit_t;

/* ---- Ray-triangle (snap_ray_triangle.c) ---- */

/**
 * @brief Ray-triangle intersection (Möller–Trumbore).
 *
 * Unlike phys_ray_vs_triangle(), this does NOT constrain t to [0,1].
 * Returns true for any t > 0 (hit in front of ray origin, any distance).
 *
 * @param origin  Ray origin.
 * @param dir     Ray direction (normalized).
 * @param v0      Triangle vertex 0.
 * @param v1      Triangle vertex 1.
 * @param v2      Triangle vertex 2.
 * @param t_out   Output: ray parameter at intersection.
 * @return true if ray hits the triangle (t > 0).
 */
bool snap_ray_vs_triangle(vec3_t origin, vec3_t dir,
                            vec3_t v0, vec3_t v1, vec3_t v2,
                            float *t_out);

/**
 * @brief Ray vs snap mesh: find nearest triangle hit.
 *
 * Transforms each triangle by the model matrix, tests against the ray,
 * and returns the nearest hit (smallest t > 0).
 *
 * @param origin     Ray origin (world space).
 * @param dir        Ray direction (world space, normalized).
 * @param mesh       Snap mesh with CPU-side geometry (non-NULL).
 * @param model      Model matrix for the entity (non-NULL).
 * @param out_t      Output: ray parameter at nearest hit.
 * @param out_face   Output: triangle index (0-based, face_idx * 3 into indices).
 * @param out_normal Output: world-space face normal of the hit triangle.
 * @return true if any triangle was hit.
 */
bool snap_ray_vs_snap_mesh(vec3_t origin, vec3_t dir,
                             const struct snap_mesh *mesh,
                             const mat4_t *model,
                             float *out_t, uint32_t *out_face,
                             vec3_t *out_normal);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_VIEWPORT_SNAP_RAYCAST_H */
