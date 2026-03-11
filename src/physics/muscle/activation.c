/**
 * @file activation.c
 * @brief Muscle activation dynamics — semi-implicit Euler integration.
 *
 * 2 non-static functions:
 *   1. phys_muscle_activation_init
 *   2. phys_muscle_activation_step
 */

#include "ferrum/physics/muscle/activation.h"

#include <stddef.h>

void phys_muscle_activation_init(phys_muscle_activation_t *act)
{
    if (!act) { return; }
    act->activation = 0.0f;
    act->excitation = 0.0f;
    act->tau_rise   = 0.015f;
    act->tau_fall   = 0.050f;
}

void phys_muscle_activation_step(phys_muscle_activation_t *act, float dt)
{
    if (!act || dt <= 0.0f) { return; }

    float u = act->excitation;
    float a = act->activation;

    /* Clamp excitation to [0,1]. */
    if (u < 0.0f) { u = 0.0f; }
    if (u > 1.0f) { u = 1.0f; }

    /* Select time constant based on excitation vs current activation. */
    float tau = (u > a) ? act->tau_rise : act->tau_fall;

    /* Guard against zero/negative time constants. */
    if (tau <= 0.0f) {
        act->activation = u;
        return;
    }

    /* Semi-implicit Euler: unconditionally stable for any dt.
     * a_new = (a + dt/tau * u) / (1 + dt/tau) */
    float ratio = dt / tau;
    float a_new = (a + ratio * u) / (1.0f + ratio);

    /* Clamp to [0,1]. */
    if (a_new < 0.0f) { a_new = 0.0f; }
    if (a_new > 1.0f) { a_new = 1.0f; }

    act->activation = a_new;
}
