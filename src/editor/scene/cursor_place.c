/**
 * @file cursor_place.c
 * @brief Ray-plane intersection for 3D cursor placement.
 *
 * Non-static functions (1 / 4 limit):
 *   cursor_ray_plane_intersect
 */

#include "ferrum/editor/scene/cursor_place.h"
#include <math.h>

/** Minimum absolute direction.y to consider the ray non-parallel. */
#define PLANE_EPSILON 1e-6f

bool cursor_ray_plane_intersect(vec3_t origin, vec3_t direction,
                                 float plane_y, float *t_out,
                                 vec3_t *hit_point) {
    /* The plane equation is: y = plane_y.
     * Substituting the ray: origin.y + t * direction.y = plane_y
     * Solving: t = (plane_y - origin.y) / direction.y */
    float denom = direction.y;
    if (fabsf(denom) < PLANE_EPSILON) {
        /* Ray is parallel to the plane. */
        return false;
    }

    float t = (plane_y - origin.y) / denom;
    if (t <= 0.0f) {
        /* Intersection is behind the ray origin. */
        return false;
    }

    *t_out = t;
    hit_point->x = origin.x + direction.x * t;
    hit_point->y = plane_y;
    hit_point->z = origin.z + direction.z * t;
    return true;
}
