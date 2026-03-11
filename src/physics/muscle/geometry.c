/**
 * @file geometry.c
 * @brief Muscle attachment geometry and moment arm computation.
 *
 * 2 non-static functions:
 *   1. phys_muscle_attach_init
 *   2. phys_muscle_geometry_moment_arm
 */

#include "ferrum/physics/muscle/geometry.h"
#include "ferrum/physics/body.h"
#include "ferrum/math/quat.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/* ── Static helpers ───────────────────────────────────────────────── */

/** Dot product of two phys_vec3_t. */
static float dot3_(phys_vec3_t a, phys_vec3_t b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

/** Cross product of two phys_vec3_t. */
static phys_vec3_t cross3_(phys_vec3_t a, phys_vec3_t b)
{
    return (phys_vec3_t){
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

/** Vector subtraction. */
static phys_vec3_t sub3_(phys_vec3_t a, phys_vec3_t b)
{
    return (phys_vec3_t){ a.x - b.x, a.y - b.y, a.z - b.z };
}

/** Vector length. */
static float len3_(phys_vec3_t v)
{
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

/** Transform a local-space point to world space using body position + orientation. */
static phys_vec3_t body_to_world_(const phys_body_t *body, phys_vec3_t local)
{
    phys_vec3_t rotated = quat_rotate_vec3(body->orientation, local);
    return (phys_vec3_t){
        body->position.x + rotated.x,
        body->position.y + rotated.y,
        body->position.z + rotated.z
    };
}

/* ── Public API ───────────────────────────────────────────────────── */

void phys_muscle_attach_init(phys_muscle_attach_t *att)
{
    if (!att) { return; }
    memset(att, 0, sizeof(*att));
}

void phys_muscle_geometry_moment_arm(
    const phys_muscle_attach_t *attach,
    const phys_muscle_wrap_t *wrap,
    const phys_vec3_t *joint_axis_world,
    const phys_vec3_t *joint_pos_world,
    const phys_body_t *body_a,
    const phys_body_t *body_b,
    float *moment_arm_out,
    float *fiber_length_out)
{
    if (!attach || !joint_axis_world || !joint_pos_world ||
        !body_a || !body_b || !moment_arm_out) {
        if (moment_arm_out) { *moment_arm_out = 0.0f; }
        if (fiber_length_out) { *fiber_length_out = 0.0f; }
        return;
    }

    /* Transform attachment points to world space. */
    phys_vec3_t origin_world    = body_to_world_(body_a, attach->origin_local);
    phys_vec3_t insertion_world = body_to_world_(body_b, attach->insertion_local);

    /* Muscle line of action: from origin to insertion. */
    phys_vec3_t line = sub3_(insertion_world, origin_world);
    float fiber_len = len3_(line);

    /* Apply cylinder wrapping if specified. */
    if (wrap && wrap->radius > 0.0f) {
        /* Transform wrap center and axis to world space (on body_a). */
        phys_vec3_t wrap_center = body_to_world_(body_a, wrap->center_local);
        phys_vec3_t wrap_axis   = quat_rotate_vec3(body_a->orientation,
                                                     wrap->axis_local);

        /* Project origin and insertion onto the plane perpendicular to
         * the wrap axis, compute the shortest path wrapping around the
         * cylinder.  For simplicity, we add pi*r to the straight-line
         * length when the line passes through the cylinder. */
        phys_vec3_t to_origin = sub3_(origin_world, wrap_center);
        phys_vec3_t to_insert = sub3_(insertion_world, wrap_center);

        /* Remove component along wrap axis. */
        float od = dot3_(to_origin, wrap_axis);
        float id = dot3_(to_insert, wrap_axis);
        phys_vec3_t o_perp = {
            to_origin.x - od * wrap_axis.x,
            to_origin.y - od * wrap_axis.y,
            to_origin.z - od * wrap_axis.z
        };
        phys_vec3_t i_perp = {
            to_insert.x - id * wrap_axis.x,
            to_insert.y - id * wrap_axis.y,
            to_insert.z - id * wrap_axis.z
        };

        float o_dist = len3_(o_perp);
        float i_dist = len3_(i_perp);

        /* If both points are outside the cylinder and the line midpoint
         * passes close to the center, add wrapping arc length. */
        if (o_dist > wrap->radius && i_dist > wrap->radius) {
            phys_vec3_t mid = {
                (origin_world.x + insertion_world.x) * 0.5f - wrap_center.x,
                (origin_world.y + insertion_world.y) * 0.5f - wrap_center.y,
                (origin_world.z + insertion_world.z) * 0.5f - wrap_center.z
            };
            float md = dot3_(mid, wrap_axis);
            phys_vec3_t m_perp = {
                mid.x - md * wrap_axis.x,
                mid.y - md * wrap_axis.y,
                mid.z - md * wrap_axis.z
            };
            float m_dist = len3_(m_perp);
            if (m_dist < wrap->radius) {
                /* Approximate wrapping: add semicircle arc. */
                float tangent_len_o = sqrtf(o_dist * o_dist -
                                            wrap->radius * wrap->radius);
                float tangent_len_i = sqrtf(i_dist * i_dist -
                                            wrap->radius * wrap->radius);
                /* Angle subtended by the wrap. */
                float angle_o = acosf(wrap->radius / o_dist);
                float angle_i = acosf(wrap->radius / i_dist);
                float arc = wrap->radius * (3.14159265f - angle_o - angle_i);
                if (arc < 0.0f) { arc = 0.0f; }
                fiber_len = tangent_len_o + arc + tangent_len_i;
            }
        }
    }

    if (fiber_length_out) {
        *fiber_length_out = fiber_len;
    }

    /* Compute moment arm: perpendicular distance from the joint axis
     * to the muscle line of action at the joint pivot.
     *
     * moment_arm = |cross(joint_axis, r)| / |joint_axis|
     * where r is the vector from the joint to the midpoint of the
     * muscle line.  Sign is determined by the cross product direction. */
    phys_vec3_t midpoint = {
        (origin_world.x + insertion_world.x) * 0.5f,
        (origin_world.y + insertion_world.y) * 0.5f,
        (origin_world.z + insertion_world.z) * 0.5f
    };
    phys_vec3_t r = sub3_(midpoint, *joint_pos_world);

    /* Project r perpendicular to joint axis. */
    float r_along_axis = dot3_(r, *joint_axis_world);
    phys_vec3_t r_perp = {
        r.x - r_along_axis * joint_axis_world->x,
        r.y - r_along_axis * joint_axis_world->y,
        r.z - r_along_axis * joint_axis_world->z
    };

    /* Moment arm = signed distance: cross(r_perp, line_dir) · axis.
     * This gives the signed perpendicular distance. */
    if (fiber_len > 1e-7f) {
        phys_vec3_t line_dir = {
            line.x / fiber_len, line.y / fiber_len, line.z / fiber_len
        };
        phys_vec3_t c = cross3_(r_perp, line_dir);
        *moment_arm_out = dot3_(c, *joint_axis_world);
    } else {
        *moment_arm_out = 0.0f;
    }
}
