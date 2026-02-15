/**
 * @file narrowphase_halfspace.c
 * @brief Halfspace (infinite plane) narrowphase intersection tests.
 *
 * Sphere, capsule, and box vs halfspace.  The halfspace plane is
 * defined by dot(normal, point) = distance.  Points below the plane
 * (dot < distance) are penetrating.
 *
 * Non-static functions: 3 (sphere, capsule, box).
 */

#include <math.h>

#include "ferrum/math/vec3.h"
#include "ferrum/physics/collision/halfspace.h"
#include "ferrum/physics/manifold.h"

/**
 * @brief Rotate a vector by a quaternion.
 *
 * Uses the q * v * q^-1 formula in expanded cross-product form.
 */
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

/* ── Sphere vs Halfspace ───────────────────────────────────────── */

bool phys_sphere_vs_halfspace(
    phys_vec3_t sphere_center, float sphere_radius,
    phys_vec3_t plane_normal, float plane_distance,
    float speculative_margin,
    phys_contact_point_t *contact_out)
{
    if (!contact_out) return false;

    /* Signed distance from sphere center to plane. */
    float signed_dist = vec3_dot(plane_normal, sphere_center) - plane_distance;

    /* Penetration = radius - signed_dist (positive = overlap). */
    float pen = sphere_radius - signed_dist;

    /* No contact if gap exceeds speculative margin. */
    if (pen < -speculative_margin) return false;

    /* Contact point on the sphere surface closest to the plane. */
    phys_vec3_t contact_pt = vec3_sub(sphere_center,
                                       vec3_scale(plane_normal, signed_dist));

    /* Normal points from the shape (body_a after type-swap) toward the
     * halfspace (body_b), i.e. INTO the solid half.  The plane_normal
     * points outward, so we negate it. */
    contact_out->normal = vec3_scale(plane_normal, -1.0f);
    contact_out->penetration = pen;
    contact_out->point_world = contact_pt;
    contact_out->local_a = (phys_vec3_t){0, 0, 0};
    contact_out->local_b = (phys_vec3_t){0, 0, 0};
    contact_out->feature_id = 0;

    return true;
}

/* ── Capsule vs Halfspace ──────────────────────────────────────── */

bool phys_capsule_vs_halfspace(
    phys_vec3_t capsule_center, phys_quat_t capsule_rotation,
    float capsule_radius, float capsule_half_height,
    phys_vec3_t plane_normal, float plane_distance,
    float speculative_margin,
    phys_contact_point_t *contact_out)
{
    if (!contact_out) return false;

    /* Capsule axis in world space: rotate local +Y by capsule_rotation. */
    phys_vec3_t local_y = {0.0f, 1.0f, 0.0f};
    phys_vec3_t axis = quat_rotate_vec3(capsule_rotation, local_y);

    /* Two capsule endpoints. */
    phys_vec3_t tip = vec3_add(capsule_center,
                                vec3_scale(axis, capsule_half_height));
    phys_vec3_t base = vec3_sub(capsule_center,
                                 vec3_scale(axis, capsule_half_height));

    /* Signed distance of each endpoint to plane. */
    float dist_tip  = vec3_dot(plane_normal, tip)  - plane_distance;
    float dist_base = vec3_dot(plane_normal, base) - plane_distance;

    /* Use the deeper (lower signed distance) endpoint. */
    phys_vec3_t deepest_center;
    float deepest_dist;
    if (dist_tip < dist_base) {
        deepest_center = tip;
        deepest_dist = dist_tip;
    } else {
        deepest_center = base;
        deepest_dist = dist_base;
    }

    float pen = capsule_radius - deepest_dist;

    if (pen < -speculative_margin) return false;

    /* Contact point on the plane directly below the deepest sphere center. */
    phys_vec3_t contact_pt = vec3_sub(deepest_center,
                                       vec3_scale(plane_normal, deepest_dist));

    /* Normal points from the shape (body_a) toward the halfspace (body_b),
     * i.e. into the solid half.  Negate the outward plane_normal. */
    contact_out->normal = vec3_scale(plane_normal, -1.0f);
    contact_out->penetration = pen;
    contact_out->point_world = contact_pt;
    contact_out->local_a = (phys_vec3_t){0, 0, 0};
    contact_out->local_b = (phys_vec3_t){0, 0, 0};
    contact_out->feature_id = 0;

    return true;
}

