/**
 * @file joint_motor.h
 * @brief Angular motor for physics joints.
 *
 * A motor drives a joint's child body (body B) toward a target
 * orientation by adding angular constraint rows to the joint.
 * Motor strength controls how aggressively the motor fights
 * external forces (0 = passive, 1 = near-kinematic).
 *
 * Public types: 1 (phys_joint_motor_t)
 * Public functions: 2 (phys_joint_motor_init, phys_joint_motor_apply)
 */

#ifndef FERRUM_PHYSICS_JOINT_MOTOR_H
#define FERRUM_PHYSICS_JOINT_MOTOR_H

#include "ferrum/physics/phys_types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations. */
struct phys_joint;
struct phys_body;

/**
 * @brief Angular motor data for a physics joint.
 *
 * When attached to a ball or hinge joint, the motor adds up to 3
 * angular constraint rows that drive body B toward the target
 * orientation.  The bias on each angular row encodes the orientation
 * error scaled by `strength`.
 *
 * @par Ownership
 * Plain data — no internal allocations.
 */
typedef struct phys_joint_motor {
    phys_quat_t target_orientation; /**< Target world orientation for body B. */
    float       strength;           /**< Motor strength 0.0–1.0. */
    float       max_torque;         /**< Lambda clamp magnitude (0 = unlimited). */
} phys_joint_motor_t;

/**
 * @brief Initialize motor to safe defaults.
 *
 * Sets target_orientation to identity, strength and max_torque to 0.
 *
 * @param motor Motor to initialize (NULL is no-op).
 */
void phys_joint_motor_init(phys_joint_motor_t *motor);

/**
 * @brief Apply motor angular rows to an already-built joint.
 *
 * Adds up to 3 angular Jacobian rows starting at joint->row_count.
 * Each row drives body B's orientation toward motor->target_orientation.
 * The angular error is decomposed into X/Y/Z components and scaled by
 * motor->strength.
 *
 * If strength <= 0 or motor/joint/bodies are NULL, no rows are added.
 *
 * @param motor  Motor configuration (non-NULL).
 * @param joint  Joint with rows already built (non-NULL).
 * @param body_a Body A (non-NULL).
 * @param body_b Body B (non-NULL).
 * @param dt     Timestep in seconds (must be > 0).
 * @return Number of rows added (0 or 3).
 *
 * @pre joint->row_count + 3 <= PHYS_JOINT_MAX_ROWS
 * @par Ownership: caller owns all pointers.
 * @par Side effects: writes joint->rows and increments joint->row_count.
 */
uint8_t phys_joint_motor_apply(const phys_joint_motor_t *motor,
                                struct phys_joint *joint,
                                const struct phys_body *body_a,
                                const struct phys_body *body_b,
                                float dt);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PHYSICS_JOINT_MOTOR_H */
