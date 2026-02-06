/**
 * @file narrowphase_box_capsule.c
 * @brief Box vs Capsule narrowphase intersection test.
 *
 * Algorithm:
 *  1. Compute capsule segment endpoints in world space.
 *  2. Transform endpoints to box local space.
 *  3. Iteratively find closest points between segment and AABB.
 *  4. Compare distance to capsule radius for contact.
 */

#include <math.h>
#include <stdbool.h>
#include <stddef.h>

#include "ferrum/physics/collision/box_capsule.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"
#include "ferrum/physics/manifold.h"

/* ── Static helpers ─────────────────────────────────────────────── */

/** Rotate a vector by a quaternion: q * v * q^-1. */
static phys_vec3_t quat_rotate_vec3(phys_quat_t q, phys_vec3_t v)
{
    phys_vec3_t u = {q.x, q.y, q.z};
    float w = q.w;
    phys_vec3_t uv = vec3_cross(u, v);
    phys_vec3_t uuv = vec3_cross(u, uv);
    uv = vec3_scale(uv, 2.0f * w);
    uuv = vec3_scale(uuv, 2.0f);
    return vec3_add(v, vec3_add(uv, uuv));
}

/** Clamp a vector component-wise to the box half-extents. */
static phys_vec3_t clamp_to_box(phys_vec3_t p, phys_vec3_t half_ext)
{
    phys_vec3_t result;
    result.x = fmaxf(-half_ext.x, fminf(p.x, half_ext.x));
    result.y = fmaxf(-half_ext.y, fminf(p.y, half_ext.y));
    result.z = fmaxf(-half_ext.z, fminf(p.z, half_ext.z));
    return result;
}

/**
 * Find the closest point on segment [a, b] to target point p.
 * Returns the closest point (not the parameter t).
 */
static phys_vec3_t closest_point_on_segment(phys_vec3_t a, phys_vec3_t b,
                                             phys_vec3_t p)
{
    phys_vec3_t ab = vec3_sub(b, a);
    phys_vec3_t ap = vec3_sub(p, a);
    float ab_dot_ab = vec3_dot(ab, ab);

    /* Degenerate segment (a == b). */
    if (ab_dot_ab < 1e-8f) {
        return a;
    }

    float t = vec3_dot(ap, ab) / ab_dot_ab;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    return vec3_add(a, vec3_scale(ab, t));
}

/**
 * Determine the face normal of the closest box face to a point
 * that is inside the box (or nearly so). Used when distance ≈ 0.
 */
static phys_vec3_t box_face_normal_for_interior(phys_vec3_t local_pt,
                                                 phys_vec3_t half_ext)
{
    /* Find which face the point is closest to. */
    float min_dist = half_ext.x - fabsf(local_pt.x);
    phys_vec3_t normal = {(local_pt.x >= 0.0f) ? 1.0f : -1.0f, 0.0f, 0.0f};

    float dy = half_ext.y - fabsf(local_pt.y);
    if (dy < min_dist) {
        min_dist = dy;
        normal = (phys_vec3_t){0.0f, (local_pt.y >= 0.0f) ? 1.0f : -1.0f, 0.0f};
    }

    float dz = half_ext.z - fabsf(local_pt.z);
    if (dz < min_dist) {
        normal = (phys_vec3_t){0.0f, 0.0f, (local_pt.z >= 0.0f) ? 1.0f : -1.0f};
    }

    return normal;
}

/* ── Public API ─────────────────────────────────────────────────── */

bool phys_box_vs_capsule(
    phys_vec3_t box_center, phys_quat_t box_rotation, phys_vec3_t box_half_extents,
    phys_vec3_t capsule_center, phys_quat_t capsule_rotation,
    float capsule_radius, float capsule_half_height,
    phys_contact_point_t *contact_out)
{
    if (!contact_out) {
        return false;
    }

    /* ── Step 1: Capsule segment endpoints in world space ──────── */
    phys_vec3_t world_axis = quat_rotate_vec3(capsule_rotation,
                                               (phys_vec3_t){0.0f, 1.0f, 0.0f});
    phys_vec3_t seg_p0 = vec3_sub(capsule_center,
                                   vec3_scale(world_axis, capsule_half_height));
    phys_vec3_t seg_p1 = vec3_add(capsule_center,
                                   vec3_scale(world_axis, capsule_half_height));

    /* ── Step 2: Transform endpoints to box local space ────────── */
    phys_quat_t q_inv = quat_conjugate(box_rotation);
    phys_vec3_t local_p0 = quat_rotate_vec3(q_inv,
                                             vec3_sub(seg_p0, box_center));
    phys_vec3_t local_p1 = quat_rotate_vec3(q_inv,
                                             vec3_sub(seg_p1, box_center));

    /* ── Step 3: Iterative closest-point refinement ────────────── */
    /* Start: closest point on segment to box center (origin). */
    phys_vec3_t closest_on_seg = closest_point_on_segment(
        local_p0, local_p1, (phys_vec3_t){0.0f, 0.0f, 0.0f});

    /* Clamp that segment point to the box → closest on box. */
    phys_vec3_t closest_on_box = clamp_to_box(closest_on_seg,
                                               box_half_extents);

    /* Refine: find closest on segment to the box point we found. */
    closest_on_seg = closest_point_on_segment(local_p0, local_p1,
                                               closest_on_box);

    /* Re-clamp to box for convergence. */
    closest_on_box = clamp_to_box(closest_on_seg, box_half_extents);

    /* One more iteration for better accuracy. */
    closest_on_seg = closest_point_on_segment(local_p0, local_p1,
                                               closest_on_box);
    closest_on_box = clamp_to_box(closest_on_seg, box_half_extents);

    /* ── Step 4: Distance check ────────────────────────────────── */
    phys_vec3_t diff = vec3_sub(closest_on_seg, closest_on_box);
    float dist_sq = vec3_dot(diff, diff);

    if (dist_sq > capsule_radius * capsule_radius) {
        return false;
    }

    float dist = sqrtf(dist_sq);

    /* ── Step 5: Contact normal (in box local space) ───────────── */
    phys_vec3_t local_normal;
    if (dist < 1e-4f) {
        /* Segment point is inside or on the box surface.
         * Use the closest box face normal. */
        local_normal = box_face_normal_for_interior(closest_on_seg,
                                                     box_half_extents);
    } else {
        /* Normal points from box surface toward capsule segment. */
        local_normal = vec3_scale(diff, 1.0f / dist);
    }

    /* ── Step 6: Transform normal to world space ───────────────── */
    phys_vec3_t world_normal = quat_rotate_vec3(box_rotation, local_normal);

    /* ── Step 7: Penetration depth ─────────────────────────────── */
    float penetration = capsule_radius - dist;

    /* ── Step 8: Contact point (midpoint of overlap region) ────── */
    /* In box local space: point on box surface + offset along normal. */
    phys_vec3_t local_contact = vec3_add(
        closest_on_box,
        vec3_scale(local_normal, penetration * 0.5f));
    phys_vec3_t world_contact = vec3_add(
        box_center,
        quat_rotate_vec3(box_rotation, local_contact));

    /* ── Fill output ───────────────────────────────────────────── */
    contact_out->normal = world_normal;
    contact_out->penetration = penetration;
    contact_out->point_world = world_contact;
    contact_out->feature_id = 0;
    contact_out->local_a = local_contact;
    contact_out->local_b = quat_rotate_vec3(
        quat_conjugate(capsule_rotation),
        vec3_sub(world_contact, capsule_center));

    return true;
}
