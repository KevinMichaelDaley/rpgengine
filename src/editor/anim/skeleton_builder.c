/**
 * @file skeleton_builder.c
 * @brief Dynamic bone add/remove by recreating skeleton_def_t.
 *
 * Non-static functions (4 / 4 limit):
 *   skeleton_builder_add_bone
 *   skeleton_builder_remove_bone
 *   skeleton_builder_set_parent
 *   skeleton_builder_recompute_world
 */

#include "ferrum/editor/anim/skeleton_builder.h"
#include "ferrum/editor/edit_skeleton_registry.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/vec3.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

/** @brief Allocate and copy tail_positions for a skeleton. */
static void ensure_tail_positions_(skeleton_def_t *skel) {
    if (skel->tail_positions || skel->joint_count == 0) return;
    skel->tail_positions = (float *)calloc(skel->joint_count * 3,
                                             sizeof(float));
}

void skeleton_builder_recompute_world(skeleton_def_t *skel) {
    if (!skel || !skel->rest_local || !skel->rest_world) return;

    for (uint32_t i = 0; i < skel->joint_count; i++) {
        uint32_t parent = skel->parent_indices
            ? skel->parent_indices[i] : UINT32_MAX;
        if (parent == UINT32_MAX || parent >= skel->joint_count) {
            skel->rest_world[i] = skel->rest_local[i];
        } else {
            skel->rest_world[i] = mat4_mul(skel->rest_world[parent],
                                            skel->rest_local[i]);
        }
    }
}

uint32_t skeleton_builder_add_bone(
    edit_skeleton_registry_t *reg,
    const char *skel_path,
    const char *name,
    uint32_t parent_idx,
    vec3_t head_world,
    vec3_t tail_world) {
    if (!reg || !skel_path || !name) return UINT32_MAX;

    const edit_skeleton_entry_t *old_entry =
        edit_skeleton_registry_get(reg, skel_path);

    uint32_t old_count = 0;
    const skeleton_def_t *old_skel = NULL;
    if (old_entry) {
        old_skel = &old_entry->skel;
        old_count = old_skel->joint_count;
    }

    /* Validate parent. */
    if (parent_idx != UINT32_MAX && parent_idx >= old_count) {
        return UINT32_MAX;
    }

    uint32_t new_count = old_count + 1;
    uint32_t new_idx = old_count;

    /* Create new skeleton. */
    skeleton_def_t new_skel;
    if (!skeleton_def_init(&new_skel, new_count, 0)) {
        return UINT32_MAX;
    }

    /* Copy existing bones. */
    if (old_skel && old_count > 0) {
        memcpy(new_skel.joint_names, old_skel->joint_names,
               old_count * SKELETON_JOINT_NAME_MAX);
        memcpy(new_skel.parent_indices, old_skel->parent_indices,
               old_count * sizeof(uint32_t));
        memcpy(new_skel.rest_local, old_skel->rest_local,
               old_count * sizeof(mat4_t));
        memcpy(new_skel.rest_world, old_skel->rest_world,
               old_count * sizeof(mat4_t));
    }

    /* Initialize new bone. */
    strncpy(new_skel.joint_names[new_idx], name,
            SKELETON_JOINT_NAME_MAX - 1);
    new_skel.joint_names[new_idx][SKELETON_JOINT_NAME_MAX - 1] = '\0';
    new_skel.parent_indices[new_idx] = parent_idx;

    /* Compute rest_local from head/tail world positions. */
    mat4_t parent_world_inv = mat4_identity();
    if (parent_idx != UINT32_MAX && parent_idx < old_count) {
        mat4_inverse(new_skel.rest_world[parent_idx], &parent_world_inv);
    }
    new_skel.rest_local[new_idx] = skeleton_builder_rest_local_from_head_tail(
        head_world, tail_world, &parent_world_inv);

    /* Recompute rest_world. */
    skeleton_builder_recompute_world(&new_skel);

    /* Copy tail_positions if they exist, and add new one. */
    ensure_tail_positions_(&new_skel);
    if (old_skel && old_skel->tail_positions && new_skel.tail_positions) {
        memcpy(new_skel.tail_positions, old_skel->tail_positions,
               old_count * 3 * sizeof(float));
    }
    if (new_skel.tail_positions) {
        new_skel.tail_positions[new_idx * 3 + 0] = tail_world.x;
        new_skel.tail_positions[new_idx * 3 + 1] = tail_world.y;
        new_skel.tail_positions[new_idx * 3 + 2] = tail_world.z;
    }

    /* Copy IBMs if they exist. */
    mat4_t *new_ibms = NULL;
    uint32_t ibm_count = 0;
    if (old_entry && old_entry->ibms && old_entry->ibm_count > 0) {
        ibm_count = new_count;
        new_ibms = (mat4_t *)calloc(ibm_count, sizeof(mat4_t));
        if (new_ibms) {
            memcpy(new_ibms, old_entry->ibms,
                   old_entry->ibm_count * sizeof(mat4_t));
            /* New bone IBM = inverse of rest_world. */
            mat4_inverse(new_skel.rest_world[new_idx], &new_ibms[new_idx]);
        }
    }

    /* Swap in registry (destroys old skeleton). */
    edit_skeleton_registry_add(reg, skel_path, &new_skel,
                                new_ibms, ibm_count);
    free(new_ibms);

    return new_idx;
}

