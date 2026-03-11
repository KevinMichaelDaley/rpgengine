/**
 * @file joint_driver.h
 * @brief Joint driver types and application API.
 *
 * A joint driver modifies a specific constraint row on a built joint,
 * overriding its bias and lambda bounds to implement motors, springs,
 * and other actuation behaviors.
 *
 * Usage:
 *   1. phys_joint_driver_init() to set safe defaults.
 *   2. Set driver type, target_row, and type-specific params.
 *   3. After calling a joint build function, call phys_joint_driver_apply()
 *      to modify the target row in-place.
 *
 * Public types: 2 (phys_joint_driver_type_t, phys_joint_driver_t)
 * Public functions: 2 (phys_joint_driver_init, phys_joint_driver_apply)
 */

#ifndef FERRUM_PHYSICS_JOINT_DRIVER_H
#define FERRUM_PHYSICS_JOINT_DRIVER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations. */
struct phys_joint;

/**
 * @brief Driver type discriminator.
 */
typedef enum phys_joint_driver_type {
    PHYS_DRIVER_NONE            = 0, /**< No driver (default). */
    PHYS_DRIVER_MOTOR           = 1, /**< Constant-velocity motor. */
    PHYS_DRIVER_SPRING          = 2, /**< Restoring spring with damping. */
    PHYS_DRIVER_LINEAR_ACTUATOR = 3, /**< Position-targeting actuator with speed limit. */
    PHYS_DRIVER_SERVO           = 4, /**< PD controller (proportional-derivative). */
    PHYS_DRIVER_AERO_HYDRAULIC  = 5, /**< Velocity-dependent drag or flow-limited force. */
} phys_joint_driver_type_t;

/**
 * @brief Motor driver parameters.
 *
 * Drives a constraint row at a constant target velocity.
 * The row bias is set to target_velocity and lambda is clamped
 * to [-max_force, +max_force].
 */
typedef struct phys_motor_params {
    float target_velocity; /**< Target velocity (rad/s or m/s). */
    float max_force;       /**< Maximum impulse magnitude per step. */
} phys_motor_params_t;

/**
 * @brief Spring driver parameters.
 *
 * Applies a restoring force proportional to displacement from
 * rest_value, with viscous damping.  The row bias is set to
 * stiffness * (rest_value - constraint_error).
 */
typedef struct phys_spring_params {
    float stiffness;  /**< Spring stiffness (N/m or N·m/rad). */
    float damping;    /**< Viscous damping coefficient. */
    float rest_value; /**< Equilibrium position/angle. */
} phys_spring_params_t;

/**
 * @brief Linear actuator driver parameters.
 *
 * Drives a constraint row toward a target position at a controlled speed.
 * Bias = clamp((target_position - constraint_error) * gain, -max_speed, max_speed).
 * Lambda clamped to ±max_force.
 */
typedef struct phys_actuator_params {
    float target_position; /**< Target position/angle. */
    float max_speed;       /**< Maximum velocity magnitude. */
    float max_force;       /**< Maximum impulse magnitude per step. */
} phys_actuator_params_t;

/**
 * @brief Servo (PD controller) driver parameters.
 *
 * Drives a constraint row with proportional-derivative control.
 * Bias = kp * (target_value - constraint_error) + kd * (-current_velocity).
 * Lambda clamped to ±max_force.
 */
typedef struct phys_servo_params {
    float target_value; /**< Target position/angle. */
    float kp;           /**< Proportional gain. */
    float kd;           /**< Derivative gain. */
    float max_force;    /**< Maximum impulse magnitude per step. */
} phys_servo_params_t;

/**
 * @brief Aerodynamic/hydraulic driver parameters.
 *
 * Velocity-dependent force model:
 *   - Aerodynamic mode: drag force = -drag_coeff * v * |v|
 *   - Hydraulic mode: velocity clamped to ±flow_limit
 * Both effects stack.  Lambda clamped to ±max_force.
 */
typedef struct phys_aero_params {
    float drag_coeff; /**< Drag coefficient (0 = no drag). */
    float flow_limit; /**< Maximum velocity under load (0 = unlimited). */
    float max_force;  /**< Maximum impulse magnitude per step. */
} phys_aero_params_t;

/**
 * @brief Joint driver: attaches actuation behavior to a constraint row.
 *
 * A driver targets a specific row index on a built joint and modifies
 * its bias and lambda bounds to implement the chosen behavior.
 *
 * @par Ownership
 * Plain data — no internal allocations.
 */
typedef struct phys_joint_driver {
    phys_joint_driver_type_t type; /**< Driver type discriminator. */
    uint8_t target_row;            /**< Index of the joint row to drive. */

    union {
        phys_motor_params_t    motor;    /**< Motor params (when type == MOTOR). */
        phys_spring_params_t   spring;   /**< Spring params (when type == SPRING). */
        phys_actuator_params_t actuator; /**< Actuator params (when type == LINEAR_ACTUATOR). */
        phys_servo_params_t    servo;    /**< Servo params (when type == SERVO). */
        phys_aero_params_t     aero;     /**< Aero/hydraulic params (when type == AERO_HYDRAULIC). */
    };
} phys_joint_driver_t;

/**
 * @brief Initialize a driver to safe defaults (PHYS_DRIVER_NONE).
 *
 * Zeroes the struct.  type is set to PHYS_DRIVER_NONE, target_row to 0.
 *
 * @param driver  Driver to initialize.  NULL is a no-op.
 *
 * @par Side effects: none beyond writing to *driver.
 */
void phys_joint_driver_init(phys_joint_driver_t *driver);

/**
 * @brief Apply a driver to a built joint's target row.
 *
 * Modifies joint->rows[driver->target_row] in-place:
 *   - MOTOR: bias = target_velocity, lambda clamped to ±max_force.
 *   - SPRING: bias = stiffness * (rest_value - constraint_error),
 *             lambda bounds set to large bilateral range.
 *   - LINEAR_ACTUATOR: bias = clamped position error, lambda ±max_force.
 *   - SERVO: bias = kp * error + kd * (-velocity), lambda ±max_force.
 *   - AERO_HYDRAULIC: drag and/or flow-limited velocity, lambda ±max_force.
 *
 * No-op if driver or joint is NULL, driver type is NONE, or
 * target_row >= joint->row_count.
 *
 * @param driver  Driver configuration (non-NULL for effect).
 * @param joint   Joint with rows already built (non-NULL for effect).
 *
 * @par Ownership: caller owns all pointers.  No allocations.
 * @par Side effects: modifies joint->rows[target_row].
 */
void phys_joint_driver_apply(const phys_joint_driver_t *driver,
                              struct phys_joint *joint);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PHYSICS_JOINT_DRIVER_H */
