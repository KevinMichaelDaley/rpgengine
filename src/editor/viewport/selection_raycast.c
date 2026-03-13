/**
 * @file selection_raycast.c
 * @brief Ray-AABB, ray-sphere intersection tests and nearest-entity picking.
 *
 * Non-static functions: 3 (ray_intersect_aabb, ray_intersect_sphere,
 *                          pick_nearest_entity).
 */

#include "ferrum/editor/viewport/selection_raycast.h"
#include "ferrum/editor/viewport/viewport_camera.h"
#include <math.h>

bool ray_intersect_aabb(const struct editor_ray *ray,
                         vec3_t aabb_min, vec3_t aabb_max, float *t_hit) {
    /* Slab method for ray-AABB intersection. */
    float tmin = -1e30f;
    float tmax = 1e30f;

    const float *origin = &ray->origin.x;
    const float *dir = &ray->direction.x;
    const float *bmin = &aabb_min.x;
    const float *bmax = &aabb_max.x;

    for (int i = 0; i < 3; i++) {
        if (fabsf(dir[i]) < 1e-8f) {
            /* Ray parallel to slab. Miss if origin outside. */
            if (origin[i] < bmin[i] || origin[i] > bmax[i]) {
                return false;
            }
        } else {
            float inv_d = 1.0f / dir[i];
            float t1 = (bmin[i] - origin[i]) * inv_d;
            float t2 = (bmax[i] - origin[i]) * inv_d;

            if (t1 > t2) {
                float tmp = t1;
                t1 = t2;
                t2 = tmp;
            }

            if (t1 > tmin) tmin = t1;
            if (t2 < tmax) tmax = t2;

            if (tmin > tmax) return false;
        }
    }

    /* If tmax < 0, the AABB is behind the ray. */
    if (tmax < 0.0f) return false;

    /* If tmin < 0, origin is inside the AABB. */
    *t_hit = (tmin >= 0.0f) ? tmin : 0.0f;
    return true;
}

bool ray_intersect_sphere(const struct editor_ray *ray,
                           vec3_t center, float radius, float *t_hit) {
    /* Geometric ray-sphere intersection. */
    vec3_t oc = vec3_sub(ray->origin, center);
    float a = vec3_dot(ray->direction, ray->direction);
    float b = 2.0f * vec3_dot(oc, ray->direction);
    float c = vec3_dot(oc, oc) - radius * radius;
    float discriminant = b * b - 4.0f * a * c;

    if (discriminant < 0.0f) return false;

    float sqrt_disc = sqrtf(discriminant);
    float t0 = (-b - sqrt_disc) / (2.0f * a);
    float t1 = (-b + sqrt_disc) / (2.0f * a);

    if (t1 < 0.0f) return false;

    *t_hit = (t0 >= 0.0f) ? t0 : t1;
    return true;
}

bool pick_nearest_entity(const struct editor_ray *ray,
                          const pick_candidate_t *candidates, uint32_t count,
                          uint32_t *out_id) {
    if (!ray || count == 0 || !candidates || !out_id) return false;

    float closest_t = 1e30f;
    bool found = false;

    for (uint32_t i = 0; i < count; i++) {
        float t;
        if (ray_intersect_aabb(ray, candidates[i].aabb_min,
                                candidates[i].aabb_max, &t)) {
            if (t < closest_t) {
                closest_t = t;
                *out_id = candidates[i].entity_id;
                found = true;
            }
        }
    }

    return found;
}