bool skeleton_builder_remove_bone(
    edit_skeleton_registry_t *reg,
    const char *skel_path,
    uint32_t bone_idx) {
    if (!reg || !skel_path) return false;

    const edit_skeleton_entry_t *old_entry =
        edit_skeleton_registry_get(reg, skel_path);
    if (!old_entry) return false;

    const skeleton_def_t *old = &old_entry->skel;
    if (bone_idx >= old->joint_count) return false;
    if (old->joint_count <= 1) {
        /* Can't remove the last bone — would leave empty skeleton. */
        return false;
    }

    uint32_t new_count = old->joint_count - 1;
    uint32_t removed_parent = old->parent_indices[bone_idx];

    skeleton_def_t new_skel;
    if (!skeleton_def_init(&new_skel, new_count, 0)) return false;

    /* Build index remap: old index → new index. */
    uint32_t remap[256]; /* Stack-safe: max 256 bones. */
    if (old->joint_count > 256) {
        skeleton_def_destroy(&new_skel);
        return false;
    }

    uint32_t wi = 0;
    for (uint32_t i = 0; i < old->joint_count; i++) {
        if (i == bone_idx) {
            remap[i] = UINT32_MAX; /* Removed. */
        } else {
            remap[i] = wi++;
        }
    }

    /* Copy bones, remapping parents. */
    wi = 0;
    for (uint32_t i = 0; i < old->joint_count; i++) {
        if (i == bone_idx) continue;

        memcpy(new_skel.joint_names[wi], old->joint_names[i],
               SKELETON_JOINT_NAME_MAX);
        memcpy(&new_skel.rest_local[wi], &old->rest_local[i],
               sizeof(mat4_t));

        /* Remap parent index. */
        uint32_t old_parent = old->parent_indices[i];
        if (old_parent == bone_idx) {
            /* Children of removed bone → reparent to removed bone's parent. */
            new_skel.parent_indices[wi] =
                (removed_parent == UINT32_MAX) ? UINT32_MAX
                                                : remap[removed_parent];
        } else if (old_parent == UINT32_MAX) {
            new_skel.parent_indices[wi] = UINT32_MAX;
        } else {
            new_skel.parent_indices[wi] = remap[old_parent];
        }

        wi++;
    }

    /* Recompute rest_world. */
    skeleton_builder_recompute_world(&new_skel);

    /* Copy tail_positions. */
    if (old->tail_positions) {
        ensure_tail_positions_(&new_skel);
        if (new_skel.tail_positions) {
            wi = 0;
            for (uint32_t i = 0; i < old->joint_count; i++) {
                if (i == bone_idx) continue;
                new_skel.tail_positions[wi * 3 + 0] =
                    old->tail_positions[i * 3 + 0];
                new_skel.tail_positions[wi * 3 + 1] =
                    old->tail_positions[i * 3 + 1];
                new_skel.tail_positions[wi * 3 + 2] =
                    old->tail_positions[i * 3 + 2];
                wi++;
            }
        }
    }

    /* Copy/remap IBMs. */
    mat4_t *new_ibms = NULL;
    uint32_t ibm_count = 0;
    if (old_entry->ibms && old_entry->ibm_count > 0) {
        ibm_count = new_count;
        new_ibms = (mat4_t *)calloc(ibm_count, sizeof(mat4_t));
        if (new_ibms) {
            wi = 0;
            for (uint32_t i = 0; i < old_entry->ibm_count && i < old->joint_count; i++) {
                if (i == bone_idx) continue;
                new_ibms[wi++] = old_entry->ibms[i];
            }
        }
    }

    edit_skeleton_registry_add(reg, skel_path, &new_skel,
                                new_ibms, ibm_count);
    free(new_ibms);
    return true;
}

bool skeleton_builder_set_parent(
    edit_skeleton_registry_t *reg,
    const char *skel_path,
    uint32_t bone_idx,
    uint32_t new_parent) {
    if (!reg || !skel_path) return false;

    edit_skeleton_entry_t *entry =
        edit_skeleton_registry_get_mut(reg, skel_path);
    if (!entry) return false;

    skeleton_def_t *skel = &entry->skel;
    if (bone_idx >= skel->joint_count) return false;
    if (new_parent != UINT32_MAX && new_parent >= skel->joint_count) return false;
    if (bone_idx == new_parent) return false;

    skel->parent_indices[bone_idx] = new_parent;
    skeleton_builder_recompute_world(skel);
    return true;
}
