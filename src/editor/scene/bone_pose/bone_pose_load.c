/**
 * @file bone_pose_load.c
 * @brief Load per-entity bone pose overrides from .bpose files.
 *
 * Called after entity skeleton loading to restore bone pose overrides
 * from disk. Also provides rest_world recomputation from rest_local
 * and the skeleton hierarchy.
 *
 * Non-static functions (2 / 4 limit):
 *   1. bone_pose_load_for_entity
 *   2. bone_pose_recompute_world
 */

#include "ferrum/editor/scene/bone_pose/bone_pose_file.h"
#include "ferrum/editor/scene/bone_pose/bone_pose_store.h"
#include "ferrum/editor/scene/scene_main.h"
#include "ferrum/editor/edit_skeleton_registry.h"
#include "ferrum/entity/entity_attrs.h"
#include "ferrum/math/mat4.h"

#include <stdio.h>
#include <string.h>

/**
 * @brief Recompute rest_world from rest_local using the skeleton hierarchy.
 *
 * rest_world[i] = rest_world[parent] * rest_local[i] for each joint.
 * Root joints (parent == UINT32_MAX) get rest_world = rest_local.
 *
 * @param block   Bone pose block with rest_local to read and rest_world to write.
 * @param skel    Skeleton definition for hierarchy (parent_indices).
 */
void bone_pose_recompute_world(bone_pose_block_t *block,
                                const skeleton_def_t *skel) {
    if (!block || !skel) return;
    uint32_t n = block->bone_count;
    if (n > skel->joint_count) n = skel->joint_count;

    for (uint32_t i = 0; i < n; i++) {
        uint32_t pidx = skel->parent_indices
            ? skel->parent_indices[i] : UINT32_MAX;
        if (pidx != UINT32_MAX && pidx < n) {
            block->rest_world[i] = mat4_mul(block->rest_world[pidx],
                                              block->rest_local[i]);
        } else {
            block->rest_world[i] = block->rest_local[i];
        }
    }
}

/**
 * @brief Load a bone pose override for an entity from its .bpose file.
 *
 * Checks the entity's SCRIPT_KEY_BONE_POSE_PATH attribute, loads the
 * .bpose file, and creates an override in the bone_pose_store.
 *
 * @param ed         Scene editor context (non-NULL).
 * @param entity_id  Entity to load bone pose for.
 * @return true if a .bpose file was loaded, false otherwise.
 */
bool bone_pose_load_for_entity(scene_editor_t *ed, uint32_t entity_id) {
    if (!ed) return false;

    const edit_entity_t *ent = edit_entity_store_get(&ed->entities, entity_id);
    if (!ent || !ent->active) return false;

    /* Check for bone_pose_path attribute. */
    uint8_t pt = 0, ps = 0;
    const void *pv = entity_attrs_get(&ent->attrs,
                                        SCRIPT_KEY_BONE_POSE_PATH, &pt, &ps);
    if (!pv || pt != SCRIPT_ATTR_STR) return false;
    const char *bpose_rel = (const char *)pv;
    if (bpose_rel[0] == '\0') return false;

    /* Look up the entity's skeleton. */
    uint8_t st = 0, ss = 0;
    const void *sv = entity_attrs_get(&ent->attrs,
                                        SCRIPT_KEY_SKEL_PATH, &st, &ss);
    if (!sv || st != SCRIPT_ATTR_STR) return false;
    const char *spath = (const char *)sv;
    const char *fname = spath;
    for (const char *p = spath; *p; p++) {
        if (*p == '/') fname = p + 1;
    }
    const edit_skeleton_entry_t *entry =
        edit_skeleton_registry_get(&ed->skeleton_registry, fname);
    if (!entry || entry->skel.joint_count == 0) return false;

    /* Build full path. */
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s/%s",
             ed->config.asset_dir, bpose_rel);

    /* Ensure a block exists for this entity. */
    bone_pose_block_t *block = bone_pose_store_ensure(
        &ed->bone_poses, entity_id, &entry->skel);
    if (!block) return false;

    /* Load from file. */
    if (!bone_pose_file_read(full_path, block)) {
        fprintf(stderr, "bone_pose_load: failed to read %s\n", full_path);
        return false;
    }

    /* Recompute rest_world from loaded rest_local. */
    bone_pose_recompute_world(block, &entry->skel);
    return true;
}
