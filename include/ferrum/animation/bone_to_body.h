/**
 * @file bone_to_body.h
 * @brief Adapter: convert between skeleton bone matrices and physics bodies.
 *
 * These functions bridge the animation and physics systems by converting
 * bone world-space transforms (mat4_t) to/from phys_body_t position and
 * orientation fields.  They are used to populate physics body pool slots
 * for animated skeletons, and to read solved positions back into bone
 * matrices after the physics tick.
 *
 * Ownership: all arrays are caller-owned.  No allocations performed.
 */
#ifndef FERRUM_ANIMATION_BONE_TO_BODY_H
#define FERRUM_ANIMATION_BONE_TO_BODY_H

#include "ferrum/math/mat4.h"
#include "ferrum/physics/body.h"
#include "ferrum/animation/bone_collider.h"
#include <stdint.h>

/**
 * @brief Populate physics body fields from bone world-space matrices.
 *
 * For each bone, extracts position and orientation from the world pose
 * matrix and writes them into the corresponding body.  If a collider
 * descriptor array is provided, also sets mass, inertia, kinematic flag,
 * and CCD flag.
 *
 * @param world_pose  Array of bone world-space transforms (read).
 * @param colliders   Optional per-bone collider descriptors (may be NULL).
 * @param bodies      Output physics body array (written).
 * @param count       Number of bones / bodies.
 *
 * @note Does NOT set body_pool indices or entity_index.  Caller must
 *       manage the body pool slot mapping.
 * @note Does NOT zero bodies; caller should memset or init first.
 */
void anim_bones_to_bodies(const mat4_t *world_pose,
                          const bone_collider_desc_t *colliders,
                          phys_body_t *bodies,
                          uint32_t count);

/**
 * @brief Write physics body positions/orientations back to bone matrices.
 *
 * Constructs a rotation matrix from each body's orientation quaternion
 * and sets translation from the body's position.  Used after the physics
 * tick to update the skeleton's world-space bone poses.
 *
 * @param bodies      Input physics body array (read).
 * @param world_pose  Output bone world-space transforms (written).
 * @param count       Number of bodies / bones.
 */
void anim_bodies_to_bones(const phys_body_t *bodies,
                          mat4_t *world_pose,
                          uint32_t count);

#endif /* FERRUM_ANIMATION_BONE_TO_BODY_H */
