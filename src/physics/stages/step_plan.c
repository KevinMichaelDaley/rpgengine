/**
 * @file step_plan.c
 * @brief Stage 0 — compute the step plan for the current tick.
 *
 * Reads world configuration and (in future phases) game state to
 * populate a phys_step_plan_t with timing and per-tier parameters.
 *
 * Phase 1: all tiers receive identical settings from world config.
 */

#include "ferrum/physics/step_plan.h"
#include "ferrum/physics/world.h"

#include <stddef.h>
#include <string.h>

/* ── Stage 0 implementation ─────────────────────────────────────── */

void phys_stage_step_plan(phys_step_plan_t *plan,
                          const struct phys_world *world,
                          const struct phys_game_state *game)
{
    if (!plan || !world) {
        return;
    }

    /* Silence unused-parameter warning — Phase 1 ignores game state. */
    (void)game;

    const phys_world_config_t *cfg = &world->config;

    /* Global timing.
     * When dt_override > 0, use it instead of fixed_dt (variable timestep
     * under sustained overload).  Clamp to max_dt_override × fixed_dt to
     * prevent physics explosions from a single huge frame. */
    float dt = cfg->fixed_dt;
    if (world->dt_override > 0.0f) {
        float max_dt = cfg->max_dt_override * cfg->fixed_dt;
        dt = world->dt_override;
        if (dt > max_dt) { dt = max_dt; }
    }

    plan->substeps          = cfg->default_substeps;
    plan->solver_iterations = cfg->default_solver_iterations;
    plan->dt                = dt;

    /* Guard against zero substeps to avoid division by zero. */
    if (plan->substeps > 0) {
        plan->substep_dt = plan->dt / (float)plan->substeps;
    } else {
        plan->substep_dt = plan->dt;
    }

    /* Phase 1: every tier gets the same parameters from world config. */
    for (int t = 0; t < PHYS_TIER_COUNT; ++t) {
        plan->tier_params[t].active            = true;
        plan->tier_params[t].solver_mode       = PHYS_SOLVER_TGS;
        plan->tier_params[t].substeps          = cfg->default_substeps;
        plan->tier_params[t].iterations        = cfg->default_solver_iterations;
        plan->tier_params[t].compliance        = 0.0f;
        plan->tier_params[t].friction_boost    = 1.0f;
        plan->tier_params[t].restitution_scale = 1.0f;
    }

    /* T0 (direct) and T1 (near) get extra substeps for stability.
     * The tick loop runs narrowphase + solver multiple times for tiers
     * that request more than the global substep count. */
    plan->tier_params[PHYS_TIER_0_DIRECT].substeps = 2;
    plan->tier_params[PHYS_TIER_1_NEAR].substeps   = 2;

    /* ANIM tier: TGS with XPBD-regularized coupled implicit velocity
     * solver for animated/ragdoll bodies.  The regularization terms
     * (compliance α, damping γ) in the velocity equation prevent
     * energy injection without needing a separate position solver.
     * phys_tier_cross_solver_mode() also returns TGS for TIER_ANIM. */
    plan->tier_params[PHYS_TIER_ANIM].solver_mode = PHYS_SOLVER_TGS;
    plan->tier_params[PHYS_TIER_ANIM].substeps    = cfg->default_substeps;
    /* TIER_ANIM uses 2× default iterations for the nonlinear coupled
     * solver — extra iterations are needed for FK propagation +
     * Jacobian re-linearization to converge to near-zero errors. */
    plan->tier_params[PHYS_TIER_ANIM].iterations   = cfg->default_solver_iterations * 2;
    plan->tier_params[PHYS_TIER_ANIM].compliance   = 0.0f;

    /* T4 amortized ticking: only active every 3rd frame. */
    if (world->tick_count % 3 != 0) {
        plan->tier_params[PHYS_TIER_4_BACKGROUND].active = false;
    }

    /* T5 (sleeping) is never simulated — bodies must be woken first. */
    plan->tier_params[PHYS_TIER_5_SLEEPING].active = false;
}
