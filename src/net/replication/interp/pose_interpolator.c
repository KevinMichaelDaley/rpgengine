#include "ferrum/net/replication/interp/pose_interpolator.h"

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
    t = clampf_(t, 0.0f, 1.0f);

    const vec3_t a = interp->prev_pos;
    const vec3_t b = interp->curr_pos;
    *out_pos = (vec3_t){
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
    };

    *out_rot = quat_slerp(interp->prev_rot, interp->curr_rot, t, quat_epsilon);
    return true;
}
