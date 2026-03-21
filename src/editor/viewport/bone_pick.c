/**
 * @file bone_pick.c
 * @brief Ray-capsule intersection and bone picking.
 *
 * Non-static functions (2 / 4 limit):
 *   ray_intersect_capsule
 *   pick_nearest_bone
 */

#include "ferrum/editor/viewport/bone_pick.h"
#include "ferrum/editor/viewport/viewport_camera.h"
#include "ferrum/math/vec3.h"

#include <math.h>

/**
 * @brief Ray-sphere intersection helper.
 *
 * Tests if a ray intersects a sphere at center with the given radius.
 * Returns the nearest positive t, or false if no hit in front of ray.
 */
static bool ray_sphere_(const editor_ray_t *ray, vec3_t center,
                         float radius, float *t_hit) {
    vec3_t oc = vec3_sub(ray->origin, center);
    float a = vec3_dot(ray->direction, ray->direction);
    float b = 2.0f * vec3_dot(oc, ray->direction);
    float c = vec3_dot(oc, oc) - radius * radius;
    float disc = b * b - 4.0f * a * c;

    if (disc < 0.0f) return false;

    float sqrt_disc = sqrtf(disc);
    float inv_2a = 1.0f / (2.0f * a);
    float t0 = (-b - sqrt_disc) * inv_2a;
    float t1 = (-b + sqrt_disc) * inv_2a;

    if (t1 < 0.0f) return false;

    *t_hit = (t0 >= 0.0f) ? t0 : t1;
    return true;
}

bool ray_intersect_capsule(const struct editor_ray *ray,
                            vec3_t cap_a, vec3_t cap_b,
                            float radius, float *t_hit) {
    if (!ray || !t_hit) return false;

    /* Capsule axis vector and length squared. */
    vec3_t ab = vec3_sub(cap_b, cap_a);
    float ab_dot = vec3_dot(ab, ab);

    /* Degenerate capsule (zero length) → sphere test. */
    if (ab_dot < 1e-12f) {
        return ray_sphere_(ray, cap_a, radius, t_hit);
    }

    /* Test the infinite cylinder around the segment ab with given radius.
     *
     * Parametric ray: P(t) = O + t*D
     * Project onto plane perpendicular to ab:
     *   Let d = D - (D·ab/ab·ab)*ab   (ray direction projected)
     *   Let oc = (O-A) - ((O-A)·ab/ab·ab)*ab  (ray origin offset projected)
     *
     * Then solve: |oc + t*d|² = r²
     *   → d·d * t² + 2*(oc·d)*t + oc·oc - r² = 0
     */
    vec3_t oa = vec3_sub(ray->origin, cap_a);

    float d_dot_ab = vec3_dot(ray->direction, ab);
    float oa_dot_ab = vec3_dot(oa, ab);
    float inv_ab2 = 1.0f / ab_dot;

    /* Projected ray direction: D - (D·ab/ab·ab)*ab. */
    vec3_t d_proj = vec3_sub(ray->direction,
                              vec3_scale(ab, d_dot_ab * inv_ab2));
    /* Projected origin offset: oa - (oa·ab/ab·ab)*ab. */
    vec3_t oc_proj = vec3_sub(oa, vec3_scale(ab, oa_dot_ab * inv_ab2));

    float a = vec3_dot(d_proj, d_proj);
    float b = 2.0f * vec3_dot(oc_proj, d_proj);
    float c = vec3_dot(oc_proj, oc_proj) - radius * radius;

    float best_t = 1e30f;
    bool found = false;

    /* Solve quadratic for cylinder. */
    if (a > 1e-12f) {
        float disc = b * b - 4.0f * a * c;
        if (disc >= 0.0f) {
            float sqrt_disc = sqrtf(disc);
            float inv_2a = 1.0f / (2.0f * a);
            float t0 = (-b - sqrt_disc) * inv_2a;
            float t1 = (-b + sqrt_disc) * inv_2a;

            /* Check both cylinder hits: must be in front of ray and
             * within the segment (project hit point onto ab axis). */
            for (int i = 0; i < 2; i++) {
                float t = (i == 0) ? t0 : t1;
                if (t < 0.0f) continue;
                /* Parameter along segment: s = ((O+t*D-A)·ab) / (ab·ab). */
                float s = (oa_dot_ab + t * d_dot_ab) * inv_ab2;
                if (s >= 0.0f && s <= 1.0f && t < best_t) {
                    best_t = t;
                    found = true;
                    break; /* t0 <= t1, so first valid hit is closest. */
                }
            }
        }
    }

    /* Test spherical cap at endpoint A. */
    float ta;
    if (ray_sphere_(ray, cap_a, radius, &ta) && ta < best_t) {
        /* Verify hit is on the cap side (not past the cylinder). */
        vec3_t hit_a = vec3_add(ray->origin, vec3_scale(ray->direction, ta));
        float sa = vec3_dot(vec3_sub(hit_a, cap_a), ab) * inv_ab2;
        if (sa <= 0.0f) {
            best_t = ta;
            found = true;
        }
    }

    /* Test spherical cap at endpoint B. */
    float tb;
    if (ray_sphere_(ray, cap_b, radius, &tb) && tb < best_t) {
        vec3_t hit_b = vec3_add(ray->origin, vec3_scale(ray->direction, tb));
        float sb = vec3_dot(vec3_sub(hit_b, cap_b), ab) * inv_ab2;
        if (sb >= 0.0f) {
            best_t = tb;
            found = true;
        }
    }

    if (found) {
        *t_hit = best_t;
    }
    return found;
}

bool pick_nearest_bone(const struct editor_ray *ray,
                        const bone_pick_candidate_t *candidates,
                        uint32_t count, uint32_t *out_bone) {
    if (!ray || count == 0 || !candidates || !out_bone) return false;

    float closest_t = 1e30f;
    bool found = false;

    for (uint32_t i = 0; i < count; i++) {
        float t;
        if (ray_intersect_capsule(ray, candidates[i].cap_a,
                                    candidates[i].cap_b,
                                    candidates[i].radius, &t)) {
            if (t < closest_t) {
                closest_t = t;
                *out_bone = candidates[i].bone_index;
                found = true;
            }
        }
    }

    return found;
}
