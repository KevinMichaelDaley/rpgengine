#include "ferrum/net/replication/interp/pose_interpolator.h"

#include "ferrum/math/common.h"

#include <math.h>
#include <stddef.h>

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
    float half_angle = acosf(fr_clampf(dq.w, -1.0f, 1.0f));
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

bool fr_pose_interpolator_push(fr_pose_interpolator_t *interp, double recv_time_s,
                               vec3_t pos, quat_t rot, vec3_t vel, vec3_t ang_vel,
                               double server_time_s) {
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
        interp->server_vel = vel;
        interp->server_ang_vel = ang_vel;
        interp->server_time_s = server_time_s;
        return true;
    }

    interp->has_prev = true;
    interp->prev_time_s = interp->curr_time_s;
    interp->prev_pos = interp->curr_pos;
    interp->prev_rot = interp->curr_rot;
    interp->prev_server_vel = interp->server_vel;
    interp->prev_server_ang_vel = interp->server_ang_vel;

    interp->has_curr = true;
    interp->curr_time_s = recv_time_s;
    interp->curr_pos = pos;
    interp->curr_rot = rot;
    interp->server_vel = vel;
    interp->server_ang_vel = ang_vel;
    interp->server_time_s = server_time_s;

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
        /* Semi-physical interpolation: integrate the full rigid body
         * state forward from prev and backward from curr, then lerp/
         * slerp between the two estimates.  This keeps position and
         * rotation coupled through their velocities. */
        t = fr_clampf(t, 0.0f, 1.0f);
        const float fdt = (float)dt;
        const float fwd_t = t * fdt;       /* time forward from prev */
        const float bwd_t = (1.0f - t) * fdt; /* time backward from curr */

        /* Forward estimate: prev + prev_vel * fwd_t. */
        vec3_t pos_fwd = {
            interp->prev_pos.x + interp->prev_server_vel.x * fwd_t,
            interp->prev_pos.y + interp->prev_server_vel.y * fwd_t,
            interp->prev_pos.z + interp->prev_server_vel.z * fwd_t,
        };
        quat_t rot_fwd = interp->prev_rot;
        {
            float w = sqrtf(
                interp->prev_server_ang_vel.x * interp->prev_server_ang_vel.x +
                interp->prev_server_ang_vel.y * interp->prev_server_ang_vel.y +
                interp->prev_server_ang_vel.z * interp->prev_server_ang_vel.z);
            if (w > 1e-6f) {
                vec3_t ax = {
                    interp->prev_server_ang_vel.x / w,
                    interp->prev_server_ang_vel.y / w,
                    interp->prev_server_ang_vel.z / w,
                };
                quat_t dq = quat_from_axis_angle(ax, w * fwd_t, 1e-8f);
                rot_fwd = quat_mul(dq, interp->prev_rot);
            }
        }

        /* Backward estimate: curr - curr_vel * bwd_t. */
        vec3_t pos_bwd = {
            interp->curr_pos.x - interp->server_vel.x * bwd_t,
            interp->curr_pos.y - interp->server_vel.y * bwd_t,
            interp->curr_pos.z - interp->server_vel.z * bwd_t,
        };
        quat_t rot_bwd = interp->curr_rot;
        {
            float w = sqrtf(
                interp->server_ang_vel.x * interp->server_ang_vel.x +
                interp->server_ang_vel.y * interp->server_ang_vel.y +
                interp->server_ang_vel.z * interp->server_ang_vel.z);
            if (w > 1e-6f) {
                vec3_t ax = {
                    interp->server_ang_vel.x / w,
                    interp->server_ang_vel.y / w,
                    interp->server_ang_vel.z / w,
                };
                quat_t dq = quat_from_axis_angle(ax, -w * bwd_t, 1e-8f);
                rot_bwd = quat_mul(dq, interp->curr_rot);
            }
        }

        /* Blend forward and backward estimates.  At t=0 we trust
         * the forward estimate fully; at t=1 the backward. */
        out_pos->x = pos_fwd.x * (1.0f - t) + pos_bwd.x * t;
        out_pos->y = pos_fwd.y * (1.0f - t) + pos_bwd.y * t;
        out_pos->z = pos_fwd.z * (1.0f - t) + pos_bwd.z * t;
        *out_rot = quat_slerp(rot_fwd, rot_bwd, t, quat_epsilon);
    } else {
        /* Beyond the latest snapshot — hold curr pose.
         * Extrapolation is intentionally disabled: at any meaningful
         * velocity the displacement per frame exceeds constraint
         * tolerances, producing visible artifacts. */
        *out_pos = interp->curr_pos;
        *out_rot = quat_normalize_safe(interp->curr_rot, quat_epsilon);
    }
    return true;
}
