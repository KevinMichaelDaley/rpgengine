#ifndef FERRUM_PHYSICS_JOINT_ANGULAR_PROJECTION_H
#define FERRUM_PHYSICS_JOINT_ANGULAR_PROJECTION_H

/** @file
 * @brief Post-solve angular position projection for joint limits.
 *
 * After the TGS velocity solve and nonlinear position projection,
 * joint angles may still exceed their limits — especially after
 * high-impulse contact events.  This module clamps joint angles
 * back to their allowed ranges by injecting angular pseudo-velocity
 * corrections that the integrator applies alongside positional ones.
 *
 * Types: phys_angular_projection_args_t (1 type).
 */

#include <stdint.h>

#include "ferrum/physics/phys_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations. */
struct phys_joint;
struct phys_body;
struct phys_velocity;

/**
 * @brief Arguments for joint angular position projection.
 *
 * @par Ownership
 * - joints, bodies: borrowed, read-only.
 * - pseudo_velocities: borrowed, modified in place.
 */
typedef struct phys_angular_projection_args {
    const struct phys_joint *joints;       /**< Joint array. */
    uint32_t joint_count;                  /**< Number of joints. */
    const struct phys_body *bodies;        /**< Body array (current positions/orientations). */
    const struct phys_velocity *velocities; /**< Solved velocities from TGS (for prediction). */
    struct phys_velocity *pseudo_velocities; /**< Pseudo-velocity workspace (modified). */
    uint32_t body_count;                   /**< Number of bodies. */
    float dt;                              /**< Substep timestep (s). */
} phys_angular_projection_args_t;

/**
 * @brief Project joint angles back to their allowed limit ranges.
 *
 * For each cone-twist or hinge joint with angular limits, predicts
 * the post-integration orientation (accounting for existing pseudo-
 * velocities), measures the angular error via swing-twist decomposition,
 * and applies corrective angular pseudo-velocity to clamp the angle
 * to the nearest limit boundary.
 *
 * @param args  Projection arguments.  NULL-safe (no-op).
 *
 * @note Runs after nonlinear position projection.
 * @note Modifies pseudo_velocities angular components only.
 * @note No allocations.
 */
void phys_project_joint_angular_limits(
    const phys_angular_projection_args_t *args);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_JOINT_ANGULAR_PROJECTION_H */
