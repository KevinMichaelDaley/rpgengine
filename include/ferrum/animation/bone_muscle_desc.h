/**
 * @file bone_muscle_desc.h
 * @brief Per-bone muscle descriptor for skeletal physics.
 *
 * Each bone may carry a muscle descriptor specifying an antagonist
 * muscle pair that drives the joint DOF between this bone and its
 * parent.  Exported from Blender and stored in the fskel JSON.
 *
 * Public types: 2 (bone_muscle_unit_desc_t, bone_muscle_desc_t)
 */

#ifndef FERRUM_ANIMATION_BONE_MUSCLE_DESC_H
#define FERRUM_ANIMATION_BONE_MUSCLE_DESC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Descriptor for a single muscle unit (half of an antagonist pair).
 *
 * @par Ownership
 * Plain data struct, no internal allocations.
 */
typedef struct bone_muscle_unit_desc {
    /* Attachment geometry (armature/engine space). */
    float origin[3];    /**< Origin point on parent body (armature space). */
    float insertion[3]; /**< Insertion point on child body (armature space). */

    /* Force model parameters. */
    float optimal_length;  /**< Fiber length at peak active force (m). 0 = auto. */
    float max_force;       /**< Maximum isometric force (N). */
    float max_velocity;    /**< Maximum shortening velocity (lengths/s). */
    float pennation_angle; /**< Fiber pennation angle (rad). */
    float width;           /**< Active force-length curve width. */

    /* Activation dynamics. */
    float tau_rise;  /**< Activation rise time constant (s). */
    float tau_fall;  /**< Activation fall time constant (s). */

    /* Tendon parameters. */
    float tendon_slack_length;     /**< Tendon slack length (m). 0 = auto. */
    float tendon_stiffness;        /**< Normalized tendon stiffness. */
    float tendon_reference_strain; /**< Reference strain (~0.033). */

    /* Wrapping surface (cylinder). */
    float wrap_center[3]; /**< Wrap cylinder center (armature space). */
    float wrap_axis[3];   /**< Wrap cylinder axis (unit vector). */
    float wrap_radius;    /**< Wrap cylinder radius. 0 = no wrapping. */
} bone_muscle_unit_desc_t;

/**
 * @brief Per-bone antagonist muscle pair descriptor.
 *
 * Describes a flexor/extensor pair driving the joint DOF between
 * this bone and its parent.  When has_muscle is 0, no muscles
 * are configured for this bone.
 *
 * @par Ownership
 * Plain data struct, no internal allocations.
 * Array is owned by skeleton_def_t.
 */
typedef struct bone_muscle_desc {
    uint8_t has_muscle;   /**< Non-zero if this bone has muscles. */
    uint8_t target_row;   /**< Joint row index driven by this pair. */
    uint8_t _pad[2];

    bone_muscle_unit_desc_t flexor;   /**< Flexor muscle. */
    bone_muscle_unit_desc_t extensor; /**< Extensor muscle. */
} bone_muscle_desc_t;

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_ANIMATION_BONE_MUSCLE_DESC_H */
