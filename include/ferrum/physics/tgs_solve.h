#ifndef FERRUM_PHYSICS_TGS_SOLVE_H
#define FERRUM_PHYSICS_TGS_SOLVE_H

/** @file
 * @brief TGS (Temporal Gauss-Seidel) velocity solver for T0/T1 bodies.
 *
 * Operates per-island, solving each island's constraints sequentially
 * for multiple iterations.  Part of Stage 11a of the physics pipeline.
 */

#include <stdint.h>

#include "ferrum/physics/phys_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations. */
struct phys_island_list;
struct phys_constraint;
struct phys_body;
struct phys_frame_arena;

/**
 * @brief Linear and angular velocity pair for a single body.
 *
 * Used as the solver's working copy of body velocities.
 */
typedef struct phys_velocity {
    phys_vec3_t linear;   /**< Linear velocity (m/s). */
    phys_vec3_t angular;  /**< Angular velocity (rad/s). */
} phys_velocity_t;

/**
 * @brief Arguments for the TGS velocity solver stage.
 *
 * Ownership: caller owns all pointers.  The solver reads bodies and
 * islands, and writes to velocities and constraint lambdas.
 */
typedef struct phys_tgs_solve_args {
    const struct phys_island_list *islands;  /**< Island decomposition. */
    struct phys_constraint *constraints;     /**< Constraint array (lambda updated). */
    const struct phys_body *bodies;          /**< Body array (read-only). */
    phys_velocity_t *velocities;            /**< In/out: solver velocity workspace. */
    phys_velocity_t *pseudo_velocities;     /**< In/out: split-impulse position correction workspace (may be NULL). */
    uint32_t body_count;                    /**< Number of bodies. */
    uint32_t iterations;                    /**< Solver iterations (typically 20–24). */
    phys_vec3_t gravity;                    /**< Gravity vector (m/s²). */
    float dt;                               /**< Substep time step (s). */
    float tick_dt;                          /**< Full tick dt (s), for per-tier gravity. */
    float slop;                             /**< Penetration slop (no position correction below this). */
    const uint32_t *tier_substep_counts;    /**< Per-tier substep counts (may be NULL). */
    struct phys_frame_arena *frame_arena;   /**< Frame arena for per-island coloring workspace (may be NULL). */
    uint32_t island_color_threshold;        /**< Min constraints per island to enable coloring (0 = disabled). */
} phys_tgs_solve_args_t;

/**
 * @brief Run the TGS velocity solver on all non-sleeping islands.
 *
 * Initializes the velocities array from body state, then iterates
 * the sequential impulse solver per island.
 *
 * @param args  Solver arguments.  NULL-safe (no-op).
 *
 * @note Side effects: modifies args->velocities and constraint lambdas.
 * @note No allocations performed.
 */
void phys_stage_tgs_solve(const phys_tgs_solve_args_t *args);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_TGS_SOLVE_H */
