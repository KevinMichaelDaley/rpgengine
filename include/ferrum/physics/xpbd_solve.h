#ifndef FERRUM_PHYSICS_XPBD_SOLVE_H
#define FERRUM_PHYSICS_XPBD_SOLVE_H

/** @file
 * @brief XPBD (Extended Position-Based Dynamics) solver for T2-T4 bodies.
 *
 * Position-based solver that is unconditionally stable and does not
 * require island decomposition.  Part of Stage 11b of the physics pipeline.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations. */
struct phys_constraint;
struct phys_body;
struct phys_velocity;

/**
 * @brief Arguments for the XPBD position solver stage.
 *
 * Ownership: caller owns all pointers.  The solver reads bodies_in,
 * writes to bodies_out and velocities_out, and updates constraint lambdas.
 */
typedef struct phys_xpbd_solve_args {
    struct phys_constraint *constraints;  /**< Modified: lambda updated. */
    uint32_t constraint_count;            /**< Number of constraints. */
    const struct phys_body *bodies_in;    /**< Original body positions (read-only). */
    struct phys_body *bodies_out;         /**< Positions updated by solver. */
    struct phys_velocity *velocities_out; /**< Derived from position change. */
    uint32_t body_count;                  /**< Number of bodies. */
    uint32_t iterations;                  /**< Solver iterations (typically 2-8). */
    float omega;                          /**< Jacobi relaxation factor (0.5-0.8). */
    float dt;                             /**< Timestep in seconds. */
} phys_xpbd_solve_args_t;

/**
 * @brief Run the XPBD position solver on all constraints.
 *
 * Copies body positions from bodies_in to bodies_out, then iterates
 * position-level constraint projection.  Final velocities are derived
 * from the position delta divided by dt.
 *
 * @param args  Solver arguments.  NULL-safe (no-op).
 *
 * @note Side effects: modifies bodies_out, velocities_out, and constraint
 *       lambdas.
 * @note No allocations performed.
 */
void phys_stage_xpbd_solve(const phys_xpbd_solve_args_t *args);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_XPBD_SOLVE_H */
