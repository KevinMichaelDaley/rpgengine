/**
 * @file muscle_unit.h
 * @brief Composite muscle unit aggregating all sub-components.
 *
 * A muscle unit combines activation dynamics, Hill force model,
 * tendon compliance, and attachment geometry into a single evaluable
 * entity that produces a torque about a joint axis.
 *
 * Public types: 1 (phys_muscle_unit_t)
 * Public functions: 2 (phys_muscle_unit_init, phys_muscle_unit_evaluate)
 */

#ifndef FERRUM_PHYSICS_MUSCLE_MUSCLE_UNIT_H
#define FERRUM_PHYSICS_MUSCLE_MUSCLE_UNIT_H

#include "ferrum/physics/muscle/activation.h"
#include "ferrum/physics/muscle/force_curve.h"
#include "ferrum/physics/muscle/tendon.h"
#include "ferrum/physics/muscle/geometry.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration. */
struct phys_body;

/**
 * @brief Complete muscle unit with state and parameters.
 *
 * @par Ownership
 * Plain data — no internal allocations.
 */
typedef struct phys_muscle_unit {
    phys_muscle_activation_t activation;  /**< Activation dynamics state. */
    phys_muscle_params_t     params;      /**< Hill force model parameters. */
    phys_tendon_params_t     tendon;      /**< Tendon series elastic element. */
    phys_muscle_attach_t     attach;      /**< Origin/insertion attachment. */
    phys_muscle_wrap_t       wrap;        /**< Optional wrapping surface. */

    float fiber_length;   /**< Current fiber length from last evaluation (meters). */
    float fiber_velocity; /**< Fiber velocity from finite difference (lengths/s). */
} phys_muscle_unit_t;

/**
 * @brief Initialize muscle unit to safe defaults.
 *
 * Initializes all sub-components and sets fiber_length to optimal_length,
 * fiber_velocity to 0.
 *
 * @param unit  Muscle unit to initialize. NULL is a no-op.
 *
 * @par Side effects: none beyond writing to *unit.
 */
void phys_muscle_unit_init(phys_muscle_unit_t *unit);

/**
 * @brief Evaluate the full muscle pipeline and produce torque.
 *
 * Pipeline:
 *   1. Step activation dynamics.
 *   2. Compute geometry (moment arm, fiber length).
 *   3. Solve tendon equilibrium.
 *   4. Compute Hill force.
 *   5. Torque = force * moment_arm.
 *
 * @param unit            Muscle unit state (non-NULL). Modified in place.
 * @param joint_axis_world Joint rotation axis in world space (unit vector).
 * @param joint_pos_world  Joint pivot position in world space.
 * @param body_a          Parent body (non-NULL).
 * @param body_b          Child body (non-NULL).
 * @param dt              Timestep in seconds. Must be > 0.
 * @param torque_out      Output: torque about joint axis (N·m). Non-NULL.
 *
 * No-op (torque_out set to 0) if required pointers are NULL or dt <= 0.
 *
 * @par Side effects: modifies unit state (activation, fiber_length, fiber_velocity).
 */
void phys_muscle_unit_evaluate(phys_muscle_unit_t *unit,
                                const phys_vec3_t *joint_axis_world,
                                const phys_vec3_t *joint_pos_world,
                                const struct phys_body *body_a,
                                const struct phys_body *body_b,
                                float dt,
                                float *torque_out);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PHYSICS_MUSCLE_MUSCLE_UNIT_H */
