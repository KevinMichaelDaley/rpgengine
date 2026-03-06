/**
 * @file anim_constraint_rows.h
 * @brief Map animation constraints to physics joints for XPBD solving.
 *
 * Converts animation constraint_def_t entries and bone joint descriptors
 * into phys_joint_t entries that plug into the physics tick pipeline.
 * Supported mappings:
 *
 *   Copy Location → PHYS_JOINT_BALL  (3 positional rows)
 *   Child Of      → PHYS_JOINT_BALL  (3 positional rows)
 *   IK            → PHYS_JOINT_BALL  (end-effector to target)
 *   Joint desc    → BALL / HINGE / DISTANCE (from bone_joint_desc_t)
 *
 * Ownership: all arrays are caller-owned.  No allocations performed.
 */
#ifndef FERRUM_ANIMATION_ANIM_CONSTRAINT_ROWS_H
#define FERRUM_ANIMATION_ANIM_CONSTRAINT_ROWS_H

#include "ferrum/animation/constraint_params.h"
#include "ferrum/math/mat4.h"
#include "ferrum/physics/joint.h"
#include <stdint.h>

/**
 * @brief Build physics joints from animation constraints.
 *
 * Iterates the skeleton's constraint definitions and produces
 * phys_joint_t entries for each constraint that maps to a bilateral
 * joint.  Joint body indices use the bone_to_body_map to translate
 * bone indices to body pool slots.
 *
 * @param skel             Skeleton definition with constraints.
 * @param world_pose       Current bone world-space matrices.
 * @param bone_to_body_map Maps bone index → body pool index.
 *                         UINT32_MAX entries are skipped.
 * @param out_joints       Output joint array (written).
 * @param max_joints       Capacity of out_joints.
 * @return Number of joints written.
 *
 * @note Only processes constraints with valid target_bone_idx.
 * @note Does NOT call phys_joint_build_* — that happens in the tick.
 */
uint32_t anim_constraints_to_joints(
    const skeleton_def_t *skel,
    const mat4_t *world_pose,
    const uint32_t *bone_to_body_map,
    phys_joint_t *out_joints,
    uint32_t max_joints);

/**
 * @brief Build physics joints from bone joint descriptors (fskel JNTS).
 *
 * Converts per-bone bone_joint_desc_t entries into phys_joint_t for
 * parent-child bone pairs.  Joint anchors are computed from world poses.
 *
 * @param skel             Skeleton definition with joint descriptors.
 * @param world_pose       Current bone world-space matrices.
 * @param bone_to_body_map Maps bone index → body pool index.
 * @param out_joints       Output joint array (written).
 * @param max_joints       Capacity of out_joints.
 * @return Number of joints written.
 */
uint32_t anim_joint_descs_to_joints(
    const skeleton_def_t *skel,
    const mat4_t *world_pose,
    const uint32_t *bone_to_body_map,
    phys_joint_t *out_joints,
    uint32_t max_joints);

#endif /* FERRUM_ANIMATION_ANIM_CONSTRAINT_ROWS_H */
