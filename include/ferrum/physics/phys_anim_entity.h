/**
 * @file phys_anim_entity.h
 * @brief Animated entity: register a skeleton as physics bodies in a world.
 *
 * Creates one body per bone (with collider and mass/inertia from the
 * skeleton's bone_collider_desc_t) and one joint per parent-child pair
 * (from the skeleton's bone_joint_desc_t) inside a phys_world_t.
 *
 * After each physics tick, call phys_anim_entity_sync_from_world() to
 * read solved body positions back into bone world-space matrices.
 *
 * Public types: 2 (phys_anim_entity_t, phys_anim_pre_tick_fn)
 */

#ifndef FERRUM_PHYSICS_PHYS_ANIM_ENTITY_H
#define FERRUM_PHYSICS_PHYS_ANIM_ENTITY_H

#include "ferrum/math/mat4.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations. */
struct phys_world;
struct skeleton_def;

/**
 * @brief Animated entity: maps a skeleton to physics world bodies.
 *
 * @par Memory layout
 * - body_indices: phys_world body index per bone (bone_count elements).
 *   UINT32_MAX for bones with no collider (BONE_COLLIDER_NONE).
 * - bone_world: output world-space bone transforms (bone_count elements).
 *
 * @par Ownership
 * Owns body_indices and bone_world arrays.  Call
 * phys_anim_entity_destroy() to free.  The physics world bodies
 * and joints persist until the world is destroyed.
 */
typedef struct phys_anim_entity {
    uint32_t  bone_count;       /**< Number of skeleton bones. */
    uint32_t *body_indices;     /**< World body index per bone (UINT32_MAX = none). */
    uint32_t  body_count;       /**< Number of bodies actually created. */
    uint32_t *joint_world_ids;  /**< World joint IDs (joint_count elements). */
    uint32_t  joint_count;      /**< Number of joints created in the world. */
    mat4_t   *bone_world;       /**< Output world-space bone transforms. */
} phys_anim_entity_t;

/**
 * @brief Create an animated entity in a physics world.
 *
 * For each bone with a non-NONE collider, creates a body in the world
 * with the bone's rest-pose position/orientation, sets the collider
 * shape, mass, kinematic flag, and CCD flag.  Then creates joints
 * between parent-child bone pairs from the skeleton's joint descriptors.
 *
 * Bones with BONE_COLLIDER_NONE get body_indices[i] = UINT32_MAX and
 * do not participate in physics.
 *
 * @param entity     Output entity (non-NULL, zeroed on failure).
 * @param world      Physics world to populate (non-NULL).
 * @param skel       Skeleton definition with colliders and joints.
 * @param world_pose Initial world-space bone transforms (bone_count elements).
 * @return true on success, false on NULL args or allocation failure.
 *
 * @note Ownership: entity owns body_indices and bone_world.
 *       World bodies/joints are owned by the world.
 * @note Side effects: creates bodies and joints in the world;
 *       allocates heap memory for entity arrays.
 */
bool phys_anim_entity_create(phys_anim_entity_t *entity,
                             struct phys_world *world,
                             const struct skeleton_def *skel,
                             const mat4_t *world_pose);

/**
 * @brief Sync bone world transforms from physics body positions.
 *
 * Reads each body's position + orientation from the world and writes
 * the corresponding bone_world matrix.  Bones without bodies
 * (body_indices[i] == UINT32_MAX) are left unchanged.
 *
 * @param entity  Animated entity (NULL-safe, no-op).
 * @param world   Physics world to read from (NULL-safe, no-op).
 *
 * @note Thread safety: call from the main thread after the tick
 *       runner has completed a tick (read bodies_curr).
 */
void phys_anim_entity_sync_from_world(phys_anim_entity_t *entity,
                                      const struct phys_world *world);

/**
 * @brief Update kinematic body positions in the world from bone matrices.
 *
 * For each kinematic bone (is_kinematic flag set in collider desc),
 * writes the bone_world position and orientation to the corresponding
 * physics body.  Non-kinematic bones are skipped (physics owns them).
 *
 * Call this from the pre-tick callback after advancing animation.
 *
 * @param entity     Animated entity (NULL-safe, no-op).
 * @param world      Physics world (NULL-safe, no-op).
 * @param world_pose Updated bone world-space transforms.
 * @param count      Number of transforms (clamped to bone_count).
 */
void phys_anim_entity_push_kinematic(phys_anim_entity_t *entity,
                                     struct phys_world *world,
                                     const mat4_t *world_pose,
                                     uint32_t count);

/**
 * @brief Drive all bodies toward animation targets with blended deltas.
 *
 * For kinematic bodies: sets position and orientation absolutely.
 * For dynamic bodies: applies an interpolated delta — blends current
 * position/orientation toward the animation target by @p blend factor.
 *
 * Call this from the substep callback after computing animation pose.
 *
 * @param entity     Animated entity (NULL-safe, no-op).
 * @param world      Physics world (NULL-safe, no-op).
 * @param world_pose Target bone world-space transforms.
 * @param count      Number of transforms (clamped to bone_count).
 * @param blend      Interpolation factor [0..1]. 0 = no effect, 1 = snap.
 *                   Values around 0.2–0.5 give soft animation drive.
 */
void phys_anim_entity_drive_toward(phys_anim_entity_t *entity,
                                   struct phys_world *world,
                                   const mat4_t *world_pose,
                                   uint32_t count,
                                   float blend);

/**
 * @brief Free the animated entity's internal arrays.
 *
 * Does NOT remove bodies or joints from the physics world.
 *
 * @param entity  Entity to destroy (NULL-safe, no-op).
 */
void phys_anim_entity_destroy(phys_anim_entity_t *entity);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PHYSICS_PHYS_ANIM_ENTITY_H */
