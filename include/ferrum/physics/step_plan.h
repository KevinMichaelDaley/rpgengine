#ifndef FERRUM_PHYSICS_STEP_PLAN_H
#define FERRUM_PHYSICS_STEP_PLAN_H

/** @file
 * @brief Step Plan stage (Stage 0) for the physics pipeline.
 *
 * Determines simulation parameters for the current tick based on
 * world configuration and optional game state.  For Phase 1, all
 * tiers receive identical parameters derived from world config.
 */

#include "ferrum/physics/tier_list.h"

#include <stdbool.h>
#include <stdint.h>

struct phys_world;
struct phys_game_state;

#ifdef __cplusplus
extern "C" {
#endif

/* ── Solver mode ────────────────────────────────────────────────── */

/**
 * @brief Solver algorithm used for a given tier or cross-tier constraint.
 */
typedef enum phys_solver_mode {
    PHYS_SOLVER_TGS  = 0,      /**< Temporal Gauss-Seidel. */
    PHYS_SOLVER_XPBD = 1,      /**< Extended Position-Based Dynamics. */
} phys_solver_mode_t;

/* ── Per-tier parameters ────────────────────────────────────────── */

/**
 * @brief Per-tier simulation parameters.
 *
 * Controls whether a tier is active and what solver settings it uses.
 *
 * Ownership: value type, no heap allocation.
 */
typedef struct phys_tier_params {
    bool     active;            /**< Whether this tier is simulated. */
    phys_solver_mode_t solver_mode; /**< Solver algorithm for this tier. */
    uint32_t substeps;          /**< Substeps for this tier. */
    uint32_t iterations;        /**< Solver iterations for this tier. */
    float    compliance;        /**< XPBD compliance (ignored for TGS). */
    float    friction_boost;    /**< Friction multiplier (1.0 = default). */
    float    restitution_scale; /**< Restitution multiplier (1.0 = default). */
} phys_tier_params_t;

/* ── Step plan ──────────────────────────────────────────────────── */

/**
 * @brief Simulation plan for a single physics tick.
 *
 * Computed once per tick by phys_stage_step_plan().  Contains global
 * timing parameters and per-tier settings.
 *
 * Ownership: value type, no heap allocation.
 */
typedef struct phys_step_plan {
    uint32_t substeps;          /**< Number of substeps per tick. */
    uint32_t solver_iterations; /**< Constraint solver iterations. */
    float    dt;                /**< Fixed timestep for this tick (seconds). */
    float    substep_dt;        /**< dt / substeps (seconds). */
    phys_tier_params_t tier_params[PHYS_TIER_COUNT]; /**< Per-tier settings. */
} phys_step_plan_t;

/* ── API ────────────────────────────────────────────────────────── */

/**
 * @brief Compute the step plan for the current tick (Stage 0).
 *
 * Reads configuration from @p world and fills @p plan with timing
 * and per-tier parameters.  In Phase 1, all tiers receive identical
 * settings from the world config.
 *
 * @param plan   Step plan to fill (NULL-safe, no-op if NULL).
 * @param world  World to read config from (NULL-safe, no-op if NULL).
 * @param game   Optional game state (may be NULL; Phase 1 ignores it).
 *
 * Ownership: caller owns @p plan; no heap allocation.
 * Side effects: writes *plan.
 */
void phys_stage_step_plan(phys_step_plan_t *plan,
                          const struct phys_world *world,
                          const struct phys_game_state *game);

/**
 * @brief Return default solver parameters for a given tier.
 *
 * Pure function — no side effects, no heap allocation.
 * For tiers outside T0–T4 (e.g. T5/sleeping), returns a
 * default inactive entry with TGS/1 substep/1 iteration.
 *
 * @param tier  The simulation tier to query.
 * @return Per-tier parameters (value type).
 */
phys_tier_params_t phys_get_tier_params(phys_tier_t tier);

/**
 * @brief Determine solver mode for a cross-tier constraint.
 *
 * Returns PHYS_SOLVER_TGS if either body is T0 or T1 (high-fidelity),
 * otherwise returns PHYS_SOLVER_XPBD.
 *
 * @param tier_a  Tier of the first body.
 * @param tier_b  Tier of the second body.
 * @return The solver mode to use for the constraint.
 */
phys_solver_mode_t phys_tier_cross_solver_mode(phys_tier_t tier_a,
                                               phys_tier_t tier_b);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_STEP_PLAN_H */
