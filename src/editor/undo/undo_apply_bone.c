/**
 * @file undo_apply_bone.c
 * @brief Apply bone undo/redo operations locally in the scene editor.
 *
 * Bone transforms are client-side only. The undo snapshot stores two
 * mat4s (128 bytes): [original_rest_local, new_rest_local].
 * Undo restores original, redo restores new. After restoring, calls
 * per_bone_gizmo_apply_drag with zero delta to propagate rest_world.
 *
 * Non-static functions (2 / 4 limit):
 *   edit_undo_apply_bone_inverse
 *   edit_undo_apply_bone_forward
 */

#include "ferrum/editor/undo_apply.h"
#include "ferrum/editor/edit_undo.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_skeleton_registry.h"
#include "ferrum/editor/scene/bone_pose/bone_pose_store.h"
#include "ferrum/editor/scene/scene_gizmo_bone.h"
#include "ferrum/entity/entity_attrs.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/vec3.h"

#include <string.h>

/** @brief Expected snapshot size: 2 × mat4 (128 bytes). */
#define BONE_SNAPSHOT_SIZE (2 * 16 * sizeof(float))

/**
 * @brief Get the mutable skeleton for an entity's bone, plus the
 *        specific rest_local pointer to overwrite.
 */
static skeleton_def_t *get_skeleton_for_bone_(
    edit_entity_store_t *entities,
    edit_skeleton_registry_t *skel_reg,
    bone_pose_store_t *bone_poses,
    bool is_prefab,
    uint32_t entity_id,
    uint32_t bone_index,
    mat4_t **out_rest_local) {
    const edit_entity_t *ent = edit_entity_store_get(entities, entity_id);
    if (!ent || !ent->active) return NULL;

    uint8_t at = 0, as = 0;
    const void *sp = entity_attrs_get(&ent->attrs, SCRIPT_KEY_SKEL_PATH,
                                       &at, &as);
    if (!sp || at != SCRIPT_ATTR_STR) return NULL;
    const char *spath = (const char *)sp;
    const char *fname = spath;
    for (const char *p = spath; *p; p++) {
        if (*p == '/') fname = p + 1;
    }

    edit_skeleton_entry_t *entry =
        edit_skeleton_registry_get_mut(skel_reg, fname);
    if (!entry || bone_index >= entry->skel.joint_count) return NULL;

    /* In regular mode, use per-entity bone pose overrides.
     * The pose_view trick: copy the skeleton_def, swap in pose pointers. */
    if (!is_prefab && bone_poses) {
        bone_pose_block_t *pose =
            bone_pose_store_get_mut(bone_poses, entity_id);
        if (!pose) {
            pose = bone_pose_store_ensure(bone_poses, entity_id,
                                            &entry->skel);
        }
        if (pose) {
            /* We need a skeleton_def that points to the pose's arrays.
             * Temporarily swap the entry's pointers for propagation. */
            entry->skel.rest_local = pose->rest_local;
            entry->skel.rest_world = pose->rest_world;
            entry->skel.tail_positions = pose->tail_positions;
            if (out_rest_local) *out_rest_local = &pose->rest_local[bone_index];
            return &entry->skel;
        }
    }

    if (out_rest_local) *out_rest_local = &entry->skel.rest_local[bone_index];
    return &entry->skel;
}

/**
 * @brief Apply bone undo or redo by restoring rest_local from snapshot.
 *
 * @param offset  0 for inverse (restore original), 16 for forward (restore new).
 */
static bool apply_bone_(
    edit_entity_store_t *entities,
    edit_skeleton_registry_t *skel_reg,
    bone_pose_store_t *bone_poses,
    bool is_prefab,
    const edit_undo_entry_t *entry,
    uint32_t float_offset) {
    if (!entities || !skel_reg || !entry) return false;
    if (!entry->snapshot_data || entry->snapshot_size < BONE_SNAPSHOT_SIZE) {
        return false;
    }

    mat4_t *rl = NULL;
    skeleton_def_t *sk = get_skeleton_for_bone_(
        entities, skel_reg, bone_poses, is_prefab,
        entry->entity_id, entry->sub_index, &rl);
    if (!rl || !sk) return false;

    /* Restore rest_local from snapshot. */
    const float *snap = (const float *)entry->snapshot_data;
    memcpy(rl->m, snap + float_offset, 16 * sizeof(float));

    /* Propagate rest_world + tail_positions by calling apply_drag with
     * zero delta. This runs propagate_rest_world without modifying
     * rest_local (delta is zero). */
    per_bone_gizmo_apply_drag(sk, entry->sub_index, (vec3_t){0, 0, 0});

    return true;
}

bool edit_undo_apply_bone_inverse(
    edit_entity_store_t *entities,
    edit_skeleton_registry_t *skel_reg,
    bone_pose_store_t *bone_poses,
    bool is_prefab,
    const edit_undo_entry_t *entry) {
    return apply_bone_(entities, skel_reg, bone_poses, is_prefab, entry, 0);
}

bool edit_undo_apply_bone_forward(
    edit_entity_store_t *entities,
    edit_skeleton_registry_t *skel_reg,
    bone_pose_store_t *bone_poses,
    bool is_prefab,
    const edit_undo_entry_t *entry) {
    return apply_bone_(entities, skel_reg, bone_poses, is_prefab, entry, 16);
}
