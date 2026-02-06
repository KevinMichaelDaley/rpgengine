#ifndef FERRUM_PHYSICS_COMPOUND_COLLIDER_H
#define FERRUM_PHYSICS_COMPOUND_COLLIDER_H

/** @file
 * @brief Compound collider for animated hierarchies (phys-003b).
 *
 * A compound collider is a collection of primitive child colliders
 * (sphere, box, capsule), each optionally driven by a skeleton bone.
 * The compound stores inline shape data per child so that AABB
 * computation does not require access to external shape pools.
 *
 * Ownership: the compound does NOT own the children storage array;
 * the caller provides it via phys_compound_init(). All functions are
 * NULL-safe.
 */

#include <stdint.h>

#include "ferrum/physics/collider.h"
#include "ferrum/physics/aabb.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Compound child ─────────────────────────────────────────────── */

/**
 * @brief A single child primitive within a compound collider.
 *
 * Stores the collider reference, inline shape data (so AABB
 * computation is self-contained), and a bone index for skeletal
 * animation.
 *
 * bone_index 0xFFFF means the child is static relative to the body
 * and will not be updated by phys_compound_update_transforms().
 */
typedef struct phys_compound_child {
    phys_collider_t collider;     /**< Child primitive (sphere/box/capsule). */
    union {
        phys_sphere_t  sphere;    /**< Inline sphere shape data. */
        phys_box_t     box;       /**< Inline box shape data. */
        phys_capsule_t capsule;   /**< Inline capsule shape data. */
    } shape;                      /**< Inline shape data union. */
    uint16_t bone_index;          /**< Skeleton bone (0xFFFF = static). */
    uint16_t flags;               /**< Reserved for future use. */
} phys_compound_child_t;

/* ── Compound collider ──────────────────────────────────────────── */

/**
 * @brief Compound collider: array of children with cached AABB.
 *
 * Ownership: does NOT own the children array; caller provides
 * storage via phys_compound_init().
 */
typedef struct phys_compound_collider {
    phys_compound_child_t *children;  /**< Caller-owned children array. */
    uint16_t child_count;             /**< Current number of children. */
    uint16_t max_children;            /**< Capacity of children array. */
    phys_aabb_t cached_aabb;          /**< Union of all child AABBs. */
} phys_compound_collider_t;

/* ── API ────────────────────────────────────────────────────────── */

/**
 * @brief Initialize a compound collider with pre-allocated storage.
 *
 * @param cc      Compound collider to initialize (if NULL, no-op).
 * @param storage Caller-owned array of phys_compound_child_t (if NULL, no-op).
 * @param max     Capacity of the storage array.
 *
 * Sets child_count to 0 and zeroes the cached AABB.
 * No side effects beyond writing *cc.
 */
void phys_compound_init(phys_compound_collider_t *cc,
                        phys_compound_child_t *storage,
                        uint16_t max);

/**
 * @brief Add a child collider with inline shape data.
 *
 * Copies the collider and shape data (based on child->type) into the
 * next available slot. If the compound is at capacity or any pointer
 * is NULL, this is a no-op.
 *
 * @param cc         Compound collider (if NULL, no-op).
 * @param child      Collider reference to copy (if NULL, no-op).
 * @param shape_data Pointer to phys_sphere_t, phys_box_t, or phys_capsule_t
 *                   matching child->type (if NULL, no-op).
 * @param bone       Skeleton bone index (0xFFFF = static).
 *
 * No side effects beyond modifying *cc.
 */
void phys_compound_add_child(phys_compound_collider_t *cc,
                             const phys_collider_t *child,
                             const void *shape_data,
                             uint16_t bone);

/**
 * @brief Update child transforms from skeleton bone data.
 *
 * For each child whose bone_index < bone_count, sets the child's
 * local_offset to bone_positions[bone_index] and local_rotation
 * to bone_rotations[bone_index]. Children with bone_index == 0xFFFF
 * or bone_index >= bone_count are left unchanged.
 *
 * @param cc             Compound collider (if NULL, no-op).
 * @param bone_rotations Array of bone orientations as quaternions (may be NULL).
 * @param bone_positions Array of bone positions (may be NULL).
 * @param bone_count     Number of bones in the arrays.
 *
 * No side effects beyond modifying child transforms in *cc.
 */
void phys_compound_update_transforms(phys_compound_collider_t *cc,
                                     const phys_quat_t *bone_rotations,
                                     const phys_vec3_t *bone_positions,
                                     uint16_t bone_count);

/**
 * @brief Compute compound AABB as union of all child AABBs.
 *
 * For each child, computes its world-space AABB by combining the body
 * transform (body_pos, body_rot) with the child's local transform and
 * inline shape data, then merges all child AABBs into the output.
 *
 * @param cc       Compound collider (if NULL, no-op).
 * @param body_pos Body world-space position.
 * @param body_rot Body world-space orientation.
 * @param out      Output AABB (if NULL, no-op).
 *
 * No side effects beyond writing *out.
 */
void phys_compound_compute_aabb(const phys_compound_collider_t *cc,
                                phys_vec3_t body_pos,
                                phys_quat_t body_rot,
                                phys_aabb_t *out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_COMPOUND_COLLIDER_H */
