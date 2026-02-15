/**
 * @file narrowphase_capsule_tri.c
 * @brief Capsule vs triangle narrowphase intersection test.
 *
 * Finds the closest point pair between the capsule line segment
 * and the triangle, then performs a sphere-like overlap check.
 */

#include <math.h>
#include <stddef.h>

#include "ferrum/math/vec3.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/mesh_collider.h"
#include "ferrum/physics/mesh_narrowphase.h"
#include "ferrum/physics/phys_quat.h"

/** Rotate vector by quaternion: q * v * q^-1. */
static phys_vec3_t quat_rotate_vec3(phys_quat_t q, phys_vec3_t v) {
    phys_vec3_t u = {q.x, q.y, q.z};
    float s = q.w;
    phys_vec3_t t = vec3_scale(vec3_cross(u, v), 2.0f);
    return vec3_add(v, vec3_add(vec3_scale(t, s), vec3_cross(u, t)));
}

/* ── Closest point on segment to point ─────────────────────────── */

static phys_vec3_t closest_on_segment(phys_vec3_t a, phys_vec3_t b,
                                       phys_vec3_t p) {
    phys_vec3_t ab = vec3_sub(b, a);
    float t = vec3_dot(vec3_sub(p, a), ab);
    float denom = vec3_dot(ab, ab);
    if (denom < 1e-12f) return a;
    t /= denom;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return vec3_add(a, vec3_scale(ab, t));
}

/* ── Closest point on triangle (same as in sphere_tri) ─────────── */

static phys_vec3_t closest_on_triangle(phys_vec3_t p,
                                        phys_vec3_t a,
                                        phys_vec3_t b,
                                        phys_vec3_t c) {
    phys_vec3_t ab = vec3_sub(b, a);
    phys_vec3_t ac = vec3_sub(c, a);
    phys_vec3_t ap = vec3_sub(p, a);

    float d1 = vec3_dot(ab, ap);
    float d2 = vec3_dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return a;

    phys_vec3_t bp = vec3_sub(p, b);
    float d3 = vec3_dot(ab, bp);
    float d4 = vec3_dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return b;

    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        float v = d1 / (d1 - d3);
        return vec3_add(a, vec3_scale(ab, v));
    }

    phys_vec3_t cp_v = vec3_sub(p, c);
    float d5 = vec3_dot(ab, cp_v);
    float d6 = vec3_dot(ac, cp_v);
    if (d6 >= 0.0f && d5 <= d6) return c;

    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        float w = d2 / (d2 - d6);
        return vec3_add(a, vec3_scale(ac, w));
    }

    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return vec3_add(b, vec3_scale(vec3_sub(c, b), w));
    }

    float denom = 1.0f / (va + vb + vc);
    float v = vb * denom;
    float w = vc * denom;
    return vec3_add(a, vec3_add(vec3_scale(ab, v), vec3_scale(ac, w)));
}

/* ── Capsule vs Triangle ───────────────────────────────────────── */

bool phys_capsule_vs_triangle(
    phys_vec3_t cap_center, phys_quat_t cap_rotation,
    float cap_radius, float cap_half_height,
    const phys_triangle_t *tri,
    float spec_margin,
    phys_contact_point_t *contact_out)
{
    if (!tri || !contact_out) return false;

    /* Capsule segment endpoints. */
    phys_vec3_t up = quat_rotate_vec3(cap_rotation, (phys_vec3_t){0, 1, 0});
    phys_vec3_t seg_a = vec3_sub(cap_center, vec3_scale(up, cap_half_height));
    phys_vec3_t seg_b = vec3_add(cap_center, vec3_scale(up, cap_half_height));

    /* Find closest point on the capsule segment to the triangle.
     * We iterate: closest-on-tri to segment point, then closest-on-segment
     * to that, converging in 2 iterations for good approximation. */
    phys_vec3_t seg_pt = cap_center;
    phys_vec3_t tri_pt;
    for (int iter = 0; iter < 3; iter++) {
        tri_pt = closest_on_triangle(seg_pt,
                                      tri->v[0], tri->v[1], tri->v[2]);
        seg_pt = closest_on_segment(seg_a, seg_b, tri_pt);
    }

    /* Final closest on triangle to the converged segment point. */
    tri_pt = closest_on_triangle(seg_pt, tri->v[0], tri->v[1], tri->v[2]);

    phys_vec3_t diff = vec3_sub(seg_pt, tri_pt);
    float dist_sq = vec3_dot(diff, diff);
    float dist = sqrtf(dist_sq);

    /* Compute triangle face normal. */
    phys_vec3_t e0 = vec3_sub(tri->v[1], tri->v[0]);
    phys_vec3_t e1 = vec3_sub(tri->v[2], tri->v[0]);
    phys_vec3_t tri_normal = vec3_cross(e0, e1);
    float tri_len = sqrtf(vec3_dot(tri_normal, tri_normal));
    if (tri_len > 1e-9f) {
        tri_normal = vec3_scale(tri_normal, 1.0f / tri_len);
    } else {
        tri_normal = (phys_vec3_t){0, 1, 0};
    }

    /* Check which side of the triangle the capsule segment point is on.
     * Negative dot means the segment point is on the backface side
     * (behind the triangle), indicating the capsule has penetrated
     * through the surface — treat the mesh as solid by pushing the
     * capsule back toward the front face. */
    float side = vec3_dot(vec3_sub(seg_pt, tri->v[0]), tri_normal);
    bool backface = (side < 0.0f);

    if (backface) {
        /* Capsule has penetrated through the triangle surface.
         * Push it back toward the front face (flip the normal).
         * Penetration depth = how far through the capsule surface is. */
        contact_out->normal = vec3_scale(tri_normal, -1.0f);
        contact_out->penetration = cap_radius + fabsf(side);
        contact_out->point_world = tri_pt;
        contact_out->feature_id = 0;
        return true;
    }

    /* Front-face: standard closest-point overlap test. */
    float threshold = cap_radius + spec_margin;
    if (dist_sq > threshold * threshold) return false;

    if (dist < 1e-6f) {
        /* Segment point is on the triangle surface. */
        contact_out->normal = tri_normal;
        contact_out->penetration = cap_radius;
    } else {
        contact_out->normal = vec3_scale(diff, 1.0f / dist);
        contact_out->penetration = cap_radius - dist;
    }

    contact_out->point_world = tri_pt;
    contact_out->feature_id = 0;
    return true;
}
