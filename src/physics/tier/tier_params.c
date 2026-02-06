/**
 * @file tier_params.c
 * @brief Default per-tier solver parameters (phys-402).
 *
 * Returns a static table of parameters keyed by phys_tier_t.
 * Pure function — no side effects, no heap allocation.
 */

#include "ferrum/physics/step_plan.h"
#include "ferrum/physics/tier_list.h"

/* ── Static parameter table ─────────────────────────────────────── */

/**
 * Default parameters for tiers T0–T4.
 * Index maps directly to phys_tier_t enum values 0–4.
 */
static const phys_tier_params_t TIER_DEFAULTS[5] = {
    /* T0: Direct manipulation — highest fidelity. */
    {
        .active      = true,
        .solver_mode = PHYS_SOLVER_TGS,
        .substeps    = 3,
        .iterations  = 24,
        .compliance  = 0.0f,
        .friction_boost    = 1.0f,
        .restitution_scale = 1.0f,
    },
    /* T1: Near interactive. */
    {
        .active      = true,
        .solver_mode = PHYS_SOLVER_TGS,
        .substeps    = 2,
        .iterations  = 20,
        .compliance  = 0.0f,
        .friction_boost    = 1.0f,
        .restitution_scale = 1.0f,
    },
    /* T2: Visible / hazardous. */
    {
        .active      = true,
        .solver_mode = PHYS_SOLVER_XPBD,
        .substeps    = 1,
        .iterations  = 8,
        .compliance  = 1e-6f,
        .friction_boost    = 1.0f,
        .restitution_scale = 1.0f,
    },
    /* T3: World-shaping. */
    {
        .active      = true,
        .solver_mode = PHYS_SOLVER_XPBD,
        .substeps    = 1,
        .iterations  = 4,
        .compliance  = 1e-5f,
        .friction_boost    = 1.0f,
        .restitution_scale = 1.0f,
    },
    /* T4: Background dynamic. */
    {
        .active      = true,
        .solver_mode = PHYS_SOLVER_XPBD,
        .substeps    = 1,
        .iterations  = 2,
        .compliance  = 1e-4f,
        .friction_boost    = 1.0f,
        .restitution_scale = 1.0f,
    },
};

/* ── Public API ─────────────────────────────────────────────────── */

phys_tier_params_t phys_get_tier_params(phys_tier_t tier)
{
    if ((int)tier >= 0 && (int)tier <= 4) {
        return TIER_DEFAULTS[(int)tier];
    }

    /* Fallback for T5 (sleeping) or out-of-range: inactive defaults. */
    phys_tier_params_t fallback = {
        .active      = false,
        .solver_mode = PHYS_SOLVER_TGS,
        .substeps    = 1,
        .iterations  = 1,
        .compliance  = 0.0f,
        .friction_boost    = 1.0f,
        .restitution_scale = 1.0f,
    };
    return fallback;
}
