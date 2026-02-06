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

/* ── Per-tier parameters ────────────────────────────────────────── */

/**
 * @brief Per-tier simulation parameters.
 *
 * Controls whether a tier is active and what solver settings it uses.
 * In Phase 1 every tier receives the same values from world config.
 *
 * Ownership: value type, no heap allocation.
 */
typedef struct phys_tier_params {
    bool     active;            /**< Whether this tier is simulated. */
    uint32_t substeps;          /**< Substeps for this tier. */
    uint32_t iterations;        /**< Solver iterations for this tier. */
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

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_STEP_PLAN_H */
