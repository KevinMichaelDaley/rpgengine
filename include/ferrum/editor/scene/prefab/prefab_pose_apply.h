/**
 * @file prefab_pose_apply.h
 * @brief Apply/restore per-bone rest_local overrides from prefab definition.
 *
 * When a prefab has bone_pose_count > 0, these overrides replace the
 * skeleton's rest_local matrices. This keeps bone edits in the .fpfab
 * file instead of modifying the shared .fskel skeleton.
 *
 * Ownership: does not take ownership of any parameters.
 * Nullability: all pointer params must be non-NULL.
 *
 * Public types: none (0-type rule).
 */
#ifndef FERRUM_EDITOR_SCENE_PREFAB_POSE_APPLY_H
#define FERRUM_EDITOR_SCENE_PREFAB_POSE_APPLY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* Forward declarations. */
struct prefab_def;
struct skeleton_def;

/**
 * @brief Apply bone pose overrides from a prefab to a skeleton.
 *
 * Copies rest_local matrices from prefab_def_t.bone_rest_local[] into
 * the skeleton's rest_local[], then recomputes rest_world[].
 * Only applies if bone_pose_count > 0 and matches skeleton joint_count.
 *
 * @param def   Prefab definition with bone_pose_count and bone_rest_local.
 * @param skel  Mutable skeleton definition (non-NULL).
 * @return true if poses were applied, false if nothing to apply.
 */
bool prefab_pose_apply(const struct prefab_def *def,
                        struct skeleton_def *skel);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_PREFAB_POSE_APPLY_H */
