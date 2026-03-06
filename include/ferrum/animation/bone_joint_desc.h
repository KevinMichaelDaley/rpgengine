/**
 * @file bone_joint_desc.h
 * @brief Per-bone joint constraint descriptor for skeletal physics.
 *
 * Each bone may carry a joint descriptor specifying how it connects
 * to its parent bone in the physics simulation.  Joint types map
 * directly to phys_joint_type_t (ball, hinge, distance).
 *
 * Exported from Blender and stored in the fskel v2 JNTS chunk.
 *
 * Public types: 1 (bone_joint_desc_t)
 */

#ifndef FERRUM_ANIMATION_BONE_JOINT_DESC_H
#define FERRUM_ANIMATION_BONE_JOINT_DESC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Per-bone joint descriptor.
 *
 * Describes the physics joint between this bone and its parent.
 * Bones with parent_index == UINT32_MAX (roots) should have
 * joint_type == 0 (NONE).
 *
 * joint_type values match phys_joint_type_t + 1 offset:
 *   0 = none (no joint, or root bone)
 *   1 = ball (3-DOF: positional lock, free rotation)
 *   2 = hinge (1-DOF: rotation around axis only)
 *   3 = distance (spring-damper maintaining rest length)
 *
 * @par Ownership
 * Plain data struct, no internal allocations.
 * Array is owned by skeleton_def_t.
 */
typedef struct bone_joint_desc {
    uint32_t joint_type;    /**< 0=none, 1=ball, 2=hinge, 3=distance. */
    float    axis[3];       /**< Hinge axis in parent bone's local space
                             *   (only used for joint_type=2). */
    float    rest_length;   /**< Distance joint rest length (joint_type=3).
                             *   0 = auto from bone length. */
    float    limit_min;     /**< Min angle (hinge, radians) or min distance. */
    float    limit_max;     /**< Max angle (hinge, radians) or max distance. */
} bone_joint_desc_t;

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_ANIMATION_BONE_JOINT_DESC_H */