/* ── Box vs Halfspace ──────────────────────────────────────────── */

int phys_box_vs_halfspace(
    phys_vec3_t box_center, phys_quat_t box_rotation,
    phys_vec3_t box_half_extents,
    phys_vec3_t plane_normal, float plane_distance,
    float speculative_margin,
    phys_contact_point_t *contacts_out, int max_contacts)
{
    if (!contacts_out || max_contacts <= 0) return 0;

    /* Box local axes in world space. */
    phys_vec3_t ax = quat_rotate_vec3(box_rotation, (phys_vec3_t){1, 0, 0});
    phys_vec3_t ay = quat_rotate_vec3(box_rotation, (phys_vec3_t){0, 1, 0});
    phys_vec3_t az = quat_rotate_vec3(box_rotation, (phys_vec3_t){0, 0, 1});

    /* Scaled half-extent vectors. */
    phys_vec3_t hx = vec3_scale(ax, box_half_extents.x);
    phys_vec3_t hy = vec3_scale(ay, box_half_extents.y);
    phys_vec3_t hz = vec3_scale(az, box_half_extents.z);

    /* Compute 8 vertices and test each against the plane.
     * Store penetrating vertices sorted by depth (deepest first). */
    phys_vec3_t verts[8];
    float pens[8];
    int pen_count = 0;

    for (int sx = -1; sx <= 1; sx += 2) {
        for (int sy = -1; sy <= 1; sy += 2) {
            for (int sz = -1; sz <= 1; sz += 2) {
                phys_vec3_t v = box_center;
                v = vec3_add(v, vec3_scale(hx, (float)sx));
                v = vec3_add(v, vec3_scale(hy, (float)sy));
                v = vec3_add(v, vec3_scale(hz, (float)sz));

                float signed_dist = vec3_dot(plane_normal, v) - plane_distance;
                float pen = -signed_dist;  /* positive when below plane */

                if (pen >= -speculative_margin) {
                    /* Insert sorted by penetration depth (descending). */
                    int ins = pen_count;
                    for (int j = 0; j < pen_count; j++) {
                        if (pen > pens[j]) {
                            ins = j;
                            break;
                        }
                    }
                    /* Shift right. */
                    if (pen_count < 8) pen_count++;
                    for (int j = pen_count - 1; j > ins; j--) {
                        verts[j] = verts[j - 1];
                        pens[j] = pens[j - 1];
                    }
                    verts[ins] = v;
                    pens[ins] = pen;
                }
            }
        }
    }

    /* Output up to max_contacts deepest vertices. */
    int out_count = pen_count;
    if (out_count > max_contacts) out_count = max_contacts;

    for (int i = 0; i < out_count; i++) {
        /* Contact point is the vertex projected onto the plane. */
        float signed_dist = vec3_dot(plane_normal, verts[i]) - plane_distance;
        phys_vec3_t contact_pt = vec3_sub(verts[i],
                                           vec3_scale(plane_normal, signed_dist));

        contacts_out[i].normal = vec3_scale(plane_normal, -1.0f);
        contacts_out[i].penetration = pens[i];
        contacts_out[i].point_world = contact_pt;
        contacts_out[i].local_a = (phys_vec3_t){0, 0, 0};
        contacts_out[i].local_b = (phys_vec3_t){0, 0, 0};
        contacts_out[i].feature_id = (uint32_t)i;
    }

    return out_count;
}
