/**
 * @file undo_apply.h
 * @brief Apply undo/redo operations to editor entities.
 *
 * Given an undo entry, applies either the inverse (for undo) or forward
 * (for redo) operation to the entity store. Notifies the physics bridge
 * of any transform or lifecycle changes.
 *
 * Thread safety: must be called from the main tick thread only.
 */
#ifndef FERRUM_EDITOR_UNDO_APPLY_H
#define FERRUM_EDITOR_UNDO_APPLY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/* Forward declarations. */
struct edit_cmd_ctx;
struct edit_undo_entry;

/**
 * @brief Apply the inverse operation of an undo entry.
 *
 * Used when undoing: reverses the effect of the original command.
 * - MOVE inverse: adds delta to entity position.
 * - ROTATE inverse: composes delta quaternion with entity orientation.
 * - SCALE inverse: multiplies entity scale by delta factors.
 * - SPAWN inverse (=DELETE): removes the entity.
 * - DELETE inverse (=SPAWN): restores entity from snapshot.
 *
 * @param ctx    Command context (entities, bridge, etc.). Must not be NULL.
 * @param entry  Undo entry containing inverse_type and delta/snapshot. Must not be NULL.
 * @return true on success, false if entity not found or params NULL.
 */
bool edit_undo_apply_inverse(struct edit_cmd_ctx *ctx,
                              const struct edit_undo_entry *entry);

/**
 * @brief Apply the forward operation of an undo entry.
 *
 * Used when redoing: re-applies the original command.
 * The forward delta is the negation of the inverse delta for transforms,
 * or the opposite lifecycle operation for spawn/delete.
 *
 * @param ctx    Command context. Must not be NULL.
 * @param entry  Undo entry containing forward_type and delta/snapshot. Must not be NULL.
 * @return true on success, false if entity not found or params NULL.
 */
bool edit_undo_apply_forward(struct edit_cmd_ctx *ctx,
                              const struct edit_undo_entry *entry);

/* ------------------------------------------------------------------------ */
/* Bone-specific undo/redo (client-side only)                                */
/* ------------------------------------------------------------------------ */

/* Forward declarations. */
struct edit_entity_store;
struct edit_skeleton_registry;
struct bone_pose_store;

/**
 * @brief Apply inverse of a bone transform undo entry.
 *
 * Restores the bone's rest_local to the original value saved in the
 * snapshot, then propagates the hierarchy.
 *
 * @param entities   Entity store.
 * @param skel_reg   Skeleton registry.
 * @param bone_poses Per-entity bone pose store (NULL in prefab mode).
 * @param is_prefab  True if operating in prefab mode.
 * @param entry      Undo entry with BONE_MOVE or BONE_ROTATE type.
 * @return true on success.
 */
bool edit_undo_apply_bone_inverse(
    struct edit_entity_store *entities,
    struct edit_skeleton_registry *skel_reg,
    struct bone_pose_store *bone_poses,
    bool is_prefab,
    const struct edit_undo_entry *entry);

/**
 * @brief Apply forward of a bone transform undo entry (redo).
 *
 * Restores the bone's rest_local to the new value saved in the snapshot.
 */
bool edit_undo_apply_bone_forward(
    struct edit_entity_store *entities,
    struct edit_skeleton_registry *skel_reg,
    struct bone_pose_store *bone_poses,
    bool is_prefab,
    const struct edit_undo_entry *entry);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_UNDO_APPLY_H */
