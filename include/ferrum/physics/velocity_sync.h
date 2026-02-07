#ifndef FERRUM_PHYSICS_VELOCITY_SYNC_H
#define FERRUM_PHYSICS_VELOCITY_SYNC_H

/** @file
 * @brief Constraint-normal velocity synchronization after position projection.
 *
 * After position projection corrects penetration, body velocities must be
 * updated to reflect the corrected geometry.  Instead of naively setting
 * v = delta_q / dt (which destroys tangential friction velocity from TGS),
 * we replace only the constraint-normal component of each body's velocity.
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

/**
 * @brief Arguments for constraint-normal velocity synchronization.
 *
 * @par Ownership
 * - island, constraints: borrowed, read-only.
 * - bodies: borrowed, velocities are MODIFIED in place.
 * - position_deltas: borrowed, read-only (from position projection result).
 */
typedef struct phys_velocity_sync_args {
    const struct phys_island *island;        /**< Island that was projected. */
    const struct phys_constraint *constraints; /**< Global constraint array. */
    struct phys_body *bodies;                /**< Global body array (modified). */
    const phys_vec3_t *position_deltas;      /**< Per-body position deltas from projection. */
    float dt;                                /**< Timestep in seconds. */
} phys_velocity_sync_args_t;

/**
 * @brief Synchronize body velocities after position projection using
 *        constraint-normal component replacement.
 *
 * For each dynamic body in the island, replaces the constraint-normal
 * component of its linear velocity with the normal component of the
 * position correction velocity (delta_q / dt).  Tangential velocity
 * from friction solving is preserved.
 *
 * See ref/sparse_stabilization.tex Section 5b.
 *
 * @param args  Sync arguments. NULL-safe (no-op).
 *
 * @note Modifies bodies[].linear_vel in place.
 * @note Static/kinematic bodies (inv_mass == 0) are skipped.
 * @note Sleeping islands are skipped.
 */
void phys_velocity_sync_normals(const phys_velocity_sync_args_t *args);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_VELOCITY_SYNC_H */
