/**
 * @file muscle_pair.h
 * @brief Antagonist muscle pairing and co-contraction.
 *
 * Pairs a flexor and extensor muscle on opposite sides of a joint.
 * Net torque = flexor_torque - extensor_torque.
 * Co-contraction stiffness = sum of both muscles' effective stiffness.
 *
 * Public types: 1 (phys_muscle_pair_t)
 * Public functions: 2 (phys_muscle_pair_init, phys_muscle_pair_compute_torque)
 */

#ifndef FERRUM_PHYSICS_MUSCLE_MUSCLE_PAIR_H
#define FERRUM_PHYSICS_MUSCLE_MUSCLE_PAIR_H

#include "ferrum/physics/muscle/muscle_unit.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration. */
struct phys_body;

/**
 * @brief Antagonist muscle pair driving a single joint DOF.
 *
 * @par Ownership
 * Plain data — no internal allocations. Caller owns the struct.
 */
typedef struct phys_muscle_pair {
    phys_muscle_unit_t flexor;    /**< Flexor muscle (positive torque direction). */
    phys_muscle_unit_t extensor;  /**< Extensor muscle (negative torque direction). */
    uint8_t target_row;           /**< Joint row index this pair drives. */
} phys_muscle_pair_t;

/**
 * @brief Initialize a muscle pair to safe defaults.
 *
 * Initializes both muscle units and sets target_row to 0.
 *
 * @param pair  Muscle pair to initialize. NULL is a no-op.
 *
 * @par Side effects: none beyond writing to *pair.
 */
void phys_muscle_pair_init(phys_muscle_pair_t *pair);

/**
 * @brief Compute net torque and co-contraction stiffness.
 *
 * Evaluates both muscles and computes:
 *   net_torque = flexor_torque - extensor_torque
 *   stiffness  = |flexor_torque| + |extensor_torque| (approximate)
 *
 * @param pair            Muscle pair state (non-NULL). Modified in place.
 * @param joint_axis_world Joint rotation axis in world space (unit vector).
 * @param joint_pos_world  Joint pivot position in world space.
 * @param body_a          Parent body (non-NULL).
 * @param body_b          Child body (non-NULL).
 * @param dt              Timestep in seconds.
 * @param net_torque_out  Output: net torque about joint axis (N·m). Non-NULL.
 * @param stiffness_out   Output: co-contraction stiffness (N·m). May be NULL.
 *
 * @par Side effects: modifies both muscle unit states.
 */
void phys_muscle_pair_compute_torque(phys_muscle_pair_t *pair,
                                      const phys_vec3_t *joint_axis_world,
                                      const phys_vec3_t *joint_pos_world,
                                      const struct phys_body *body_a,
                                      const struct phys_body *body_b,
                                      float dt,
                                      float *net_torque_out,
                                      float *stiffness_out);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PHYSICS_MUSCLE_MUSCLE_PAIR_H */
