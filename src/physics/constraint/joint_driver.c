/**
 * @file joint_driver.c
 * @brief Joint driver initialization and application.
 *
 * 2 non-static functions:
 *   1. phys_joint_driver_init
 *   2. phys_joint_driver_apply
 */

#include "ferrum/physics/joint_driver.h"
#include "ferrum/physics/joint.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/* ── Initialization ──────────────────────────────────────────────── */

void phys_joint_driver_init(phys_joint_driver_t *driver)
{
    if (!driver) {
        return;
    }
    memset(driver, 0, sizeof(*driver));
    driver->type = PHYS_DRIVER_NONE;
    driver->target_row = 0;
}

/* ── Application ─────────────────────────────────────────────────── */

void phys_joint_driver_apply(const phys_joint_driver_t *driver,
                              phys_joint_t *joint)
{
    if (!driver || !joint) {
        return;
    }
    if (driver->type == PHYS_DRIVER_NONE) {
        return;
    }
    if (driver->target_row >= joint->row_count) {
        return;
    }

    phys_jacobian_row_t *row = &joint->rows[driver->target_row];

    switch (driver->type) {
    case PHYS_DRIVER_MOTOR:
        /* Motor: override bias with target velocity, clamp lambda. */
        row->bias = driver->motor.target_velocity;
        row->lambda_min = -driver->motor.max_force;
        row->lambda_max = driver->motor.max_force;
        break;

    case PHYS_DRIVER_SPRING: {
        /* Spring: restoring bias proportional to displacement from rest. */
        float error = row->constraint_error;
        row->bias = driver->spring.stiffness *
                    (driver->spring.rest_value - error);
        /* Bilateral bounds: allow push and pull. */
        float large = 1e6f;
        row->lambda_min = -large;
        row->lambda_max = large;
        break;
    }

    case PHYS_DRIVER_LINEAR_ACTUATOR: {
        /* Linear actuator: drive toward target_position at controlled speed. */
        float error = row->constraint_error;
        float pos_error = driver->actuator.target_position - error;
        /* Convert position error to velocity bias.  Use a gain of 60
         * (equivalent to 1/dt at 60 Hz) to get reasonable convergence. */
        float raw_bias = pos_error * 60.0f;
        /* Clamp to max_speed. */
        float max_spd = driver->actuator.max_speed;
        if (raw_bias > max_spd) raw_bias = max_spd;
        if (raw_bias < -max_spd) raw_bias = -max_spd;
        row->bias = raw_bias;
        row->lambda_min = -driver->actuator.max_force;
        row->lambda_max = driver->actuator.max_force;
        break;
    }

    case PHYS_DRIVER_SERVO: {
        /* Servo (PD controller): kp * error + kd * (-velocity). */
        float error = row->constraint_error;
        float pos_error = driver->servo.target_value - error;
        /* The row's current bias approximates the velocity on this DOF. */
        float current_vel = row->bias;
        float bias = driver->servo.kp * pos_error
                   + driver->servo.kd * (-current_vel);
        row->bias = bias;
        row->lambda_min = -driver->servo.max_force;
        row->lambda_max = driver->servo.max_force;
        break;
    }

    case PHYS_DRIVER_AERO_HYDRAULIC: {
        /* Aero/hydraulic: velocity-dependent effects. */
        float v = row->bias;

        /* Flow limit: clamp velocity magnitude. */
        if (driver->aero.flow_limit > 0.0f) {
            if (v > driver->aero.flow_limit) v = driver->aero.flow_limit;
            if (v < -driver->aero.flow_limit) v = -driver->aero.flow_limit;
        }

        /* Aerodynamic drag: F = -drag_coeff * v * |v|. */
        if (driver->aero.drag_coeff > 0.0f) {
            v -= driver->aero.drag_coeff * v * fabsf(v);
        }

        row->bias = v;
        row->lambda_min = -driver->aero.max_force;
        row->lambda_max = driver->aero.max_force;
        break;
    }

    default:
        break;
    }
}
