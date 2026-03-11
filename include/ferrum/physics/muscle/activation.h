/**
 * @file activation.h
 * @brief Muscle activation dynamics (first-order ODE).
 *
 * Models the neural-to-mechanical delay in muscle activation using
 * a first-order differential equation with separate rise and fall
 * time constants:
 *
 *   da/dt = (u - a) / tau
 *
 * where tau = tau_rise when u > a, tau_fall when u < a.
 * Integrated with unconditionally-stable semi-implicit Euler.
 *
 * Public types: 1 (phys_muscle_activation_t)
 * Public functions: 2 (phys_muscle_activation_init, phys_muscle_activation_step)
 */

#ifndef FERRUM_PHYSICS_MUSCLE_ACTIVATION_H
#define FERRUM_PHYSICS_MUSCLE_ACTIVATION_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Muscle activation state and parameters.
 *
 * @par Ownership
 * Plain data — no internal allocations.
 */
typedef struct phys_muscle_activation {
    float activation; /**< Current activation level a in [0,1]. */
    float excitation; /**< Neural input signal u in [0,1]. */
    float tau_rise;   /**< Rise time constant (seconds). Typical: 0.01–0.02. */
    float tau_fall;   /**< Fall time constant (seconds). Typical: 0.04–0.06. */
} phys_muscle_activation_t;

/**
 * @brief Initialize activation state to safe defaults.
 *
 * Sets activation=0, excitation=0, tau_rise=0.015, tau_fall=0.050.
 *
 * @param act  Activation state to initialize. NULL is a no-op.
 *
 * @par Side effects: none beyond writing to *act.
 */
void phys_muscle_activation_init(phys_muscle_activation_t *act);

/**
 * @brief Advance activation by one timestep (semi-implicit Euler).
 *
 * Uses the formula: a_new = (a + dt/tau * u) / (1 + dt/tau)
 * where tau = tau_rise when u > a, tau_fall otherwise.
 * Result is clamped to [0,1].
 *
 * @param act  Activation state (non-NULL for effect).
 * @param dt   Timestep in seconds. Must be > 0; if <= 0, no-op.
 *
 * @par Side effects: modifies act->activation.
 */
void phys_muscle_activation_step(phys_muscle_activation_t *act, float dt);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PHYSICS_MUSCLE_ACTIVATION_H */
