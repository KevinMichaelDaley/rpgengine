/**
 * @file narrowphase_sphere_box.c
 * @brief Sphere vs oriented box (OBB) narrowphase intersection test.
 */

#include <math.h>

#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/narrowphase.h"

/**
 * @brief Rotate a vector by a quaternion: v' = q * v * q^-1.
 *
 * Uses the optimized formula: v' = v + 2w*(u × v) + 2*(u × (u × v))
 * where q = (u.x, u.y, u.z, w).
 */
static phys_vec3_t quat_rotate_vec3(phys_quat_t q, phys_vec3_t v)
{
    phys_vec3_t u = {q.x, q.y, q.z};
    float w = q.w;

    phys_vec3_t uv = vec3_cross(u, v);
    phys_vec3_t uuv = vec3_cross(u, uv);

    /* v' = v + 2*w*uv + 2*uuv */
    return vec3_add(v, vec3_add(vec3_scale(uv, 2.0f * w),
                                vec3_scale(uuv, 2.0f)));
}

/**
 * @brief Clamp a float to [lo, hi].
 */
static float clampf(float val, float lo, float hi)
{
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

bool phys_sphere_vs_box(
    phys_vec3_t sphere_center, float sphere_radius,
    phys_vec3_t box_center, phys_quat_t box_rotation,
    phys_vec3_t box_half_extents,
    phys_contact_point_t *contact_out)
{
    if (!contact_out) {
        return false;
    }

    /* Step 1: Transform sphere center to box local space. */
    phys_vec3_t delta = vec3_sub(sphere_center, box_center);
    phys_quat_t inv_rot = quat_conjugate(box_rotation);
    phys_vec3_t local_center = quat_rotate_vec3(inv_rot, delta);

    /* Step 2: Clamp to box extents → closest point on box surface. */
    phys_vec3_t closest_local = {
        clampf(local_center.x, -box_half_extents.x, box_half_extents.x),
        clampf(local_center.y, -box_half_extents.y, box_half_extents.y),
        clampf(local_center.z, -box_half_extents.z, box_half_extents.z),
    };

    /* Step 3: Difference from sphere center (local) to closest point. */
    phys_vec3_t diff = vec3_sub(local_center, closest_local);
    float dist_sq = vec3_dot(diff, diff);

    const float epsilon = 1e-6f;
    phys_vec3_t normal_local;
    float penetration;

    if (dist_sq > sphere_radius * sphere_radius) {
        /* Separated — no contact. */
        return false;
    }

    if (dist_sq < epsilon) {
        /*
         * Step 6: Sphere center is inside the box.
         * Find the closest face and push out along that axis.
         */
        float half[3] = {box_half_extents.x, box_half_extents.y,
                         box_half_extents.z};
        float lc[3] = {local_center.x, local_center.y, local_center.z};

        float min_depth = half[0] - fabsf(lc[0]);
        int min_axis = 0;

        for (int i = 1; i < 3; i++) {
            float depth = half[i] - fabsf(lc[i]);
            if (depth < min_depth) {
                min_depth = depth;
                min_axis = i;
            }
        }

        /* Normal points toward the face the sphere center is closest to. */
        float sign = (lc[min_axis] >= 0.0f) ? 1.0f : -1.0f;
        normal_local = (phys_vec3_t){0.0f, 0.0f, 0.0f};
        float *nl = &normal_local.x;
        nl[min_axis] = sign;

        penetration = min_depth + sphere_radius;
    } else {
        /* Step 7: Sphere center outside box. */
        float dist = sqrtf(dist_sq);
        normal_local = vec3_scale(diff, 1.0f / dist);
        penetration = sphere_radius - dist;
    }

    /* Step 8: Transform normal back to world space. */
    phys_vec3_t world_normal = quat_rotate_vec3(box_rotation, normal_local);

    /* Step 9: Contact point on sphere surface toward box. */
    contact_out->normal = world_normal;
    contact_out->penetration = penetration;
    contact_out->point_world = vec3_sub(
        sphere_center,
        vec3_scale(world_normal, sphere_radius - penetration * 0.5f));
    contact_out->feature_id = 0;

    return true;
}
