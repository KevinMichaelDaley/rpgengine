/**
 * @file narrowphase_capsule_capsule.c
 * @brief Capsule vs capsule narrowphase intersection test.
 *
 * Algorithm:
 * 1. Compute world-space segment endpoints for each capsule.
 * 2. Find closest points between the two line segments.
 * 3. Reduce to sphere-sphere test at those closest points.
 */

#include <math.h>

#include "ferrum/physics/collision/capsule_capsule.h"
#include "ferrum/math/common.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"
#include "ferrum/physics/manifold.h"

/* ── Static helpers ─────────────────────────────────────────────── */

/**
 * @brief Rotate a vector by a quaternion: q * v * q^-1.
/* ── Public API ─────────────────────────────────────────────────── */

bool phys_capsule_vs_capsule(
    phys_vec3_t center_a, phys_quat_t rotation_a,
    float radius_a, float half_height_a,
    phys_vec3_t center_b, phys_quat_t rotation_b,
    float radius_b, float half_height_b,
    float speculative_margin,
    phys_contact_point_t *contact_out)
{
    if (!contact_out) {
        return false;
    }

    const float epsilon = 1e-6f;

    /* Capsule axis is local +Y = (0,1,0). Rotate into world space. */
    phys_vec3_t up = {0.0f, 1.0f, 0.0f};
    phys_vec3_t axis_a = quat_rotate_vec3(rotation_a, up);
    phys_vec3_t axis_b = quat_rotate_vec3(rotation_b, up);

    /* Segment endpoints for capsule A. */
    phys_vec3_t a0 = vec3_sub(center_a, vec3_scale(axis_a, half_height_a));
    phys_vec3_t a1 = vec3_add(center_a, vec3_scale(axis_a, half_height_a));

    /* Segment endpoints for capsule B. */
    phys_vec3_t b0 = vec3_sub(center_b, vec3_scale(axis_b, half_height_b));
    phys_vec3_t b1 = vec3_add(center_b, vec3_scale(axis_b, half_height_b));

    /* Direction vectors for each segment. */
    phys_vec3_t d1 = vec3_sub(a1, a0);
    phys_vec3_t d2 = vec3_sub(b1, b0);
    phys_vec3_t r  = vec3_sub(a0, b0);

    float a = vec3_dot(d1, d1);  /* Squared length of segment A. */
    float e = vec3_dot(d2, d2);  /* Squared length of segment B. */
    float f = vec3_dot(d2, r);

    float s = 0.0f;
    float t = 0.0f;

    if (a <= epsilon && e <= epsilon) {
        /* Both capsules are degenerate (zero height) → point vs point. */
        s = 0.0f;
        t = 0.0f;
    } else if (a <= epsilon) {
        /* Capsule A is degenerate → clamp to a0, project onto B. */
        s = 0.0f;
        t = fr_clampf(f / e, 0.0f, 1.0f);
    } else {
        float c = vec3_dot(d1, r);
        if (e <= epsilon) {
            /* Capsule B is degenerate → clamp to b0, project onto A. */
            t = 0.0f;
            s = fr_clampf(-c / a, 0.0f, 1.0f);
        } else {
            /* General non-degenerate case. */
            float b_coeff = vec3_dot(d1, d2);
            float denom = a * e - b_coeff * b_coeff;

            if (denom > epsilon) {
                s = fr_clampf((b_coeff * f - c * e) / denom, 0.0f, 1.0f);
            } else {
                /* Nearly parallel segments — use midpoint of A. */
                s = 0.5f;
            }

            /* Compute t from s. */
            t = (b_coeff * s + f) / e;

            /* Clamp t, then re-derive s if t was clamped. */
            if (t < 0.0f) {
                t = 0.0f;
                s = fr_clampf(-c / a, 0.0f, 1.0f);
            } else if (t > 1.0f) {
                t = 1.0f;
                s = fr_clampf((b_coeff - c) / a, 0.0f, 1.0f);
            }
        }
    }

    /* Closest points on each segment. */
    phys_vec3_t closest_a = vec3_add(a0, vec3_scale(d1, s));
    phys_vec3_t closest_b = vec3_add(b0, vec3_scale(d2, t));

    /* Sphere-sphere test at the closest points. */
    phys_vec3_t diff = vec3_sub(closest_b, closest_a);
    float dist_sq = vec3_dot(diff, diff);
    float combined_radius = radius_a + radius_b;
    float threshold = combined_radius + speculative_margin;

    if (dist_sq > threshold * threshold) {
        return false;
    }

    float dist = sqrtf(dist_sq);

    if (dist < 1e-4f) {
        /* Closest points coincide — use fallback normal. */
        contact_out->normal = (phys_vec3_t){0.0f, 1.0f, 0.0f};
        contact_out->penetration = combined_radius;
    } else {
        /* Normal from A toward B. */
        contact_out->normal = vec3_scale(diff, 1.0f / dist);
        contact_out->penetration = combined_radius - dist;
    }

    /* Contact point on the surface of capsule A. */
    contact_out->point_world = vec3_add(
        closest_a,
        vec3_scale(contact_out->normal, radius_a));

    /* No geometric feature tracking for capsule-capsule. */
    contact_out->feature_id = 0;

    return true;
}
