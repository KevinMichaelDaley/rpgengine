/**
 * @file cursor_place.h
 * @brief Ray-plane intersection for 3D cursor placement.
 *
 * Provides a utility for intersecting a ray with a horizontal (Y=constant)
 * plane, used by Ctrl+right-click to place the 3D cursor in the scene editor.
 *
 * Ownership: no allocations; all data passed by pointer or value.
 * Nullability: t_out and hit_point must be non-NULL.
 * Error semantics: returns false if the ray is parallel or pointing away.
 * Side effects: none (pure math).
 *
 * Public types: none (0 / 2-type rule).
 */
#ifndef FERRUM_EDITOR_SCENE_CURSOR_PLACE_H
#define FERRUM_EDITOR_SCENE_CURSOR_PLACE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "ferrum/math/vec3.h"

/**
 * @brief Intersect a ray with a horizontal plane at Y = plane_y.
 *
 * Computes the intersection point of a ray (origin + t * direction)
 * with the plane Y = plane_y.  Returns false if the ray is parallel
 * to the plane (|direction.y| < epsilon) or if the intersection
 * is behind the ray origin (t <= 0).
 *
 * @param origin     Ray origin in world space.
 * @param direction  Ray direction (should be normalized, but not required).
 * @param plane_y    Y coordinate of the horizontal plane.
 * @param t_out      Output: parameter t at intersection (non-NULL).
 * @param hit_point  Output: world-space intersection point (non-NULL).
 * @return true if a valid forward intersection was found.
 */
bool cursor_ray_plane_intersect(vec3_t origin, vec3_t direction,
                                 float plane_y, float *t_out,
                                 vec3_t *hit_point);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_CURSOR_PLACE_H */
