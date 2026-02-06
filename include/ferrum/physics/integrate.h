#ifndef FERRUM_PHYSICS_INTEGRATE_H
#define FERRUM_PHYSICS_INTEGRATE_H

/** @file
 * @brief Stage 12: Integrate + Sleep.
 *
 * Updates body positions and orientations from solved velocities,
 * applies gravity, and detects sleeping bodies.
 */

#include <stdint.h>

#include "ferrum/physics/phys_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations. */
struct phys_body;
struct phys_velocity;

/**
 * @brief Arguments for the integrate + sleep stage.
 *
 * Ownership: caller owns all pointers.  The stage reads bodies_in and
 * velocities, and writes to bodies_out.  bodies_in and bodies_out may
 * alias safely (in-place integration).
 *
 * @note sleep_threshold_linear and sleep_threshold_angular are speeds
 *       (not squared).
 */
typedef struct phys_integrate_args {
    const struct phys_body *bodies_in;     /**< Input body array (read-only). */
    const struct phys_velocity *velocities; /**< Solved velocity pairs. */
    struct phys_body *bodies_out;          /**< Output body array. */
    uint32_t body_count;                   /**< Number of bodies. */
    float dt;                              /**< Time step (seconds). */
    phys_vec3_t gravity;                   /**< Gravitational acceleration. */
    float sleep_threshold_linear;          /**< Speed below which linear is "at rest". */
    float sleep_threshold_angular;         /**< Speed below which angular is "at rest". */
    uint32_t sleep_delay_frames;           /**< Frames below threshold before sleeping. */
} phys_integrate_args_t;

/**
 * @brief Run the integrate + sleep stage on all bodies.
 *
 * For each dynamic (non-static, non-kinematic) body:
 * - Copies solved velocity from the velocities array.
 * - Applies gravity to the linear velocity.
 * - Integrates position: position += linear_vel * dt.
 * - Integrates orientation via quaternion derivative.
 * - Detects sleeping bodies based on velocity thresholds.
 *
 * Static and kinematic bodies are copied unchanged.
 *
 * @param args  Integration arguments.  NULL-safe (no-op).
 *
 * @note No allocations performed.
 * @note Side effects: writes to args->bodies_out.
 */
void phys_stage_integrate(const phys_integrate_args_t *args);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_INTEGRATE_H */
