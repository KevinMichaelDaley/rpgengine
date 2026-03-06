/**
 * @file ragdoll_motor.c
 * @brief Ragdoll motor strength and target update.
 *
 * Non-static functions: 3 (ragdoll_set_motor_strength,
 *                          ragdoll_set_bone_motor_strength,
 *                          ragdoll_update_motor_targets)
 */

#include "ferrum/animation/ragdoll.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/quat.h"

#include <math.h>

/**
 * @brief Extract rotation quaternion from a column-major mat4_t.
 */
static quat_t extract_rotation(const mat4_t *m) {
    float sx = sqrtf(m->m[0]*m->m[0] + m->m[1]*m->m[1] + m->m[2]*m->m[2]);
    float sy = sqrtf(m->m[4]*m->m[4] + m->m[5]*m->m[5] + m->m[6]*m->m[6]);
    float sz = sqrtf(m->m[8]*m->m[8] + m->m[9]*m->m[9] + m->m[10]*m->m[10]);
    if (sx < 1e-7f) sx = 1e-7f;
    if (sy < 1e-7f) sy = 1e-7f;
    if (sz < 1e-7f) sz = 1e-7f;

    float r00 = m->m[0] / sx, r01 = m->m[4] / sy, r02 = m->m[8] / sz;
    float r10 = m->m[1] / sx, r11 = m->m[5] / sy, r12 = m->m[9] / sz;
    float r20 = m->m[2] / sx, r21 = m->m[6] / sy, r22 = m->m[10] / sz;

    float trace = r00 + r11 + r22;
    quat_t q;
    if (trace > 0.f) {
        float s = 0.5f / sqrtf(trace + 1.f);
        q.w = 0.25f / s;
        q.x = (r21 - r12) * s;
        q.y = (r02 - r20) * s;
        q.z = (r10 - r01) * s;
    } else if (r00 > r11 && r00 > r22) {
        float s = 2.f * sqrtf(1.f + r00 - r11 - r22);
        q.w = (r21 - r12) / s;
        q.x = 0.25f * s;
        q.y = (r01 + r10) / s;
        q.z = (r02 + r20) / s;
    } else if (r11 > r22) {
        float s = 2.f * sqrtf(1.f + r11 - r00 - r22);
        q.w = (r02 - r20) / s;
        q.x = (r01 + r10) / s;
        q.y = 0.25f * s;
        q.z = (r12 + r21) / s;
    } else {
        float s = 2.f * sqrtf(1.f + r22 - r00 - r11);
        q.w = (r10 - r01) / s;
        q.x = (r02 + r20) / s;
        q.y = (r12 + r21) / s;
        q.z = 0.25f * s;
    }

    float len = sqrtf(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    if (len > 1e-7f) {
        float inv = 1.f / len;
        q.x *= inv; q.y *= inv; q.z *= inv; q.w *= inv;
    }
    return q;
}

void ragdoll_set_motor_strength(ragdoll_t *ragdoll, float strength) {
    if (!ragdoll) return;
    for (uint32_t i = 0; i < ragdoll->bone_count; i++) {
        ragdoll->motor_strengths[i] = strength;
    }
}

void ragdoll_set_bone_motor_strength(ragdoll_t *ragdoll, uint32_t bone_idx,
                                     float strength) {
    if (!ragdoll || bone_idx >= ragdoll->bone_count) return;
    ragdoll->motor_strengths[bone_idx] = strength;
}

void ragdoll_update_motor_targets(ragdoll_t *ragdoll,
                                  const mat4_t *target_pose,
                                  uint32_t pose_count) {
    if (!ragdoll || !target_pose) return;
    uint32_t count = pose_count < ragdoll->bone_count
                     ? pose_count : ragdoll->bone_count;
    for (uint32_t i = 0; i < count; i++) {
        ragdoll->motors[i].target_orientation = extract_rotation(&target_pose[i]);
        ragdoll->motors[i].strength = ragdoll->motor_strengths[i];
    }
}
