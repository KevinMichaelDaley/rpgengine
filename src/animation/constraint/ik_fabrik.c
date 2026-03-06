/**
 * @file ik_fabrik.c
 * @brief Forward And Backward Reaching Inverse Kinematics (FABRIK) solver.
 *
 * Non-static functions: 1 (ik_solve_fabrik)
 */

#include "ferrum/animation/ik_solver.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/mat4.h"
#include <math.h>

/* Extract translation from column-major mat4. */
static vec3_t mat4_get_translation_(const mat4_t *m) {
    return (vec3_t){ m->m[12], m->m[13], m->m[14] };
}

/* Set translation in column-major mat4. */
static void mat4_set_translation_(mat4_t *m, vec3_t pos) {
    m->m[12] = pos.x;
    m->m[13] = pos.y;
    m->m[14] = pos.z;
}

void ik_solve_fabrik(const skeleton_def_t *skel, mat4_t *pose,
                     uint32_t tip_bone_idx, uint32_t chain_length,
                     vec3_t target, uint32_t max_iter, float tolerance) {
    if (!skel || !pose || chain_length == 0) return;

    uint32_t tip = tip_bone_idx;

    /* Build chain indices by walking parents from tip.
     * chain_length bones require chain_length+1 joint positions. */
    uint32_t chain[64];
    uint32_t actual_len = 0;
    uint32_t current = tip;
    for (uint32_t i = 0; i <= chain_length && i < 64; i++) {
        chain[actual_len++] = current;
        if (skel->parent_indices[current] == UINT32_MAX) break;
        if (i < chain_length) current = skel->parent_indices[current];
    }
    /* chain[0] = tip, chain[actual_len-1] = root of IK chain. */

    if (actual_len < 2) return;

    /* Precompute bone segment lengths from rest pose. */
    float lengths[64];
    for (uint32_t i = 0; i < actual_len - 1; i++) {
        vec3_t a = mat4_get_translation_(&skel->rest_world[chain[i + 1]]);
        vec3_t b = mat4_get_translation_(&skel->rest_world[chain[i]]);
        lengths[i] = vec3_magnitude(vec3_sub(b, a));
        if (lengths[i] < 1e-6f) lengths[i] = 1e-6f;
    }

    /* Extract initial joint positions. */
    vec3_t positions[64];
    for (uint32_t i = 0; i < actual_len; i++) {
        positions[i] = mat4_get_translation_(&pose[chain[i]]);
    }

    /* Root position (fixed). */
    vec3_t root_pos = positions[actual_len - 1];

    /* Check total reach. */
    float total_reach = 0.0f;
    for (uint32_t i = 0; i < actual_len - 1; i++) {
        total_reach += lengths[i];
    }
    float dist_to_target = vec3_magnitude(vec3_sub(target, root_pos));

    if (dist_to_target > total_reach) {
        /* Unreachable — fully extend toward target. */
        vec3_t dir = vec3_sub(target, root_pos);
        float mag = vec3_magnitude(dir);
        if (mag < 1e-6f) return;
        dir = vec3_scale(dir, 1.0f / mag);

        positions[actual_len - 1] = root_pos;
        for (int i = (int)actual_len - 2; i >= 0; i--) {
            positions[i] = vec3_add(positions[i + 1],
                                    vec3_scale(dir, lengths[i]));
        }
    } else {
        /* Iterative FABRIK. */
        for (uint32_t iter = 0; iter < max_iter; iter++) {
            float effector_dist = vec3_magnitude(
                vec3_sub(positions[0], target));
            if (effector_dist < tolerance) break;

            /* Forward pass: tip → root (pull toward target). */
            positions[0] = target;
            for (uint32_t i = 1; i < actual_len; i++) {
                vec3_t dir = vec3_sub(positions[i], positions[i - 1]);
                float mag = vec3_magnitude(dir);
                if (mag < 1e-6f) {
                    dir = (vec3_t){ 0.0f, 1.0f, 0.0f };
                    mag = 1.0f;
                }
                dir = vec3_scale(dir, lengths[i - 1] / mag);
                positions[i] = vec3_add(positions[i - 1], dir);
            }

            /* Backward pass: root → tip (restore root position). */
            positions[actual_len - 1] = root_pos;
            for (int i = (int)actual_len - 2; i >= 0; i--) {
                vec3_t dir = vec3_sub(positions[i], positions[i + 1]);
                float mag = vec3_magnitude(dir);
                if (mag < 1e-6f) {
                    dir = (vec3_t){ 0.0f, 1.0f, 0.0f };
                    mag = 1.0f;
                }
                dir = vec3_scale(dir, lengths[i] / mag);
                positions[i] = vec3_add(positions[i + 1], dir);
            }
        }
    }

    /* Write solved positions back to pose matrices. */
    for (uint32_t i = 0; i < actual_len; i++) {
        mat4_set_translation_(&pose[chain[i]], positions[i]);
    }
}
