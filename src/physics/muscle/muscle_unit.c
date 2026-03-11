/**
 * @file muscle_unit.c
 * @brief Composite muscle unit evaluation pipeline.
 *
 * 2 non-static functions:
 *   1. phys_muscle_unit_init
 *   2. phys_muscle_unit_evaluate
 */

#include "ferrum/physics/muscle/muscle_unit.h"
#include "ferrum/physics/body.h"

#include <math.h>
#include <stddef.h>

void phys_muscle_unit_init(phys_muscle_unit_t *unit)
{
    if (!unit) { return; }
    phys_muscle_activation_init(&unit->activation);
    phys_muscle_params_init(&unit->params);
    phys_tendon_params_init(&unit->tendon);
    phys_muscle_attach_init(&unit->attach);
    unit->wrap.center_local = (phys_vec3_t){0, 0, 0};
    unit->wrap.axis_local   = (phys_vec3_t){0, 1, 0};
    unit->wrap.radius       = 0.0f;
    unit->fiber_length      = unit->params.optimal_length;
    unit->fiber_velocity    = 0.0f;
}

void phys_muscle_unit_evaluate(phys_muscle_unit_t *unit,
                                const phys_vec3_t *joint_axis_world,
                                const phys_vec3_t *joint_pos_world,
                                const phys_body_t *body_a,
                                const phys_body_t *body_b,
                                float dt,
                                float *torque_out)
{
    if (!unit || !joint_axis_world || !joint_pos_world ||
        !body_a || !body_b || !torque_out || dt <= 0.0f) {
        if (torque_out) { *torque_out = 0.0f; }
        return;
    }

    /* Step 1: Activation dynamics. */
    phys_muscle_activation_step(&unit->activation, dt);

    /* Step 2: Compute geometry (moment arm, total fiber length). */
    float moment_arm = 0.0f;
    float total_fiber_length = 0.0f;
    phys_muscle_geometry_moment_arm(
        &unit->attach,
        (unit->wrap.radius > 0.0f) ? &unit->wrap : NULL,
        joint_axis_world,
        joint_pos_world,
        body_a, body_b,
        &moment_arm,
        &total_fiber_length);

    /* Step 3: Solve tendon equilibrium to get actual fiber length. */
    phys_tendon_state_t tendon_state;
    float total_mt_length = total_fiber_length + unit->tendon.slack_length;
    phys_tendon_equilibrium(
        &unit->tendon,
        &unit->params,
        unit->activation.activation,
        total_mt_length,
        unit->fiber_length,
        &tendon_state);

    /* Step 4: Compute fiber velocity by finite difference. */
    float prev_length = unit->fiber_length;
    float new_length  = tendon_state.fiber_length;
    float fiber_vel   = (new_length - prev_length) / dt;

    /* Update state. */
    unit->fiber_length   = new_length;
    unit->fiber_velocity = fiber_vel;

    /* Step 5: Compute muscle force. */
    float norm_length   = new_length / unit->params.optimal_length;
    float norm_velocity = (unit->params.max_velocity > 0.0f)
                        ? (fiber_vel / (unit->params.max_velocity *
                                        unit->params.optimal_length))
                        : 0.0f;

    phys_muscle_force_t force;
    phys_muscle_force_compute(
        &unit->params,
        unit->activation.activation,
        norm_length,
        norm_velocity,
        &force);

    /* Step 6: Torque = force × moment_arm. */
    *torque_out = force.f_total * moment_arm;
}
