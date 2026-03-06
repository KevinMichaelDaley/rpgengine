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
 * joint_type values:
 *   0 = none (no joint, or root bone)
 *   1 = ball (3-DOF: positional lock, free rotation)
 *   2 = hinge (1-DOF: rotation around axis only)
 *   3 = distance (spring-damper maintaining rest length)
 *   4 = lock (0-DOF: full rigid attachment)
 *   5 = copy_rotation (match orientation, no positional constraint)
 *   6 = limit_rotation (per-axis angular limits)
 *   7 = limit_position (per-axis positional limits)
 *   8 = aim (align axis toward target position)
 *
 * @par Ownership
 * Plain data struct, no internal allocations.
 * Array is owned by skeleton_def_t.
 */
typedef struct bone_joint_desc {
    uint32_t joint_type;    /**< 0=none, 1=ball, 2=hinge, 3=distance,
                             *   4=lock, 5=copy_rotation, 6=limit_rotation,
                             *   7=limit_position, 8=aim. */
    float    axis[3];       /**< Hinge axis (type=2) or track axis (type=8)
                             *   in parent bone's local space. */
    float    rest_length;   /**< Distance joint rest length (type=3).
                             *   0 = auto from bone length. */
    float    limit_min[3];  /**< Per-axis min angle (rad) or position.
                             *   For hinge (type=2): only [0] is used.
                             *   For limit_rotation/position: [0]=X,[1]=Y,[2]=Z. */
    float    limit_max[3];  /**< Per-axis max angle (rad) or position. */
    uint32_t limit_axes;    /**< Bitmask of active limit axes: bit 0=X, 1=Y, 2=Z.
                             *   Only used for types 6 and 7. */
} bone_joint_desc_t;

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_ANIMATION_BONE_JOINT_DESC_H */
