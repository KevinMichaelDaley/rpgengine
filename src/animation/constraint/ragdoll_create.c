/**
 * @file ragdoll_create.c
 * @brief Ragdoll creation and destruction.
 *
 * Creates physics bodies and ball joints from a skeleton definition.
 * Each bone gets a body positioned at its world-space bind pose.
 * Parent-child pairs get ball joints at the child's position.
 *
 * Non-static functions: 2 (ragdoll_create, ragdoll_destroy)
 */

#include "ferrum/animation/ragdoll.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/joint.h"
#include "ferrum/physics/joint_motor.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Extract position from a mat4_t.
 */
static vec3_t mat4_get_position(const mat4_t *m) {
    return (vec3_t){ m->m[12], m->m[13], m->m[14] };
}

/**
 * @brief Extract rotation quaternion from a mat4_t.
 *
 * Assumes the matrix has no shear; extracts rotation from the
 * upper-left 3×3 after stripping scale.
 */
static quat_t mat4_get_rotation(const mat4_t *m) {
    /* Extract column magnitudes (scale). */
    float sx = sqrtf(m->m[0]*m->m[0] + m->m[1]*m->m[1] + m->m[2]*m->m[2]);
    float sy = sqrtf(m->m[4]*m->m[4] + m->m[5]*m->m[5] + m->m[6]*m->m[6]);
    float sz = sqrtf(m->m[8]*m->m[8] + m->m[9]*m->m[9] + m->m[10]*m->m[10]);
    if (sx < 1e-7f) sx = 1e-7f;
    if (sy < 1e-7f) sy = 1e-7f;
    if (sz < 1e-7f) sz = 1e-7f;

    /* Normalized rotation matrix elements. */
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

    /* Normalize. */
    float len = sqrtf(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    if (len > 1e-7f) {
        float inv = 1.f / len;
        q.x *= inv; q.y *= inv; q.z *= inv; q.w *= inv;
    }
    return q;
}

bool ragdoll_create(ragdoll_t *ragdoll,
                    const struct skeleton_def *skel_opaque,
                    const mat4_t *world_pose) {
    const skeleton_def_t *skel = (const skeleton_def_t *)skel_opaque;
    if (!ragdoll || !skel || !world_pose || skel->joint_count == 0) return false;

    memset(ragdoll, 0, sizeof(*ragdoll));
    uint32_t n = skel->joint_count;
    ragdoll->bone_count = n;

    /* Allocate all arrays. */
    ragdoll->body_indices = (uint32_t *)calloc(n, sizeof(uint32_t));
    ragdoll->joint_indices = (uint32_t *)calloc(n, sizeof(uint32_t));
    ragdoll->motor_strengths = (float *)calloc(n, sizeof(float));
    ragdoll->motors = (phys_joint_motor_t *)calloc(n, sizeof(phys_joint_motor_t));
    ragdoll->bodies = (phys_body_t *)calloc(n, sizeof(phys_body_t));
    ragdoll->joints = (phys_joint_t *)calloc(n, sizeof(phys_joint_t));
    ragdoll->bone_world = (mat4_t *)calloc(n, sizeof(mat4_t));

    if (!ragdoll->body_indices || !ragdoll->joint_indices ||
        !ragdoll->motor_strengths || !ragdoll->motors ||
        !ragdoll->bodies || !ragdoll->joints || !ragdoll->bone_world) {
        ragdoll_destroy(ragdoll);
        return false;
    }

    /* Create bodies: one per bone. */
    for (uint32_t i = 0; i < n; i++) {
        phys_body_init(&ragdoll->bodies[i]);
        ragdoll->bodies[i].position = (phys_vec3_t){
            world_pose[i].m[12],
            world_pose[i].m[13],
            world_pose[i].m[14]
        };
        ragdoll->bodies[i].orientation = mat4_get_rotation(&world_pose[i]);

        /* Dynamic body with unit mass (caller can adjust later). */
        phys_body_set_mass(&ragdoll->bodies[i], 1.0f);
        phys_body_set_box_inertia(&ragdoll->bodies[i], 1.0f,
                                  (phys_vec3_t){0.1f, 0.5f, 0.1f});

        ragdoll->body_indices[i] = i;

        /* Default motor strength = 1.0 (animation-dominated). */
        ragdoll->motor_strengths[i] = 1.0f;
        phys_joint_motor_init(&ragdoll->motors[i]);
        ragdoll->motors[i].strength = 1.0f;
        ragdoll->motors[i].max_torque = 1000.0f;

        /* Copy world pose to output. */
        ragdoll->bone_world[i] = world_pose[i];
    }

    /* Create joints between parent-child pairs. */
    ragdoll->joint_count = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t parent = skel->parent_indices[i];
        if (parent == UINT32_MAX || parent >= n) {
            ragdoll->joint_indices[i] = UINT32_MAX;
            continue;
        }

        uint32_t ji = ragdoll->joint_count;
        phys_joint_init(&ragdoll->joints[ji]);
        ragdoll->joints[ji].type = PHYS_JOINT_BALL;
        ragdoll->joints[ji].body_a = parent;
        ragdoll->joints[ji].body_b = i;

        /* Anchor at child bone's position, in each body's local space. */
        vec3_t child_pos = mat4_get_position(&world_pose[i]);
        vec3_t parent_pos = mat4_get_position(&world_pose[parent]);

        /* Local anchor A = child position relative to parent body. */
        ragdoll->joints[ji].local_anchor_a = (phys_vec3_t){
            child_pos.x - parent_pos.x,
            child_pos.y - parent_pos.y,
            child_pos.z - parent_pos.z
        };
        /* Local anchor B = origin of child body. */
        ragdoll->joints[ji].local_anchor_b = (phys_vec3_t){0.f, 0.f, 0.f};

        ragdoll->joint_indices[i] = ji;
        ragdoll->joint_count++;
    }

    return true;
}

void ragdoll_destroy(ragdoll_t *ragdoll) {
    if (!ragdoll) return;
    free(ragdoll->body_indices);
    free(ragdoll->joint_indices);
    free(ragdoll->motor_strengths);
    free(ragdoll->motors);
    free(ragdoll->bodies);
    free(ragdoll->joints);
    free(ragdoll->bone_world);
    memset(ragdoll, 0, sizeof(*ragdoll));
}
