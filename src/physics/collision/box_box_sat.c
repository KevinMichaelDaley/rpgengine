/**
 * @file box_box_sat.c
 * @brief Box vs Box narrowphase using Separating Axis Theorem.
 *
 * Tests 15 separating axes (3 face-A, 3 face-B, 9 edge-edge cross
 * products) and generates contact points for the minimum penetration
 * axis.  Face contacts are clipped against reference face side planes;
 * edge contacts produce a single midpoint contact.
 */

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "ferrum/physics/collision/box_box.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"
#include "ferrum/physics/manifold.h"

/* ── Constants ──────────────────────────────────────────────────── */

/** Small epsilon to handle parallel edges in the rotation matrix. */
#define SAT_EPSILON 1e-6f

/** Relative bias applied to edge-edge penetration depths before comparing
 *  against face axes.  Face contacts produce higher-quality manifolds
 *  (multiple contact points vs. a single edge midpoint), so we prefer
 *  them when penetrations are nearly equal.  Without this bias, floating-
 *  point noise from SAT_EPSILON inflation in face-axis projections causes
 *  edge-edge axes to win spuriously for axis-aligned boxes. */
#define EDGE_BIAS 1.05f

/* ── Static helpers ─────────────────────────────────────────────── */

/**
 * @brief Extract the three local-space axes from a quaternion.
 *
 * Converts quaternion q into the three column vectors of the
 * corresponding rotation matrix: right (+X), up (+Y), forward (+Z).
 */
static void quat_to_axes(phys_quat_t q,
                          phys_vec3_t *right,
                          phys_vec3_t *up,
                          phys_vec3_t *forward)
{
    float x = q.x, y = q.y, z = q.z, w = q.w;
    float x2 = x + x, y2 = y + y, z2 = z + z;
    float xx = x * x2, xy = x * y2, xz = x * z2;
    float yy = y * y2, yz = y * z2, zz = z * z2;
    float wx = w * x2, wy = w * y2, wz = w * z2;

    *right   = (phys_vec3_t){1 - yy - zz, xy + wz, xz - wy};
    *up      = (phys_vec3_t){xy - wz, 1 - xx - zz, yz + wx};
    *forward = (phys_vec3_t){xz + wy, yz - wx, 1 - xx - yy};
}

/**
 * @brief Clip a polygon against a plane defined by normal direction
 *        and offset.
 *
 * Clips the polygon (in_pts, in_count) against the plane
 * dot(point, plane_normal) <= plane_offset.
 * Output is written to out_pts; returns the number of output vertices.
 * max_out limits the output size.
 */
static int clip_polygon(const phys_vec3_t *in_pts, int in_count,
                         phys_vec3_t plane_normal, float plane_offset,
                         phys_vec3_t *out_pts, int max_out)
{
    if (in_count < 1 || max_out < 1) return 0;

    int out_count = 0;

    for (int i = 0; i < in_count && out_count < max_out; i++) {
        int j = (i + 1) % in_count;
        float di = vec3_dot(in_pts[i], plane_normal) - plane_offset;
        float dj = vec3_dot(in_pts[j], plane_normal) - plane_offset;

        /* Point i is inside (or on) the plane. */
        if (di <= 0.0f) {
            out_pts[out_count++] = in_pts[i];
            if (out_count >= max_out) break;
        }

        /* Edge crosses the plane — compute intersection. */
        if ((di > 0.0f) != (dj > 0.0f)) {
            float t = di / (di - dj);
            phys_vec3_t edge = vec3_sub(in_pts[j], in_pts[i]);
            out_pts[out_count++] = vec3_add(in_pts[i], vec3_scale(edge, t));
            if (out_count >= max_out) break;
        }
    }

    return out_count;
}

/**
 * @brief Compute the closest points on two line segments.
 *
 * Given segments P0+s*D0 and P1+t*D1, computes the closest points
 * and returns them via out_a and out_b.
 */
