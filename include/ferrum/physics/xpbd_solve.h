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
    float compliance;                     /**< XPBD compliance (α); 0 = stiff. */
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

/**
 * @brief Solve a slice of constraints on a shared body workspace.
 *
 * Designed for parallel dispatch: each fiber calls this on a disjoint
 * slice of constraints, all writing to the same bodies array.
 * The Jacobi relaxation factor (omega < 1) ensures convergence even
 * with concurrent writes from multiple fibers.
 *
 * @param constraints  Constraint slice to solve (lambdas modified).
 * @param count        Number of constraints in the slice.
 * @param bodies       Shared body workspace (positions modified in-place).
 * @param iterations   Number of solver iterations.
 * @param omega        Jacobi relaxation factor (0.5–0.8).
 * @param dt           Timestep in seconds.
 * @param compliance   XPBD compliance (α); 0 = perfectly stiff.
 *
 * @note Does NOT copy bodies or derive velocities — caller manages that.
 * @note NULL-safe (no-op on NULL constraints or bodies).
 */
void phys_xpbd_solve_constraint_batch(
    struct phys_constraint *constraints,
    uint32_t count,
    struct phys_body *bodies,
    uint32_t iterations,
    float omega,
    float dt,
    float compliance);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_XPBD_SOLVE_H */
