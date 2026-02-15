/**
 * @file narrowphase_box_tri.c
 * @brief Box vs triangle narrowphase intersection test.
 *
 * Uses SAT (Separating Axis Theorem) between an oriented box and a triangle.
 * Tests 13 potential separating axes:
 *   - Triangle normal (1)
 *   - Box face normals (3)
 *   - Cross products of box edges × triangle edges (9)
 *
 * If no separating axis is found, generates a contact point at the
 * deepest penetration.
 */

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "ferrum/math/vec3.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/mesh_collider.h"
#include "ferrum/physics/mesh_narrowphase.h"
#include "ferrum/physics/phys_quat.h"

/* ── Helpers ────────────────────────────────────────────────────── */

/** Rotate vector by quaternion: q * v * q^-1. */
static phys_vec3_t quat_rotate_vec3(phys_quat_t q, phys_vec3_t v) {
    phys_vec3_t u = {q.x, q.y, q.z};
    float s = q.w;
    phys_vec3_t t = vec3_scale(vec3_cross(u, v), 2.0f);
    return vec3_add(v, vec3_add(vec3_scale(t, s), vec3_cross(u, t)));
}

/** Project triangle vertices onto axis, return [min, max]. */
static void tri_project(phys_vec3_t a, phys_vec3_t b, phys_vec3_t c,
                         phys_vec3_t axis, float *out_min, float *out_max) {
    float da = vec3_dot(a, axis);
    float db = vec3_dot(b, axis);
    float dc = vec3_dot(c, axis);
    *out_min = da < db ? (da < dc ? da : dc) : (db < dc ? db : dc);
    *out_max = da > db ? (da > dc ? da : dc) : (db > dc ? db : dc);
}

/** Project OBB onto axis, return half-extent (symmetric about center proj). */
static float box_half_proj(phys_vec3_t he, phys_vec3_t axes[3],
                            phys_vec3_t axis) {
    return fabsf(vec3_dot(axes[0], axis)) * he.x +
           fabsf(vec3_dot(axes[1], axis)) * he.y +
           fabsf(vec3_dot(axes[2], axis)) * he.z;
}

/** Test one SAT axis. Returns overlap depth (negative = separated). */
static float test_axis(phys_vec3_t axis,
                        phys_vec3_t box_center, phys_vec3_t he,
                        phys_vec3_t box_axes[3],
                        phys_vec3_t v0, phys_vec3_t v1, phys_vec3_t v2,
                        float spec_margin) {
    float axis_len = sqrtf(vec3_dot(axis, axis));
    if (axis_len < 1e-8f) return 1e30f; /* Degenerate axis — skip. */
    axis = vec3_scale(axis, 1.0f / axis_len);

    float box_c = vec3_dot(box_center, axis);
    float box_r = box_half_proj(he, box_axes, axis);

    float tri_min, tri_max;
    tri_project(v0, v1, v2, axis, &tri_min, &tri_max);

    float gap = 0.0f;
    if (box_c + box_r < tri_min) {
        gap = tri_min - (box_c + box_r);
    } else if (box_c - box_r > tri_max) {
        gap = (box_c - box_r) - tri_max;
    } else {
        /* Overlapping: compute overlap depth. */
        float d1 = (box_c + box_r) - tri_min;
        float d2 = tri_max - (box_c - box_r);
        return d1 < d2 ? d1 : d2;
    }

    if (gap > spec_margin) return -gap; /* Separated. */
    return -gap; /* Within speculative margin. */
}

/* ── Box vs Triangle ───────────────────────────────────────────── */

int phys_box_vs_triangle(
    phys_vec3_t box_center, phys_quat_t box_rotation,
    phys_vec3_t box_half_extents,
    const phys_triangle_t *tri,
    float spec_margin,
    phys_contact_point_t *contacts_out,
    int max_contacts)
{
    if (!tri || !contacts_out || max_contacts <= 0) return 0;

    /* Box local axes in world space. */
    phys_vec3_t box_axes[3];
    box_axes[0] = quat_rotate_vec3(box_rotation, (phys_vec3_t){1, 0, 0});
    box_axes[1] = quat_rotate_vec3(box_rotation, (phys_vec3_t){0, 1, 0});
    box_axes[2] = quat_rotate_vec3(box_rotation, (phys_vec3_t){0, 0, 1});

    phys_vec3_t v0 = tri->v[0], v1 = tri->v[1], v2 = tri->v[2];

    /* Triangle edges. */
    phys_vec3_t e0 = vec3_sub(v1, v0);
    phys_vec3_t e1 = vec3_sub(v2, v1);
    phys_vec3_t e2 = vec3_sub(v0, v2);

    /* Triangle normal. */
    phys_vec3_t tri_normal = vec3_cross(e0, vec3_sub(v2, v0));
    float tri_normal_len = sqrtf(vec3_dot(tri_normal, tri_normal));
    if (tri_normal_len < 1e-9f) return 0; /* Degenerate triangle. */
    tri_normal = vec3_scale(tri_normal, 1.0f / tri_normal_len);

    /* Test all 13 SAT axes, track minimum overlap. */
    float min_depth = 1e30f;
    phys_vec3_t min_axis = tri_normal;

    phys_vec3_t axes[13];
    axes[0] = tri_normal;
    axes[1] = box_axes[0];
    axes[2] = box_axes[1];
    axes[3] = box_axes[2];

    phys_vec3_t tri_edges[3] = {e0, e1, e2};
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            axes[4 + i * 3 + j] = vec3_cross(box_axes[i], tri_edges[j]);
        }
    }

    for (int i = 0; i < 13; i++) {
        float depth = test_axis(axes[i], box_center, box_half_extents,
                                 box_axes, v0, v1, v2, spec_margin);
        if (depth < -spec_margin) return 0; /* Separated beyond margin. */
        if (depth < min_depth) {
            min_depth = depth;
            float len = sqrtf(vec3_dot(axes[i], axes[i]));
            if (len > 1e-8f) {
                min_axis = vec3_scale(axes[i], 1.0f / len);
            }
        }
    }

    /* Ensure normal points from triangle toward box center. */
    if (vec3_dot(min_axis, vec3_sub(box_center, v0)) < 0.0f) {
        min_axis = vec3_scale(min_axis, -1.0f);
    }

    /* Generate a single contact at the box's deepest vertex. */
    phys_vec3_t signs[8] = {
        { 1, 1, 1}, { 1, 1,-1}, { 1,-1, 1}, { 1,-1,-1},
        {-1, 1, 1}, {-1, 1,-1}, {-1,-1, 1}, {-1,-1,-1},
    };

    float deepest_proj = 1e30f;
    phys_vec3_t deepest_pt = box_center;
    for (int i = 0; i < 8; i++) {
        phys_vec3_t corner = vec3_add(box_center,
            vec3_add(vec3_scale(box_axes[0], signs[i].x * box_half_extents.x),
            vec3_add(vec3_scale(box_axes[1], signs[i].y * box_half_extents.y),
                     vec3_scale(box_axes[2], signs[i].z * box_half_extents.z))));
        float proj = vec3_dot(corner, min_axis);
        if (proj < deepest_proj) {
            deepest_proj = proj;
            deepest_pt = corner;
        }
    }

    contacts_out[0].point_world = deepest_pt;
    contacts_out[0].normal = min_axis;
    contacts_out[0].penetration = min_depth;
    contacts_out[0].feature_id = 0;
    return 1;
}
