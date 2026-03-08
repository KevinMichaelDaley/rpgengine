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
struct phys_joint;
struct phys_manifold;

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
    const struct phys_mat3 *inv_inertia_world; /**< Precomputed world-space inverse inertia per body. */
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
    struct phys_joint *joints;              /**< Joint array for nonlinear position projection (may be NULL). */
    uint32_t joint_count;                   /**< Number of joints in the array. */
    struct phys_body *bodies_mut;           /**< Mutable body array for coupled implicit solver (TIER_ANIM).
                                             *   When non-NULL, the coupled solver writes position/orientation
                                             *   updates directly to bodies during the solve.  Must point to
                                             *   the same memory as bodies (typically bodies_next).  May be NULL
                                             *   for standard TGS (decoupled) operation. */
    struct phys_mat3 *inv_inertia_world_mut; /**< Mutable world-space inverse inertia (for coupled solver
                                              *   to update after orientation changes). May be NULL. */
    const uint32_t *constraint_joint_indices; /**< Maps constraint index → joint index (for Jacobian rebuild). */
    uint8_t *skip_body;                      /**< Per-body flag: 1 = coupled solver updated position,
                                              *   integrator should skip position integration.
                                              *   Arena-allocated, body_count entries. May be NULL. */
    const struct phys_manifold *manifolds;   /**< Manifold array for contact constraint rebuild. May be NULL. */
    uint32_t manifold_count;                 /**< Number of manifolds. */
    float baumgarte;                         /**< Baumgarte stabilization factor for contact rebuild. */
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
