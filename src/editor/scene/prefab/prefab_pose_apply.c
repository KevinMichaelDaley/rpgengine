/**
 * @file prefab_pose_apply.c
 * @brief Apply per-bone rest_local overrides from prefab definition.
 *
 * Copies rest_local matrices from prefab bone_rest_local[] into the
 * skeleton_def_t, then recomputes rest_world[] by walking the hierarchy.
 *
 * Non-static functions: prefab_pose_apply (1/4).
 */

#include "ferrum/editor/scene/prefab/prefab_pose_apply.h"
#include "ferrum/editor/scene/prefab/prefab_def.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/math/mat4.h"

#include <string.h>

/**
 * @brief Recompute rest_world[] from rest_local[] and parent hierarchy.
 *
 * For each bone: if parent is UINT32_MAX (root), rest_world = rest_local.
 * Otherwise rest_world = parent_rest_world * rest_local.
 */
static void recompute_rest_world(skeleton_def_t *skel) {
    for (uint32_t i = 0; i < skel->joint_count; i++) {
        uint32_t parent = skel->parent_indices[i];
        if (parent == UINT32_MAX || parent >= skel->joint_count) {
            skel->rest_world[i] = skel->rest_local[i];
        } else {
            skel->rest_world[i] = mat4_mul(skel->rest_world[parent],
                                            skel->rest_local[i]);
        }
    }
}

bool prefab_pose_apply(const struct prefab_def *def,
                        struct skeleton_def *skel) {
    if (!def || !skel) return false;
    if (def->bone_pose_count == 0) return false;

    /* Apply only if counts match (avoid partial overwrites). */
    uint32_t count = def->bone_pose_count;
    if (count > skel->joint_count) count = skel->joint_count;

    for (uint32_t i = 0; i < count; i++) {
        memcpy(skel->rest_local[i].m, def->bone_rest_local[i],
               16 * sizeof(float));
    }

    recompute_rest_world(skel);
    return true;
}
