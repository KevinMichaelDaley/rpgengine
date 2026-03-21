/**
 * @file skeleton_builder_direct.c
 * @brief Add/remove bones directly on a skeleton_def_t (for working copies).
 *
 * Non-static functions (2 / 4 limit):
 *   skeleton_builder_add_bone_direct
 *   skeleton_builder_remove_bone_direct
 */

#include "ferrum/editor/anim/skeleton_builder.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/math/mat4.h"

#include <stdlib.h>
#include <string.h>

uint32_t skeleton_builder_add_bone_direct(
    skeleton_def_t *skel,
    const char *name,
    uint32_t parent_idx,
    vec3_t head_world,
    vec3_t tail_world) {
    if (!skel || !name) return UINT32_MAX;

    uint32_t old_count = skel->joint_count;
    if (parent_idx != UINT32_MAX && parent_idx >= old_count) return UINT32_MAX;

    uint32_t new_count = old_count + 1;
    uint32_t new_idx = old_count;

    /* Create new skeleton. */
    skeleton_def_t new_skel;
    if (!skeleton_def_init(&new_skel, new_count, 0)) return UINT32_MAX;

    /* Copy existing data. */
    if (old_count > 0) {
        if (skel->joint_names)
            memcpy(new_skel.joint_names, skel->joint_names,
                   old_count * SKELETON_JOINT_NAME_MAX);
        if (skel->parent_indices)
            memcpy(new_skel.parent_indices, skel->parent_indices,
                   old_count * sizeof(uint32_t));
        if (skel->rest_local)
            memcpy(new_skel.rest_local, skel->rest_local,
                   old_count * sizeof(mat4_t));
        if (skel->rest_world)
            memcpy(new_skel.rest_world, skel->rest_world,
                   old_count * sizeof(mat4_t));
    }

    /* Initialize new bone. */
    strncpy(new_skel.joint_names[new_idx], name, SKELETON_JOINT_NAME_MAX - 1);
    new_skel.joint_names[new_idx][SKELETON_JOINT_NAME_MAX - 1] = '\0';
    new_skel.parent_indices[new_idx] = parent_idx;

    /* Compute rest_local. */
    mat4_t parent_inv = mat4_identity();
    if (parent_idx != UINT32_MAX && parent_idx < old_count) {
        mat4_inverse(new_skel.rest_world[parent_idx], &parent_inv);
    }
    new_skel.rest_local[new_idx] = skeleton_builder_rest_local_from_head_tail(
        head_world, tail_world, &parent_inv);

    skeleton_builder_recompute_world(&new_skel);

    /* Copy/create tail_positions. */
    new_skel.tail_positions = (float *)calloc(new_count * 3, sizeof(float));
    if (new_skel.tail_positions) {
        if (skel->tail_positions && old_count > 0) {
            memcpy(new_skel.tail_positions, skel->tail_positions,
                   old_count * 3 * sizeof(float));
        }
        new_skel.tail_positions[new_idx * 3 + 0] = tail_world.x;
        new_skel.tail_positions[new_idx * 3 + 1] = tail_world.y;
        new_skel.tail_positions[new_idx * 3 + 2] = tail_world.z;
    }

    /* Destroy old and replace. */
    skeleton_def_destroy(skel);
    *skel = new_skel;

    return new_idx;
}

bool skeleton_builder_remove_bone_direct(
    skeleton_def_t *skel,
    uint32_t bone_idx) {
    if (!skel || bone_idx >= skel->joint_count) return false;
    if (skel->joint_count <= 1) return false;

    uint32_t old_count = skel->joint_count;
    uint32_t new_count = old_count - 1;
    uint32_t removed_parent = skel->parent_indices[bone_idx];

    skeleton_def_t new_skel;
    if (!skeleton_def_init(&new_skel, new_count, 0)) return false;

    /* Build remap. */
    uint32_t remap[256];
    if (old_count > 256) { skeleton_def_destroy(&new_skel); return false; }
    uint32_t wi = 0;
    for (uint32_t i = 0; i < old_count; i++) {
        remap[i] = (i == bone_idx) ? UINT32_MAX : wi++;
    }

    /* Copy, remapping parents. */
    wi = 0;
    for (uint32_t i = 0; i < old_count; i++) {
        if (i == bone_idx) continue;
        memcpy(new_skel.joint_names[wi], skel->joint_names[i],
               SKELETON_JOINT_NAME_MAX);
        new_skel.rest_local[wi] = skel->rest_local[i];
        uint32_t op = skel->parent_indices[i];
        if (op == bone_idx) {
            new_skel.parent_indices[wi] =
                (removed_parent == UINT32_MAX) ? UINT32_MAX
                                                : remap[removed_parent];
        } else if (op == UINT32_MAX) {
            new_skel.parent_indices[wi] = UINT32_MAX;
        } else {
            new_skel.parent_indices[wi] = remap[op];
        }
        wi++;
    }

    skeleton_builder_recompute_world(&new_skel);

    /* Copy tail_positions. */
    if (skel->tail_positions) {
        new_skel.tail_positions = (float *)calloc(new_count * 3, sizeof(float));
        if (new_skel.tail_positions) {
            wi = 0;
            for (uint32_t i = 0; i < old_count; i++) {
                if (i == bone_idx) continue;
                new_skel.tail_positions[wi * 3 + 0] = skel->tail_positions[i * 3 + 0];
                new_skel.tail_positions[wi * 3 + 1] = skel->tail_positions[i * 3 + 1];
                new_skel.tail_positions[wi * 3 + 2] = skel->tail_positions[i * 3 + 2];
                wi++;
            }
        }
    }

    skeleton_def_destroy(skel);
    *skel = new_skel;
    return true;
}
