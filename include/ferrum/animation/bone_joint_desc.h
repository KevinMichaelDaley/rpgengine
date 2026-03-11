/**
 * @file bone_joint_desc.h
 * @brief Per-bone joint constraint descriptor for skeletal physics.
 *
 * Each bone may carry a joint descriptor specifying how it connects
 * to its parent bone in the physics simulation.  Joint types map
 * directly to phys_joint_type_t (cone_twist, hinge, distance).
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
 *   1 = cone_twist (3-DOF positional lock + per-axis angular limits)
 *   2 = hinge (1-DOF: rotation around axis only)
 *   3 = distance (spring-damper maintaining rest length)
 *   4 = lock (0-DOF: full rigid attachment)
 *   5 = copy_rotation (match orientation, no positional constraint)
 *   6 = limit_rotation (per-axis angular limits)
 *   7 = limit_position (per-axis positional limits)
 *   8 = aim (align axis toward target position)
 *   9 = twist (single-axis twist: 3 pos + 2 ang lock + optional limit)
 *  10 = ball_socket (3-DOF spherical joint, no angular limits)
 *
 * @par Ownership
 * Plain data struct, no internal allocations.
 * Array is owned by skeleton_def_t.
 */
typedef struct bone_joint_desc {
    uint32_t joint_type;    /**< 0=none, 1=cone_twist, 2=hinge, 3=distance,
                             *   4=lock, 5=copy_rotation, 6=limit_rotation,
                             *   7=limit_position, 8=aim, 9=twist,
                             *   10=ball_socket. */
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
    float    compliance;    /**< XPBD compliance (α) for positional rows;
                             *   0 = perfectly stiff.  Typical: 1e-4 to 1e-2. */
    float    angular_compliance; /**< XPBD compliance for angular limit rows.
                             *   0 = use same as positional compliance.
                             *   Allows angular limits to be softer than
                             *   positional lock.  Typical: 0.001–0.05. */
    float    damping;       /**< Viscous damping coefficient; dissipates
                             *   relative velocity at the joint.  0 = none. */
    float    yield_strength;/**< Impulse threshold for plastic deformation.
                             *   0 = no yield (joint never deforms). */
    float    break_strength;/**< Impulse threshold for joint destruction.
                             *   0 = unbreakable. */

    float    anchor_a[3];  /**< Joint anchor on parent body in armature
                             *   (engine) space.  When has_anchors==0,
                             *   ignored and computed from bone heads. */
    float    anchor_b[3];  /**< Joint anchor on child body in armature
                             *   (engine) space.  When has_anchors==0,
                             *   ignored and computed from bone heads. */
    uint8_t  has_anchors;  /**< Non-zero if anchor_a/b were explicitly set.
                             *   When 0, anchors are computed from bone
                             *   head positions at entity creation time. */

    /** Drive behavior flags (maps to PHYS_JOINT_FLAG_*).
     *  bit 0 = angular drive (return to rest pose within limits).
     *  bit 1 = linear drive (soft positional spring). */
    uint8_t  drive_flags;

    /** XPBD compliance for drive rows.  Controls drive softness
     *  independently from the hard constraint compliance.
     *  Higher = softer.  Typical: 0.1 (medium), 1.0 (very soft). */
    float    drive_compliance;

    /** CG solver inertia scaling factor.  Joint bodies appear this
     *  many times heavier to the CG solver, reducing the condition
     *  number of the system matrix when contacts are present.
     *  Default: 10.0.  Larger values improve conditioning but reduce
     *  solver accuracy for joint constraints. */
    float    mass_scale;
} bone_joint_desc_t;

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_ANIMATION_BONE_JOINT_DESC_H */
