/**
 * @file muscle_pair.c
 * @brief Antagonist muscle pairing — net torque and co-contraction.
 *
 * 2 non-static functions:
 *   1. phys_muscle_pair_init
 *   2. phys_muscle_pair_compute_torque
 */

#include "ferrum/physics/muscle/muscle_pair.h"
#include "ferrum/physics/body.h"

#include <math.h>
#include <stddef.h>

void phys_muscle_pair_init(phys_muscle_pair_t *pair)
{
    if (!pair) { return; }
    phys_muscle_unit_init(&pair->flexor);
    phys_muscle_unit_init(&pair->extensor);
    pair->target_row = 0;
}

void phys_muscle_pair_compute_torque(phys_muscle_pair_t *pair,
                                      const phys_vec3_t *joint_axis_world,
                                      const phys_vec3_t *joint_pos_world,
                                      const phys_body_t *body_a,
                                      const phys_body_t *body_b,
                                      float dt,
                                      float *net_torque_out,
                                      float *stiffness_out)
{
    if (!pair || !net_torque_out) {
        if (net_torque_out) { *net_torque_out = 0.0f; }
        if (stiffness_out)  { *stiffness_out  = 0.0f; }
        return;
    }

    float flexor_torque  = 0.0f;
    float extensor_torque = 0.0f;

    phys_muscle_unit_evaluate(&pair->flexor,
                               joint_axis_world, joint_pos_world,
                               body_a, body_b, dt,
                               &flexor_torque);

    phys_muscle_unit_evaluate(&pair->extensor,
                               joint_axis_world, joint_pos_world,
                               body_a, body_b, dt,
                               &extensor_torque);

    /* Net torque: flexor produces positive torque, extensor negative. */
    *net_torque_out = flexor_torque - extensor_torque;

    /* Co-contraction stiffness: approximate as sum of absolute torques.
     * When both muscles are active, stiffness is high even if net
     * torque is near zero. */
    if (stiffness_out) {
        *stiffness_out = fabsf(flexor_torque) + fabsf(extensor_torque);
    }
}