static void closest_points_segments(
    phys_vec3_t p0, phys_vec3_t d0,
    phys_vec3_t p1, phys_vec3_t d1,
    phys_vec3_t *out_a, phys_vec3_t *out_b)
{
    phys_vec3_t r = vec3_sub(p0, p1);
    float a = vec3_dot(d0, d0);
    float e = vec3_dot(d1, d1);
    float f = vec3_dot(d1, r);
    float b = vec3_dot(d0, d1);
    float c = vec3_dot(d0, r);

    float denom = a * e - b * b;
    float s, t;

    if (denom < SAT_EPSILON) {
        /* Segments are nearly parallel. */
        s = 0.0f;
        t = f / (e > SAT_EPSILON ? e : 1.0f);
    } else {
        s = (b * f - c * e) / denom;
        t = (a * f - b * c) / denom;
    }

    /* Clamp to segment range [0, 1]. */
    if (s < 0.0f) s = 0.0f;
    if (s > 1.0f) s = 1.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    *out_a = vec3_add(p0, vec3_scale(d0, s));
    *out_b = vec3_add(p1, vec3_scale(d1, t));
}

/* ── Main SAT function ──────────────────────────────────────────── */

int phys_box_vs_box(
    phys_vec3_t center_a, phys_quat_t rotation_a, phys_vec3_t half_extents_a,
    phys_vec3_t center_b, phys_quat_t rotation_b, phys_vec3_t half_extents_b,
    phys_contact_point_t *contact_out, int max_contacts,
    float speculative_margin)
{
    /* NULL safety. */
    if (max_contacts <= 0) return 0;
    if (!contact_out) return 0;

    /* Extract axes from quaternions. */
    phys_vec3_t ax_a[3], ax_b[3];
    quat_to_axes(rotation_a, &ax_a[0], &ax_a[1], &ax_a[2]);
    quat_to_axes(rotation_b, &ax_b[0], &ax_b[1], &ax_b[2]);

    /* Relative translation vector. */
    phys_vec3_t d = vec3_sub(center_b, center_a);

    /* Half-extents as arrays for indexed access. */
    float ea[3] = {half_extents_a.x, half_extents_a.y, half_extents_a.z};
    float eb[3] = {half_extents_b.x, half_extents_b.y, half_extents_b.z};

    /*
     * R[i][j] = dot(ax_a[i], ax_b[j]) — rotation matrix expressing
     * B's frame in A's frame.
     * AbsR includes epsilon for robustness with parallel edges.
     */
    float R[3][3];
    float AbsR[3][3];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            R[i][j] = vec3_dot(ax_a[i], ax_b[j]);
            AbsR[i][j] = fabsf(R[i][j]) + SAT_EPSILON;
        }
    }

    /* d projected onto A's axes. */
    float da[3];
    for (int i = 0; i < 3; i++) {
        da[i] = vec3_dot(d, ax_a[i]);
    }

    /* Track the axis with minimum penetration (may be negative = speculative).
     * For overlapping boxes, min_pen gives the contact normal axis.
     * For speculative, we track max_pen (least separation = first collision). */
    float min_pen = 1e30f;
    float max_pen = -1e30f;
    int best_axis = -1;
    int spec_axis = -1;

    /* ── Test 15 separating axes ────────────────────────────────── */

    /* Axes 0-2: face normals of A. */
    for (int i = 0; i < 3; i++) {
        float ra = ea[i];
        float rb = eb[0] * AbsR[i][0] + eb[1] * AbsR[i][1] + eb[2] * AbsR[i][2];
        float sep = fabsf(da[i]) - (ra + rb);
        if (sep > speculative_margin) return 0;
        float pen = -(sep);
        if (pen < min_pen) {
            min_pen = pen;
            best_axis = i;
        }
        if (pen > max_pen) {
            max_pen = pen;
            spec_axis = i;
        }
    }

    /* d projected onto B's axes. */
    float db[3];
    for (int i = 0; i < 3; i++) {
        db[i] = vec3_dot(d, ax_b[i]);
    }

    /* Axes 3-5: face normals of B. */
    for (int i = 0; i < 3; i++) {
        float ra = ea[0] * AbsR[0][i] + ea[1] * AbsR[1][i] + ea[2] * AbsR[2][i];
        float rb = eb[i];
        float sep = fabsf(db[i]) - (ra + rb);
        if (sep > speculative_margin) return 0;
        float pen = -(sep);
        if (pen < min_pen) {
            min_pen = pen;
            best_axis = 3 + i;
        }
        if (pen > max_pen) {
            max_pen = pen;
            spec_axis = 3 + i;
        }
    }

    /* Axes 6-14: cross products of edge pairs. */
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            /* Cross product axis = ax_a[i] x ax_b[j]. */
            phys_vec3_t cross = vec3_cross(ax_a[i], ax_b[j]);
            float len2 = vec3_dot(cross, cross);
            if (len2 < SAT_EPSILON) continue; /* Near-parallel edges. */

            float inv_len = 1.0f / sqrtf(len2);

            /* Project extents onto the cross axis. */
            /* For A: sum of ea[k] * |dot(ax_a[k], cross)| for k != i. */
            float ra = 0.0f;
            for (int k = 0; k < 3; k++) {
                if (k == i) continue;
                /* |dot(ax_a[k], ax_a[i] x ax_b[j])| can be computed from AbsR. */
                ra += ea[k] * fabsf(vec3_dot(ax_a[k], cross));
            }
            float rb = 0.0f;
            for (int k = 0; k < 3; k++) {
                if (k == j) continue;
                rb += eb[k] * fabsf(vec3_dot(ax_b[k], cross));
            }

            float proj_d = fabsf(vec3_dot(d, cross));
            float sep = proj_d - (ra + rb);
            if (sep > speculative_margin) return 0;

            /* Normalize penetration by axis length.  Apply a small
             * relative bias so that face axes are preferred over edge-edge
             * axes when penetrations are nearly equal. */
            float pen = -(sep) * inv_len * EDGE_BIAS;
            if (pen < min_pen) {
                min_pen = pen;
                best_axis = 6 + i * 3 + j;
            }
            if (pen > max_pen) {
                max_pen = pen;
                spec_axis = 6 + i * 3 + j;
            }
        }
    }

    if (best_axis < 0) return 0; /* Should not happen. */

    /* ── Speculative contact (separated but within margin) ──────── */
    if (min_pen < 0.0f) {
        /* Boxes are separated; emit a single speculative contact at
         * the midpoint.  Use spec_axis (least-separated axis) for the
         * contact normal since that's where they'll collide first. */
        int sa = spec_axis;
        phys_vec3_t normal;
        if (sa < 3) {
            normal = ax_a[sa];
        } else if (sa < 6) {
            normal = ax_b[sa - 3];
        } else {
            int ei = (sa - 6) / 3;
            int ej = (sa - 6) % 3;
            normal = vec3_cross(ax_a[ei], ax_b[ej]);
            normal = vec3_normalize_safe(normal, SAT_EPSILON);
        }
        /* Ensure normal points from A toward B. */
        if (vec3_dot(normal, d) < 0.0f) {
            normal = vec3_scale(normal, -1.0f);
        }
        contact_out[0].point_world = vec3_scale(vec3_add(center_a, center_b), 0.5f);
        contact_out[0].normal = normal;
        contact_out[0].penetration = max_pen; /* negative = separated (speculative) */
        contact_out[0].feature_id = 0xFFFE0000u | (uint32_t)sa;
        contact_out[0].local_a = (phys_vec3_t){0, 0, 0};
        contact_out[0].local_b = (phys_vec3_t){0, 0, 0};
        return 1;
    }

    /* ── Compute contact normal ─────────────────────────────────── */

    phys_vec3_t normal;
    if (best_axis < 3) {
        /* Face axis of A. */
        normal = ax_a[best_axis];
    } else if (best_axis < 6) {
        /* Face axis of B. */
        normal = ax_b[best_axis - 3];
    } else {
        /* Edge-edge cross product. */
        int ei = (best_axis - 6) / 3;
        int ej = (best_axis - 6) % 3;
        normal = vec3_cross(ax_a[ei], ax_b[ej]);
        normal = vec3_normalize_safe(normal, SAT_EPSILON);
    }

    /* Ensure normal points from A toward B. */
    if (vec3_dot(normal, d) < 0.0f) {
        normal = vec3_scale(normal, -1.0f);
    }

    /* ── Generate contact points ────────────────────────────────── */

    if (best_axis >= 6) {
        /* Edge-edge contact: compute closest points on the two edges. */
        int ei = (best_axis - 6) / 3;
        int ej = (best_axis - 6) % 3;

        /* Find the support edges. */
        /* Edge on A: along ax_a[ei], at the support vertex in the
         * direction of normal projected onto the other two A axes. */
        phys_vec3_t support_a = center_a;
        for (int k = 0; k < 3; k++) {
            if (k == ei) continue;
            float sign = vec3_dot(ax_a[k], normal) > 0.0f ? ea[k] : -ea[k];
            support_a = vec3_add(support_a, vec3_scale(ax_a[k], sign));
        }

        phys_vec3_t support_b = center_b;
        for (int k = 0; k < 3; k++) {
            if (k == ej) continue;
            float sign = vec3_dot(ax_b[k], normal) > 0.0f ? -eb[k] : eb[k];
            support_b = vec3_add(support_b, vec3_scale(ax_b[k], sign));
        }

        /* Edge directions scaled by half-extents. */
        phys_vec3_t edge_a_start = vec3_sub(support_a, vec3_scale(ax_a[ei], ea[ei]));
        phys_vec3_t edge_a_dir = vec3_scale(ax_a[ei], 2.0f * ea[ei]);
        phys_vec3_t edge_b_start = vec3_sub(support_b, vec3_scale(ax_b[ej], eb[ej]));
        phys_vec3_t edge_b_dir = vec3_scale(ax_b[ej], 2.0f * eb[ej]);

        phys_vec3_t pt_a, pt_b;
        closest_points_segments(edge_a_start, edge_a_dir,
                                edge_b_start, edge_b_dir,
                                &pt_a, &pt_b);

        /* Contact at midpoint. */
        contact_out[0].point_world = vec3_scale(vec3_add(pt_a, pt_b), 0.5f);
        contact_out[0].normal = normal;
        contact_out[0].penetration = min_pen;
        /* Stable feature ID encoding the edge pair axis indices. */
        contact_out[0].feature_id =
            ((uint32_t)best_axis << 16) | ((uint32_t)ei << 8) | (uint32_t)ej;
        contact_out[0].local_a = (phys_vec3_t){0, 0, 0};
        contact_out[0].local_b = (phys_vec3_t){0, 0, 0};
        return 1;
    }

    /*
     * Face contact: clip the incident face polygon against the
     * reference face's side planes.
     */

    /* Determine reference and incident boxes. */
    int ref_face_axis;  /* axis index in the reference box */
    float ref_face_sign;
    const phys_vec3_t *ref_axes;
    const phys_vec3_t *inc_axes;
    phys_vec3_t ref_center, inc_center;
    float ref_e[3], inc_e[3];

    if (best_axis < 3) {
        /* Reference = A, incident = B. */
        ref_axes = ax_a;
        inc_axes = ax_b;
        ref_center = center_a;
        inc_center = center_b;
        for (int i = 0; i < 3; i++) { ref_e[i] = ea[i]; inc_e[i] = eb[i]; }
        ref_face_axis = best_axis;
        ref_face_sign = (da[best_axis] >= 0.0f) ? 1.0f : -1.0f;
    } else {
        /* Reference = B, incident = A. */
        ref_axes = ax_b;
        inc_axes = ax_a;
        ref_center = center_b;
        inc_center = center_a;
        for (int i = 0; i < 3; i++) { ref_e[i] = eb[i]; inc_e[i] = ea[i]; }
        ref_face_axis = best_axis - 3;
        ref_face_sign = (db[ref_face_axis] >= 0.0f) ? -1.0f : 1.0f;
    }

    /* Reference face normal (outward from the reference box). */
    phys_vec3_t ref_normal = vec3_scale(ref_axes[ref_face_axis], ref_face_sign);

    /* Find the incident face: the face of the incident box most
     * anti-parallel to the reference normal. */
    int inc_face_axis = 0;
    float inc_face_sign = 1.0f;
    float min_dot = 1e30f;
    for (int i = 0; i < 3; i++) {
        float dp = vec3_dot(inc_axes[i], ref_normal);
        if (dp < min_dot) {
            min_dot = dp;
            inc_face_axis = i;
            inc_face_sign = 1.0f;
        }
        if (-dp < min_dot) {
            min_dot = -dp;
            inc_face_axis = i;
            inc_face_sign = -1.0f;
        }
    }

    /* Build incident face quad (4 vertices). */
    phys_vec3_t inc_face_center = vec3_add(
        inc_center,
        vec3_scale(inc_axes[inc_face_axis], inc_face_sign * inc_e[inc_face_axis]));

    /* The two tangent axes of the incident face. */
    int t0 = (inc_face_axis + 1) % 3;
    int t1 = (inc_face_axis + 2) % 3;

    phys_vec3_t inc_quad[4];
    inc_quad[0] = vec3_add(inc_face_center,
        vec3_add(vec3_scale(inc_axes[t0], -inc_e[t0]),
                 vec3_scale(inc_axes[t1], -inc_e[t1])));
    inc_quad[1] = vec3_add(inc_face_center,
        vec3_add(vec3_scale(inc_axes[t0],  inc_e[t0]),
                 vec3_scale(inc_axes[t1], -inc_e[t1])));
    inc_quad[2] = vec3_add(inc_face_center,
        vec3_add(vec3_scale(inc_axes[t0],  inc_e[t0]),
                 vec3_scale(inc_axes[t1],  inc_e[t1])));
    inc_quad[3] = vec3_add(inc_face_center,
        vec3_add(vec3_scale(inc_axes[t0], -inc_e[t0]),
                 vec3_scale(inc_axes[t1],  inc_e[t1])));

    /* Clip against the 4 side planes of the reference face. */
    int rt0 = (ref_face_axis + 1) % 3;
    int rt1 = (ref_face_axis + 2) % 3;

    /* Working buffers for Sutherland-Hodgman clipping. */
    phys_vec3_t buf_a[8], buf_b[8];
    memcpy(buf_a, inc_quad, 4 * sizeof(phys_vec3_t));
    int count = 4;

    /* Clip against +rt0 side plane. */
    count = clip_polygon(buf_a, count, ref_axes[rt0],
                         vec3_dot(ref_axes[rt0], ref_center) + ref_e[rt0],
                         buf_b, 8);
    /* Clip against -rt0 side plane. */
    phys_vec3_t neg_rt0 = vec3_scale(ref_axes[rt0], -1.0f);
    count = clip_polygon(buf_b, count, neg_rt0,
                         vec3_dot(neg_rt0, ref_center) + ref_e[rt0],
                         buf_a, 8);
    /* Clip against +rt1 side plane. */
    count = clip_polygon(buf_a, count, ref_axes[rt1],
                         vec3_dot(ref_axes[rt1], ref_center) + ref_e[rt1],
                         buf_b, 8);
    /* Clip against -rt1 side plane. */
    phys_vec3_t neg_rt1 = vec3_scale(ref_axes[rt1], -1.0f);
    count = clip_polygon(buf_b, count, neg_rt1,
                         vec3_dot(neg_rt1, ref_center) + ref_e[rt1],
                         buf_a, 8);

    /* Project clipped points onto reference plane and keep those
     * that are behind (or on) the reference face. */
    float ref_plane_offset = vec3_dot(ref_normal, ref_center)
                             + ref_e[ref_face_axis];

    int num_contacts = 0;
    for (int i = 0; i < count && num_contacts < max_contacts; i++) {
        float sep = vec3_dot(buf_a[i], ref_normal) - ref_plane_offset;
        if (sep <= 0.0f) {
            contact_out[num_contacts].point_world = buf_a[i];
            contact_out[num_contacts].normal = normal;
            contact_out[num_contacts].penetration = -sep;

            /* Stable feature ID: encode the reference face, incident face,
             * and the quadrant of the clipped point on the incident face.
             * This prevents warmstart impulse mismatch when the clipping
             * filters out different points between frames. */
            phys_vec3_t rel = vec3_sub(buf_a[i], inc_face_center);
            int q0 = (vec3_dot(rel, inc_axes[t0]) >= 0.0f) ? 1 : 0;
            int q1 = (vec3_dot(rel, inc_axes[t1]) >= 0.0f) ? 1 : 0;
            uint32_t quadrant = (uint32_t)((q0 << 1) | q1);
            contact_out[num_contacts].feature_id =
                ((uint32_t)best_axis << 16)
              | ((uint32_t)inc_face_axis << 8)
              | (quadrant << 4)
              | (uint32_t)i;

            contact_out[num_contacts].local_a = (phys_vec3_t){0, 0, 0};
            contact_out[num_contacts].local_b = (phys_vec3_t){0, 0, 0};
            num_contacts++;
        }
    }

    /* If no contacts survived clipping (degenerate overlap), generate
     * a fallback contact at the midpoint. */
    if (num_contacts == 0 && min_pen > 0.0f) {
        phys_vec3_t midpoint = vec3_scale(vec3_add(center_a, center_b), 0.5f);
        contact_out[0].point_world = midpoint;
        contact_out[0].normal = normal;
        contact_out[0].penetration = min_pen;
        contact_out[0].feature_id = 0xFFFF0000u | (uint32_t)best_axis;
        contact_out[0].local_a = (phys_vec3_t){0, 0, 0};
        contact_out[0].local_b = (phys_vec3_t){0, 0, 0};
        num_contacts = 1;
    }

    return num_contacts;
}
