/**
 * @file bone_pose_load.h
 * @brief Load per-entity bone pose overrides and recompute rest_world.
 *
 * Public types: none (0 / 2-type rule).
 */
#ifndef FERRUM_EDITOR_SCENE_BONE_POSE_LOAD_H
#define FERRUM_EDITOR_SCENE_BONE_POSE_LOAD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* Forward declarations. */
struct scene_editor;
struct bone_pose_block;
struct skeleton_def;

/**
 * @brief Load a bone pose override for an entity from its .bpose file.
 *
 * @param ed         Scene editor context (non-NULL).
 * @param entity_id  Entity to load bone pose for.
 * @return true if a .bpose file was loaded.
 */
bool bone_pose_load_for_entity(struct scene_editor *ed, uint32_t entity_id);

/**
 * @brief Recompute rest_world from rest_local using the skeleton hierarchy.
 *
 * @param block  Bone pose block (non-NULL).
 * @param skel   Skeleton definition for hierarchy (non-NULL).
 */
void bone_pose_recompute_world(struct bone_pose_block *block,
                                const struct skeleton_def *skel);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_BONE_POSE_LOAD_H */
