/**
 * @file force_curve.h
 * @brief Hill-type muscle force model.
 *
 * Computes instantaneous muscle force from:
 *   - Active force-length curve: Gaussian-like peak at optimal fiber length.
 *   - Passive force-length curve: exponential rise at long lengths.
 *   - Force-velocity curve: Hill equation (concentric drops, eccentric rises).
 *
 * F_total = activation * f_active(L) * f_velocity(V) * max_force
 *         + f_passive(L) * max_force
 *
 * All curves are parameterized per muscle instance.
 *
 * Public types: 2 (phys_muscle_params_t, phys_muscle_force_t)
 * Public functions: 2 (phys_muscle_params_init, phys_muscle_force_compute)
 */

#ifndef FERRUM_PHYSICS_MUSCLE_FORCE_CURVE_H
#define FERRUM_PHYSICS_MUSCLE_FORCE_CURVE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Per-muscle parameters for the Hill force model.
 *
 * @par Ownership
 * Plain data — no internal allocations.
 */
typedef struct phys_muscle_params {
    float optimal_length;  /**< Fiber length at peak active force (meters). */
    float max_force;       /**< Maximum isometric force at optimal length (N). */
    float max_velocity;    /**< Maximum shortening velocity (lengths/s). */
    float pennation_angle; /**< Fiber pennation angle at optimal length (rad). */
    float width;           /**< Active force-length curve width (dimensionless).
                            *   Larger = broader plateau. Typical: 0.56. */
} phys_muscle_params_t;

/**
 * @brief Decomposed muscle force output.
 *
 * @par Ownership
 * Plain data — no internal allocations.
 */
typedef struct phys_muscle_force {
    float f_active;   /**< Active force-length multiplier [0,1]. */
    float f_passive;  /**< Passive force-length multiplier (dimensionless). */
    float f_velocity; /**< Force-velocity multiplier. */
    float f_total;    /**< Total muscle force in Newtons. */
} phys_muscle_force_t;

/**
 * @brief Initialize muscle parameters to sensible defaults.
 *
 * Sets optimal_length=0.1, max_force=100, max_velocity=10,
 * pennation_angle=0, width=0.56.
 *
 * @param p  Parameters to initialize. NULL is a no-op.
 *
 * @par Side effects: none beyond writing to *p.
 */
void phys_muscle_params_init(phys_muscle_params_t *p);

/**
 * @brief Compute muscle force from the Hill model.
 *
 * @param params       Muscle parameters (non-NULL).
 * @param activation   Current activation level [0,1].
 * @param norm_length  Normalized fiber length (fiber_length / optimal_length).
 * @param norm_velocity Normalized fiber velocity (positive = shortening,
 *                     in units of max_velocity). Clamped internally.
 * @param out          Force output (non-NULL).
 *
 * No-op if params or out is NULL.
 *
 * @par Side effects: writes to *out.
 */
void phys_muscle_force_compute(const phys_muscle_params_t *params,
                                float activation,
                                float norm_length,
                                float norm_velocity,
                                phys_muscle_force_t *out);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PHYSICS_MUSCLE_FORCE_CURVE_H */
