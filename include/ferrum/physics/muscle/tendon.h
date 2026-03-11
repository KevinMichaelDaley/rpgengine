/**
 * @file tendon.h
 * @brief Tendon series elastic element model.
 *
 * Models the tendon as a stiff nonlinear spring in series with the
 * muscle fiber.  At each timestep, solves the muscle-tendon
 * equilibrium: F_muscle(L_fiber) = F_tendon(L_tendon), where
 * L_tendon = L_total - L_fiber * cos(pennation).
 *
 * Enables energy storage (e.g. Achilles tendon in running).
 *
 * Public types: 2 (phys_tendon_params_t, phys_tendon_state_t)
 * Public functions: 2 (phys_tendon_params_init, phys_tendon_equilibrium)
 */

#ifndef FERRUM_PHYSICS_MUSCLE_TENDON_H
#define FERRUM_PHYSICS_MUSCLE_TENDON_H

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations. */
struct phys_muscle_params;

/**
 * @brief Tendon parameters.
 *
 * @par Ownership
 * Plain data — no internal allocations.
 */
typedef struct phys_tendon_params {
    float slack_length;     /**< Tendon length below which force is zero (meters). */
    float stiffness;        /**< Normalized stiffness at reference strain. */
    float reference_strain; /**< Strain at which stiffness is defined (~0.033). */
} phys_tendon_params_t;

/**
 * @brief Tendon equilibrium output state.
 *
 * @par Ownership
 * Plain data — no internal allocations.
 */
typedef struct phys_tendon_state {
    float tendon_length; /**< Current tendon length (meters). */
    float tendon_force;  /**< Current tendon force (Newtons). */
    float fiber_length;  /**< Equilibrium fiber length (meters). */
} phys_tendon_state_t;

/**
 * @brief Initialize tendon parameters to sensible defaults.
 *
 * Sets slack_length=0.2, stiffness=35.0, reference_strain=0.033.
 *
 * @param p  Parameters to initialize. NULL is a no-op.
 *
 * @par Side effects: none beyond writing to *p.
 */
void phys_tendon_params_init(phys_tendon_params_t *p);

/**
 * @brief Solve muscle-tendon equilibrium.
 *
 * Finds the fiber length where F_muscle(L_fiber) = F_tendon(L_tendon)
 * using bounded Newton iteration (max 10 iterations).
 *
 * @param tendon       Tendon parameters (non-NULL).
 * @param muscle       Muscle fiber parameters (non-NULL).
 * @param activation   Current activation [0,1].
 * @param total_length Total musculotendon unit length (meters).
 * @param fiber_hint   Initial guess for fiber length (previous frame's value).
 *                     Use muscle->optimal_length if no prior state.
 * @param out          Output state (non-NULL).
 *
 * No-op if tendon, muscle, or out is NULL.
 *
 * @par Side effects: writes to *out.
 */
void phys_tendon_equilibrium(const phys_tendon_params_t *tendon,
                              const struct phys_muscle_params *muscle,
                              float activation,
                              float total_length,
                              float fiber_hint,
                              phys_tendon_state_t *out);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PHYSICS_MUSCLE_TENDON_H */
