/**
 * @file tier_cross.c
 * @brief Cross-tier solver mode resolution (phys-402).
 *
 * Determines which solver algorithm to use for a constraint
 * that spans two bodies in different tiers.
 */

#include "ferrum/physics/step_plan.h"
#include "ferrum/physics/tier_list.h"

/* ── Public API ─────────────────────────────────────────────────── */

phys_solver_mode_t phys_tier_cross_solver_mode(phys_tier_t tier_a,
                                               phys_tier_t tier_b)
{
    /* Use TGS if either body is in a high-fidelity tier (T0 or T1). */
    if (tier_a <= PHYS_TIER_1_NEAR || tier_b <= PHYS_TIER_1_NEAR) {
        return PHYS_SOLVER_TGS;
    }
    return PHYS_SOLVER_XPBD;
}
