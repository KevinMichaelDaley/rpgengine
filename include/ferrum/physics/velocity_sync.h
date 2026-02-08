#ifndef FERRUM_PHYSICS_VELOCITY_SYNC_H
#define FERRUM_PHYSICS_VELOCITY_SYNC_H

/** @file
 * @brief Constraint-normal velocity synchronization after position projection.
 *
 * After position projection corrects penetration (both linear and angular),
 * body velocities must be updated to reflect the corrected geometry.  The
 * velocity sync replaces only the constraint-normal component of each
 * body's velocity (linear and angular) using the full block-diagonal
 * Jacobian, preserving tangential friction velocity from TGS.
 *
 * See ref/sparse_stabilization.tex Section 5b.
 */

#include "ferrum/physics/phys_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations. */
struct phys_island;
struct phys_constraint;
struct phys_body;
struct phys_velocity;

/**
 * @brief Arguments for constraint-normal velocity synchronization.
 *
 * @par Ownership
 * - island, constraints: borrowed, read-only.
 * - bodies: borrowed, velocities are MODIFIED in place (linear + angular).
 * - correction_deltas: borrowed, read-only (from position projection result).
 */
typedef struct phys_velocity_sync_args {
    const struct phys_island *island;        /**< Island that was projected. */
    const struct phys_constraint *constraints; /**< Global constraint array. */
    struct phys_body *bodies;                /**< Global body array (modified). */
    const struct phys_velocity *correction_deltas; /**< Per-body generalized deltas from projection. */
    float dt;                                /**< Timestep in seconds. */
} phys_velocity_sync_args_t;

/**
 * @brief Synchronize body velocities after position projection using
 *        constraint-normal component replacement (full Jacobian).
 *
 * For each constraint in the island, computes the target relative
 * velocity along the constraint normal from the generalized correction
 * deltas (linear + angular), then adjusts both linear and angular
 * velocity via impulse to match.  Tangential velocity from friction
 * solving is preserved.
 *
 * The solver is fully bilateral (lambda may be positive or negative)
 * to find the true least-squares velocity match without directional
 * bias.  Controlled by the PHYS_VELOCITY_SYNC_ENABLED compile flag.
 *
 * See ref/sparse_stabilization.tex Section 5b.
 *
 * @param args  Sync arguments. NULL-safe (no-op).
 *
 * @note Modifies bodies[].linear_vel and bodies[].angular_vel in place.
 * @note Static/kinematic bodies (inv_mass == 0) are skipped.
 * @note Sleeping islands are skipped.
 */
void phys_velocity_sync_normals(const phys_velocity_sync_args_t *args);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_VELOCITY_SYNC_H */
