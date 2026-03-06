/**
 * @file ik_ccd.c
 * @brief Cyclic Coordinate Descent IK solver.
 *
 * Non-static functions: 1 (ik_solve_ccd)
 */

#include "ferrum/animation/ik_solver.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/quat.h"
#include <math.h>

/* Extract translation from column-major mat4. */
static vec3_t mat4_get_translation_(const mat4_t *m) {
    return (vec3_t){ m->m[12], m->m[13], m->m[14] };
}

/**
 * @brief Build a rotation matrix that rotates vector 'from' to vector 'to'.
 *
 * Both vectors should be unit length. If they are nearly parallel or
 * anti-parallel, returns identity or 180° rotation respectively.
 */
static mat4_t rotation_between_vectors_(vec3_t from, vec3_t to) {
    float dot = vec3_dot(from, to);

    /* Nearly parallel — no rotation needed. */
    if (dot > 0.9999f) {
        return mat4_identity();
    }

    /* Nearly anti-parallel — rotate 180° around any perpendicular axis. */
    if (dot < -0.9999f) {
        vec3_t perp = { 1.0f, 0.0f, 0.0f };
        if (fabsf(from.x) > 0.9f) {
            perp = (vec3_t){ 0.0f, 1.0f, 0.0f };
        }
        vec3_t axis = vec3_cross(from, perp);
        axis = vec3_normalize_safe(axis, 1e-7f);
        quat_t q = quat_from_axis_angle(axis, 3.14159265f, 1e-7f);
        mat4_t rot;
        quat_to_mat4(q, &rot);
        return rot;
    }

    vec3_t axis = vec3_cross(from, to);
    float angle = acosf(dot);
    axis = vec3_normalize_safe(axis, 1e-7f);
    quat_t q = quat_from_axis_angle(axis, angle, 1e-7f);
    mat4_t rot;
    quat_to_mat4(q, &rot);
    return rot;
}

/**
 * @brief Apply rotation R around pivot point p to a bone's world transform.
 *
 * new_world = T(p) × R × T(-p) × old_world
 */
static void rotate_around_pivot_(mat4_t *bone_world, mat4_t rot, vec3_t pivot) {
    /* Translate bone to pivot origin, rotate, translate back. */
    mat4_t t_neg = mat4_translation(-pivot.x, -pivot.y, -pivot.z);
    mat4_t t_pos = mat4_translation(pivot.x, pivot.y, pivot.z);
    mat4_t temp = mat4_mul(t_neg, *bone_world);
    temp = mat4_mul(rot, temp);
    *bone_world = mat4_mul(t_pos, temp);
}

void ik_solve_ccd(const skeleton_def_t *skel, mat4_t *pose,
                  uint32_t bone_count, uint32_t chain_length,
                  vec3_t target, uint32_t max_iter, float tolerance) {
    if (!skel || !pose || chain_length == 0 || bone_count < 2) return;

    /* Find the tip bone (last bone in the chain).
     * We assume the chain ends at bone (bone_count - 1) and goes back
     * chain_length bones via parent links. chain_length bones = chain_length+1 joints. */
    uint32_t tip = bone_count - 1;

    /* Build chain indices by walking parents from tip.
     * chain_length bones require chain_length+1 joint positions. */
    uint32_t chain[64]; /* max 64 joints in chain */
    uint32_t actual_len = 0;
    uint32_t current = tip;
    for (uint32_t i = 0; i <= chain_length && i < 64; i++) {
        chain[actual_len++] = current;
        if (skel->parent_indices[current] == UINT32_MAX) break;
        if (i < chain_length) current = skel->parent_indices[current];
    }
    /* chain[0] = tip, chain[actual_len-1] = root of IK chain. */

    if (actual_len < 2) return;

    for (uint32_t iter = 0; iter < max_iter; iter++) {
        vec3_t effector = mat4_get_translation_(&pose[tip]);
        float dist = vec3_magnitude(vec3_sub(effector, target));
        if (dist < tolerance) break;

        /* Iterate from tip-1 to root of chain (skip tip itself). */
        for (uint32_t ci = 1; ci < actual_len; ci++) {
            uint32_t bone = chain[ci];
            vec3_t bone_pos = mat4_get_translation_(&pose[bone]);

            /* Vector from bone to current end-effector. */
            effector = mat4_get_translation_(&pose[tip]);
            vec3_t to_effector = vec3_sub(effector, bone_pos);
            vec3_t to_target = vec3_sub(target, bone_pos);

            float len_eff = vec3_magnitude(to_effector);
            float len_tgt = vec3_magnitude(to_target);
            if (len_eff < 1e-6f || len_tgt < 1e-6f) continue;

            to_effector = vec3_scale(to_effector, 1.0f / len_eff);
            to_target = vec3_scale(to_target, 1.0f / len_tgt);

            mat4_t rot = rotation_between_vectors_(to_effector, to_target);

            /* Rotate this bone and all descendants in the chain. */
            for (uint32_t di = 0; di < ci; di++) {
                rotate_around_pivot_(&pose[chain[di]], rot, bone_pos);
            }
            /* Also rotate the current bone. */
            rotate_around_pivot_(&pose[bone], rot, bone_pos);
        }
    }
}
