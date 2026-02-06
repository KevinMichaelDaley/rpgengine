/**
 * @file tier_stabilization.c
 * @brief Per-tier stabilization parameter lookup.
 *
 * Maps simulation tiers (T0–T4) to friction boost and velocity damping
 * factors.  T0 gets the strongest stabilization to prevent micro-sliding;
 * T4 gets minimal stabilization to save compute.
 */

#include "ferrum/physics/stabilization.h"

#include "ferrum/physics/tier_list.h"

/** Per-tier friction boost values (indexed by tier). */
static const float TIER_FRICTION_BOOST[] = {
    3.0f,  /* T0: Direct manipulation — strongest. */
    2.0f,  /* T1: Near interactive. */
    1.5f,  /* T2: Visible / hazardous. */
    1.0f,  /* T3: World-shaping — minimal. */
    1.0f,  /* T4: Background — minimal. */
    1.0f,  /* T5: Sleeping (fallback). */
};

/** Per-tier velocity damping values (indexed by tier). */
static const float TIER_VELOCITY_DAMPING[] = {
    0.98f, /* T0: Direct manipulation — strongest. */
    0.97f, /* T1: Near interactive. */
    0.95f, /* T2: Visible / hazardous. */
    0.90f, /* T3: World-shaping — minimal. */
    0.85f, /* T4: Background — minimal, saves compute. */
    0.85f, /* T5: Sleeping (fallback). */
};

void phys_tier_stabilization_params(phys_tier_t tier,
                                    float *friction_boost,
                                    float *velocity_damping)
{
    /* Clamp to valid range. */
    int idx = (int)tier;
    if (idx < 0) {
        idx = 0;
    }
    if (idx >= PHYS_TIER_COUNT) {
        idx = PHYS_TIER_COUNT - 1;
    }

    *friction_boost   = TIER_FRICTION_BOOST[idx];
    *velocity_damping = TIER_VELOCITY_DAMPING[idx];
}
