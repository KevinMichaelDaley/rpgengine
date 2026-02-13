#include "ferrum/net/replication/interp/pose_interpolator.h"

#include <math.h>
#include <stddef.h>

static float clampf_(float x, float lo, float hi) {
    if (x < lo) {
        return lo;
    }
    if (x > hi) {
        return hi;
    }
    return x;
}

/**
 * @brief Extract angular velocity from a delta quaternion over dt.
 *
 * Given dq = q_curr * conj(q_prev), extract the axis-angle and
 * return angular_velocity = axis * angle / dt.
 */
static vec3_t angular_vel_from_quats(quat_t prev, quat_t curr, double dt) {
    vec3_t zero = {0.0f, 0.0f, 0.0f};
    if (dt <= 1e-12) return zero;

    /* dq = curr * conj(prev) */
    quat_t prev_conj = quat_conjugate(prev);
    quat_t dq = quat_mul(curr, prev_conj);

    /* Ensure shortest path. */
    if (dq.w < 0.0f) {
        dq.x = -dq.x; dq.y = -dq.y;
        dq.z = -dq.z; dq.w = -dq.w;
    }

    /* Extract axis-angle: angle = 2 * acos(w), axis = xyz / sin(half_angle). */
    float half_angle = acosf(clampf_(dq.w, -1.0f, 1.0f));
    float sin_ha = sinf(half_angle);
    if (sin_ha < 1e-6f) return zero;

    float angle = 2.0f * half_angle;
    float inv_sin = 1.0f / sin_ha;
    float inv_dt = 1.0f / (float)dt;

    return (vec3_t){
        dq.x * inv_sin * angle * inv_dt,
        dq.y * inv_sin * angle * inv_dt,
        dq.z * inv_sin * angle * inv_dt,
    };
}

void fr_pose_interpolator_reset(fr_pose_interpolator_t *interp) {
    if (!interp) {
        return;
    }
    *interp = (fr_pose_interpolator_t){0};
}

bool fr_pose_interpolator_push(fr_pose_interpolator_t *interp, double recv_time_s, vec3_t pos, quat_t rot) {
    if (!interp) {
        return false;
    }

    if (!interp->has_curr) {
        interp->has_curr = true;
        interp->curr_time_s = recv_time_s;
        interp->curr_pos = pos;
        interp->curr_rot = rot;
        interp->implied_vel = (vec3_t){0, 0, 0};
        interp->implied_ang_vel = (vec3_t){0, 0, 0};
        return true;
    }

    interp->has_prev = true;
    interp->prev_time_s = interp->curr_time_s;
    interp->prev_pos = interp->curr_pos;
    interp->prev_rot = interp->curr_rot;

    interp->has_curr = true;
    interp->curr_time_s = recv_time_s;
    interp->curr_pos = pos;
    interp->curr_rot = rot;

    /* Compute implied velocities from the two snapshots. */
    double dt = interp->curr_time_s - interp->prev_time_s;
    if (dt > 1e-12) {
        float inv_dt = 1.0f / (float)dt;
        interp->implied_vel = (vec3_t){
            (pos.x - interp->prev_pos.x) * inv_dt,
            (pos.y - interp->prev_pos.y) * inv_dt,
            (pos.z - interp->prev_pos.z) * inv_dt,
        };
        interp->implied_ang_vel = angular_vel_from_quats(
            interp->prev_rot, interp->curr_rot, dt);
    }

    return true;
}

bool fr_pose_interpolator_sample(const fr_pose_interpolator_t *interp,
                                double now_time_s,
                                float quat_epsilon,
                                vec3_t *out_pos,
                                quat_t *out_rot) {
    if (!interp || !out_pos || !out_rot) {
        return false;
    }

    if (!interp->has_curr) {
        return false;
    }

    if (!interp->has_prev) {
        *out_pos = interp->curr_pos;
        *out_rot = quat_normalize_safe(interp->curr_rot, quat_epsilon);
        return true;
    }

    const double dt = interp->curr_time_s - interp->prev_time_s;
    if (dt <= 1e-12) {
        *out_pos = interp->curr_pos;
        *out_rot = quat_normalize_safe(interp->curr_rot, quat_epsilon);
        return true;
    }

    float t = (float)((now_time_s - interp->prev_time_s) / dt);

    if (t <= 1.0f) {
        /* Interpolation between prev and curr — simple lerp/slerp. */
        t = clampf_(t, 0.0f, 1.0f);

        const vec3_t a = interp->prev_pos;
        const vec3_t b = interp->curr_pos;
        *out_pos = (vec3_t){
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t,
        };

        *out_rot = quat_slerp(interp->prev_rot, interp->curr_rot, t,
                               quat_epsilon);
    } else {
        /* Extrapolation beyond curr — use implied velocity.
         * Cap at 1.5× to avoid runaway drift on packet loss. */
        float extrap = clampf_(t - 1.0f, 0.0f, 0.8f);
        float extrap_dt = extrap * (float)dt;

        /* Linear: curr_pos + vel * extrap_dt. */
        *out_pos = (vec3_t){
            interp->curr_pos.x + interp->implied_vel.x * extrap_dt,
            interp->curr_pos.y + interp->implied_vel.y * extrap_dt,
            interp->curr_pos.z + interp->implied_vel.z * extrap_dt,
        };

        /* Angular: apply angular velocity as axis-angle rotation.
         * omega = implied_ang_vel, angle = |omega| * extrap_dt. */
        float ang_speed = sqrtf(
            interp->implied_ang_vel.x * interp->implied_ang_vel.x +
            interp->implied_ang_vel.y * interp->implied_ang_vel.y +
            interp->implied_ang_vel.z * interp->implied_ang_vel.z);
        if (ang_speed > 1e-6f) {
            vec3_t axis = {
                interp->implied_ang_vel.x / ang_speed,
                interp->implied_ang_vel.y / ang_speed,
                interp->implied_ang_vel.z / ang_speed,
            };
            float angle = ang_speed * extrap_dt;
            quat_t dq = quat_from_axis_angle(axis, angle, 1e-8f);
            *out_rot = quat_normalize_safe(
                quat_mul(dq, interp->curr_rot), quat_epsilon);
        } else {
            *out_rot = quat_normalize_safe(interp->curr_rot, quat_epsilon);
        }
    }
    return true;
}
