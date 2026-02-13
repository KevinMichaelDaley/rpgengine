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
        /* Interpolation between prev and curr using cubic hermite.
         * Tangents are server-authoritative velocities scaled by dt,
         * giving a smooth arc that respects the physics trajectory. */
        t = clampf_(t, 0.0f, 1.0f);

        const float t2 = t * t;
        const float t3 = t2 * t;
        /* Hermite basis: h00, h10, h01, h11 */
        const float h00 =  2.0f * t3 - 3.0f * t2 + 1.0f;
        const float h10 =         t3 - 2.0f * t2 + t;
        const float h01 = -2.0f * t3 + 3.0f * t2;
        const float h11 =         t3 -        t2;

        /* Tangents = velocity * dt (displacement over the interval). */
        const float fdt = (float)dt;
        const vec3_t m0 = {
            interp->prev_server_vel.x * fdt,
            interp->prev_server_vel.y * fdt,
            interp->prev_server_vel.z * fdt,
        };
        const vec3_t m1 = {
            interp->server_vel.x * fdt,
            interp->server_vel.y * fdt,
            interp->server_vel.z * fdt,
        };

        *out_pos = (vec3_t){
            h00 * interp->prev_pos.x + h10 * m0.x +
            h01 * interp->curr_pos.x + h11 * m1.x,
            h00 * interp->prev_pos.y + h10 * m0.y +
            h01 * interp->curr_pos.y + h11 * m1.y,
            h00 * interp->prev_pos.z + h10 * m0.z +
            h01 * interp->curr_pos.z + h11 * m1.z,
        };

        /* Rotation: velocity-aware interpolation.
         * Integrate angular velocity forward from prev by t*dt,
         * and backward from curr by (1-t)*dt, then slerp between
         * the two estimates for a smooth arc. */
        const float fwd_dt = t * fdt;
        const float bwd_dt = (1.0f - t) * fdt;

        /* Forward estimate: prev_rot rotated by prev_ang_vel * fwd_dt. */
        quat_t rot_fwd = interp->prev_rot;
        {
            float w = sqrtf(interp->prev_server_ang_vel.x * interp->prev_server_ang_vel.x +
                            interp->prev_server_ang_vel.y * interp->prev_server_ang_vel.y +
                            interp->prev_server_ang_vel.z * interp->prev_server_ang_vel.z);
            if (w > 1e-6f) {
                vec3_t ax = {
                    interp->prev_server_ang_vel.x / w,
                    interp->prev_server_ang_vel.y / w,
                    interp->prev_server_ang_vel.z / w,
                };
                quat_t dq = quat_from_axis_angle(ax, w * fwd_dt, 1e-8f);
                rot_fwd = quat_mul(dq, interp->prev_rot);
            }
        }

        /* Backward estimate: curr_rot rotated by -curr_ang_vel * bwd_dt. */
        quat_t rot_bwd = interp->curr_rot;
        {
            float w = sqrtf(interp->server_ang_vel.x * interp->server_ang_vel.x +
                            interp->server_ang_vel.y * interp->server_ang_vel.y +
                            interp->server_ang_vel.z * interp->server_ang_vel.z);
            if (w > 1e-6f) {
                vec3_t ax = {
                    interp->server_ang_vel.x / w,
                    interp->server_ang_vel.y / w,
                    interp->server_ang_vel.z / w,
                };
                quat_t dq = quat_from_axis_angle(ax, -w * bwd_dt, 1e-8f);
                rot_bwd = quat_mul(dq, interp->curr_rot);
            }
        }

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
