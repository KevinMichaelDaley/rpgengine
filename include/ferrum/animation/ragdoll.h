/**
 * @file ragdoll.h
 * @brief Ragdoll: bone↔body mapping with motor-driven sequential pipeline.
 *
 * A ragdoll maps skeleton bones to physics bodies and joints.
 * Each tick, the animation solver produces target poses, which become
 * motor targets on the physics joints.  After the physics tick, body
 * transforms sync back to bone world matrices for GPU skinning.
 *
 * Public types: 1 (ragdoll_t)
 * Public functions: see ragdoll_create.c, ragdoll_motor.c, ragdoll_sync.c
 */

#ifndef FERRUM_ANIMATION_RAGDOLL_H
#define FERRUM_ANIMATION_RAGDOLL_H

#include "ferrum/physics/joint_motor.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/joint.h"
#include "ferrum/math/mat4.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration. */
struct skeleton_def;

/**
 * @brief Ragdoll: maps skeleton bones to physics bodies + joints.
 *
 * @par Memory layout
 * All arrays are bone_count elements, allocated in a single block.
 * - body_indices: physics body indices (one per bone)
 * - joint_indices: physics joint indices (UINT32_MAX for root)
 * - motor_strengths: per-bone motor strength 0.0–1.0
 * - motors: per-bone motor data (target orientation + strength)
 * - bodies: local body array (for standalone ragdoll without phys_world)
 * - joints: local joint array (for standalone ragdoll without phys_world)
 * - bone_world: output world-space bone transforms
 *
 * @par Ownership
 * Owns all internal arrays.  Call ragdoll_destroy() to free.
 */
typedef struct ragdoll {
    uint32_t bone_count;            /**< Number of bones. */
    uint32_t *body_indices;         /**< Physics body index per bone. */
    uint32_t *joint_indices;        /**< Physics joint index per bone (UINT32_MAX for root). */
    float *motor_strengths;         /**< Per-bone motor strength. */
    phys_joint_motor_t *motors;     /**< Per-bone motor data. */
    phys_body_t *bodies;            /**< Local body storage. */
    phys_joint_t *joints;           /**< Local joint storage (bone_count - 1 max). */
    uint32_t joint_count;           /**< Number of active joints. */
    mat4_t *bone_world;             /**< Output: world-space bone transforms. */
} ragdoll_t;

/**
 * @brief Create a ragdoll from a skeleton definition.
 *
 * Creates one body per bone (capsule-shaped, positioned from world_pose).
 * Creates ball joints between parent-child bones.  Sets default
 * motor_strength to 1.0 (animation-dominated).
 *
 * @param ragdoll    Output ragdoll (non-NULL).
 * @param skel       Skeleton definition (non-NULL).
 * @param world_pose World-space bind poses (bone_count elements).
 * @return true on success, false on failure (NULL args, alloc failure).
 *
 * @par Ownership: ragdoll owns all allocated memory.
 * @par Side effects: allocates heap memory.
 */
bool ragdoll_create(ragdoll_t *ragdoll,
                    const struct skeleton_def *skel,
                    const mat4_t *world_pose);

/**
 * @brief Destroy a ragdoll and free all memory.
 *
 * @param ragdoll Ragdoll to destroy (NULL-safe).
 */
void ragdoll_destroy(ragdoll_t *ragdoll);

/**
 * @brief Set motor strength for all bones.
 *
 * @param ragdoll Ragdoll (NULL-safe).
 * @param strength Motor strength 0.0–1.0.
 */
void ragdoll_set_motor_strength(ragdoll_t *ragdoll, float strength);

/**
 * @brief Set motor strength for a single bone.
 *
 * @param ragdoll Ragdoll (NULL-safe).
 * @param bone_idx Bone index (out-of-range is no-op).
 * @param strength Motor strength 0.0–1.0.
 */
void ragdoll_set_bone_motor_strength(ragdoll_t *ragdoll, uint32_t bone_idx,
                                     float strength);

/**
 * @brief Update motor targets from animation solver output.
 *
 * Extracts rotation from each target_pose matrix and writes it
 * to the corresponding motor's target_orientation.  Also copies
 * the per-bone motor_strength into motor.strength.
 *
 * @param ragdoll     Ragdoll (NULL-safe).
 * @param target_pose World-space target poses from animation solver.
 * @param pose_count  Number of poses (clamped to bone_count).
 */
void ragdoll_update_motor_targets(ragdoll_t *ragdoll,
                                  const mat4_t *target_pose,
                                  uint32_t pose_count);

/**
 * @brief Sync bone world transforms from physics body transforms.
 *
 * Reads each body's position + orientation and writes the
 * corresponding bone_world matrix.
 *
 * @param ragdoll Ragdoll (NULL-safe).
 */
void ragdoll_sync_from_physics(ragdoll_t *ragdoll);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_ANIMATION_RAGDOLL_H */
