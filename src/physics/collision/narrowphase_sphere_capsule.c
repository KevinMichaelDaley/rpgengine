/**
 * @file narrowphase_sphere_capsule.c
 * @brief Sphere-capsule narrowphase intersection test.
 *
 * Reduces to sphere-sphere by finding the closest point on the
 * capsule's line segment to the sphere center.
 */

#include <math.h>

#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/narrowphase.h"

/* ── Static helpers ─────────────────────────────────────────────── */

/**
 * Clamp a float to [lo, hi].
 */
static float clampf(float val, float lo, float hi)
{
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

/* ── Public API ─────────────────────────────────────────────────── */

bool phys_sphere_vs_capsule(
    phys_vec3_t sphere_center, float sphere_radius,
    phys_vec3_t capsule_center, phys_quat_t capsule_rotation,
    float capsule_radius, float capsule_half_height,
    float speculative_margin,
    phys_contact_point_t *contact_out)
{
    if (!contact_out) {
        return false;
    }

    /* Step 1: Compute capsule segment endpoints in world space. */
    phys_vec3_t local_axis = {0.0f, 1.0f, 0.0f};
    phys_vec3_t world_axis = quat_rotate_vec3(capsule_rotation, local_axis);
    phys_vec3_t half_seg = vec3_scale(world_axis, capsule_half_height);
    phys_vec3_t p0 = vec3_sub(capsule_center, half_seg);
    phys_vec3_t p1 = vec3_add(capsule_center, half_seg);

    /* Step 2: Find closest point on segment [p0, p1] to sphere_center. */
    phys_vec3_t d = vec3_sub(p1, p0);
    float dd = vec3_dot(d, d);
    float t = 0.5f; /* default: midpoint if segment is degenerate */
    if (dd > 1e-8f) {
        t = vec3_dot(vec3_sub(sphere_center, p0), d) / dd;
        t = clampf(t, 0.0f, 1.0f);
    }
    phys_vec3_t closest = vec3_add(p0, vec3_scale(d, t));

    /* Step 3: Sphere-sphere test between sphere and closest/capsule_radius. */
    phys_vec3_t diff = vec3_sub(sphere_center, closest);
    float dist_sq = vec3_dot(diff, diff);
    float r_sum = sphere_radius + capsule_radius;
    float threshold = r_sum + speculative_margin;

    if (dist_sq > threshold * threshold) {
        return false;
    }

    float dist = sqrtf(dist_sq);

    if (dist < 1e-4f) {
        /* Sphere center on capsule segment — arbitrary up normal. */
        contact_out->normal = (phys_vec3_t){0.0f, 1.0f, 0.0f};
        contact_out->penetration = r_sum;
    } else {
        /* Normal from closest point toward sphere center.
         * Negative penetration = separated (speculative). */
        contact_out->normal = vec3_scale(diff, 1.0f / dist);
        contact_out->penetration = r_sum - dist;
    }

    /* Contact point: on the capsule surface toward the sphere. */
    contact_out->point_world = vec3_add(
        closest,
        vec3_scale(contact_out->normal,
                   capsule_radius - contact_out->penetration * 0.5f));

    contact_out->feature_id = 0;

    return true;
}
