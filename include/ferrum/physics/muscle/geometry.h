/**
 * @file geometry.h
 * @brief Muscle attachment geometry and moment arm computation.
 *
 * Computes the moment arm of a muscle about a joint axis from
 * attachment points (origin on parent bone, insertion on child bone).
 * Supports simple cylinder wrapping for muscles crossing bony
 * prominences.
 *
 * Torque = muscle_force * moment_arm
 *
 * Public types: 2 (phys_muscle_attach_t, phys_muscle_wrap_t)
 * Public functions: 2 (phys_muscle_attach_init, phys_muscle_geometry_moment_arm)
 */

#ifndef FERRUM_PHYSICS_MUSCLE_GEOMETRY_H
#define FERRUM_PHYSICS_MUSCLE_GEOMETRY_H

#include "ferrum/physics/phys_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration. */
struct phys_body;

/**
 * @brief Muscle attachment points in bone-local coordinates.
 *
 * @par Ownership
 * Plain data — no internal allocations.
 */
typedef struct phys_muscle_attach {
    phys_vec3_t origin_local;    /**< Origin point in parent body's local space. */
    phys_vec3_t insertion_local; /**< Insertion point in child body's local space. */
} phys_muscle_attach_t;

/**
 * @brief Wrapping surface for muscle routing around bone geometry.
 *
 * Models a cylinder that the muscle line of action wraps around.
 * When radius is 0, no wrapping is applied (straight line).
 *
 * @par Ownership
 * Plain data — no internal allocations.
 */
typedef struct phys_muscle_wrap {
    phys_vec3_t center_local; /**< Wrap cylinder center in parent body space. */
    phys_vec3_t axis_local;   /**< Wrap cylinder axis (unit vector) in parent body space. */
    float radius;             /**< Wrap cylinder radius. 0 = no wrapping. */
} phys_muscle_wrap_t;

/**
 * @brief Initialize attachment to zero (origin and insertion at body origins).
 *
 * @param att  Attachment to initialize. NULL is a no-op.
 *
 * @par Side effects: none beyond writing to *att.
 */
void phys_muscle_attach_init(phys_muscle_attach_t *att);

/**
 * @brief Compute moment arm and fiber length from attachment geometry.
 *
 * Transforms origin and insertion to world space using body transforms,
 * computes the muscle line of action (with optional cylinder wrapping),
 * and projects onto the joint axis to get the moment arm.
 *
 * @param attach          Attachment points (non-NULL).
 * @param wrap            Wrapping surface (NULL = no wrapping).
 * @param joint_axis_world Joint rotation axis in world space (unit vector, non-NULL).
 * @param joint_pos_world  Joint pivot position in world space (non-NULL).
 * @param body_a          Parent body (non-NULL).
 * @param body_b          Child body (non-NULL).
 * @param moment_arm_out  Output: signed moment arm (meters). Non-NULL.
 * @param fiber_length_out Output: straight-line or wrapped fiber length (meters).
 *                         May be NULL if not needed.
 *
 * No-op if required pointers are NULL.
 *
 * @par Side effects: writes to *moment_arm_out and optionally *fiber_length_out.
 */
void phys_muscle_geometry_moment_arm(
    const phys_muscle_attach_t *attach,
    const phys_muscle_wrap_t *wrap,
    const phys_vec3_t *joint_axis_world,
    const phys_vec3_t *joint_pos_world,
    const struct phys_body *body_a,
    const struct phys_body *body_b,
    float *moment_arm_out,
    float *fiber_length_out);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PHYSICS_MUSCLE_GEOMETRY_H */
