/**
 * @file skeleton_builder.h
 * @brief Dynamic bone add/remove for skeleton editing.
 *
 * Since skeleton_def_t arrays are fixed-size at creation, adding or
 * removing bones creates a new skeleton with the adjusted joint count,
 * copies data, and replaces the registry entry. This is acceptable
 * because bone creation is a user-initiated action, not per-frame.
 *
 * Thread safety: must be called from the main tick thread only.
 */
#ifndef FERRUM_EDITOR_ANIM_SKELETON_BUILDER_H
#define FERRUM_EDITOR_ANIM_SKELETON_BUILDER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "ferrum/math/vec3.h"
#include "ferrum/math/mat4.h"

/* Forward declarations. */
struct edit_skeleton_registry;
struct skeleton_def;

/**
 * @brief Add a bone to a skeleton in the registry.
 *
 * Creates a new skeleton with joint_count+1, copies existing bones,
 * initializes the new bone with the given name/parent/positions,
 * recomputes rest_world, and swaps the registry entry.
 *
 * @param reg         Skeleton registry (non-NULL).
 * @param skel_path   Registry path key (e.g., "humanoid.fskel").
 * @param name        Bone name (max 63 chars).
 * @param parent_idx  Parent bone index (UINT32_MAX for root).
 * @param head_world  World-space head position.
 * @param tail_world  World-space tail position.
 * @return New bone index, or UINT32_MAX on failure.
 */
uint32_t skeleton_builder_add_bone(
    struct edit_skeleton_registry *reg,
    const char *skel_path,
    const char *name,
    uint32_t parent_idx,
    vec3_t head_world,
    vec3_t tail_world);

/**
 * @brief Remove a bone from a skeleton in the registry.
 *
 * Creates a new skeleton with joint_count-1, remaps parent indices,
 * reparents children of the removed bone to its parent.
 *
 * @param reg         Skeleton registry.
 * @param skel_path   Registry path key.
 * @param bone_idx    Index of bone to remove.
 * @return true on success.
 */
bool skeleton_builder_remove_bone(
    struct edit_skeleton_registry *reg,
    const char *skel_path,
    uint32_t bone_idx);

/**
 * @brief Change a bone's parent.
 *
 * @param reg         Skeleton registry.
 * @param skel_path   Registry path key.
 * @param bone_idx    Bone to reparent.
 * @param new_parent  New parent index (UINT32_MAX for root).
 * @return true on success.
 */
bool skeleton_builder_set_parent(
    struct edit_skeleton_registry *reg,
    const char *skel_path,
    uint32_t bone_idx,
    uint32_t new_parent);

/**
 * @brief Add a bone directly to a skeleton_def_t (destroys and replaces it).
 *
 * The skeleton is destroyed and recreated with joint_count+1. The caller's
 * pointer remains valid (the struct is modified in place).
 *
 * @return New bone index, or UINT32_MAX on failure.
 */
uint32_t skeleton_builder_add_bone_direct(
    struct skeleton_def *skel,
    const char *name,
    uint32_t parent_idx,
    vec3_t head_world,
    vec3_t tail_world);

/**
 * @brief Remove a bone directly from a skeleton_def_t (destroys and replaces it).
 * @return true on success.
 */
bool skeleton_builder_remove_bone_direct(
    struct skeleton_def *skel,
    uint32_t bone_idx);

/**
 * @brief Recompute rest_world from rest_local and parent hierarchy.
 *
 * Must be called after modifying rest_local or parent_indices.
 * Requires parent indices to be topologically sorted (parent < child).
 *
 * @param skel  Skeleton to update (non-NULL).
 */
void skeleton_builder_recompute_world(struct skeleton_def *skel);

/**
 * @brief Compute rest_local from world-space head and tail positions.
 *
 * @param head_world       World-space head position.
 * @param tail_world       World-space tail position.
 * @param parent_world_inv Inverse of parent bone's rest_world (identity for root).
 * @return Local-space transform matrix.
 */
mat4_t skeleton_builder_rest_local_from_head_tail(
    vec3_t head_world,
    vec3_t tail_world,
    const mat4_t *parent_world_inv);

/**
 * @brief Recompute tail_positions from rest_world and bone geometry.
 *
 * Sets tail_positions[i] = rest_world[i] * (0, bone_length, 0).
 * Allocates tail_positions if NULL.
 *
 * @param skel  Skeleton to update.
 */
void skeleton_builder_update_tail_positions(struct skeleton_def *skel);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_ANIM_SKELETON_BUILDER_H */
