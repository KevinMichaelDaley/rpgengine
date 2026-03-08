/**
 * @file tier_classify.c
 * @brief Stage 1 implementation — distance-based tier classification.
 *
 * Classification logic:
 *   - Static bodies  → excluded (not in any tier list)
 *   - Sleeping bodies → T5 (regardless of distance)
 *   - Dynamic bodies  → T0–T4 by distance to nearest player
 *   - Fallback (no game state / no players) → T0
 *
 * Hysteresis: when a body would be demoted (moved to a higher tier number),
 * the distance threshold is widened by 20% to prevent flapping.
 */

#include "ferrum/physics/tier_classify.h"

#include <stdbool.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/game_state.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/tier_list.h"

/** Number of distance thresholds (T0–T3 boundaries; T4 is the catch-all). */
#define TIER_THRESHOLD_COUNT 4

/** Exact promotion thresholds for tiers T0–T3.
 *  Indexed by (tier - PHYS_TIER_0_DIRECT). */
static const float tier_thresholds[TIER_THRESHOLD_COUNT] = {
    5.0f,   /* T0: < 5m   */
    20.0f,  /* T1: < 20m  */
    50.0f,  /* T2: < 50m  */
    200.0f, /* T3: < 200m */
};

/** Demotion thresholds (promotion threshold * 1.2).
 *  Indexed by (tier - PHYS_TIER_0_DIRECT). */
static const float tier_demotion_thresholds[TIER_THRESHOLD_COUNT] = {
    6.0f,   /* T0 demotion: must exceed 6m   */
    24.0f,  /* T1 demotion: must exceed 24m  */
    60.0f,  /* T2 demotion: must exceed 60m  */
    240.0f, /* T3 demotion: must exceed 240m */
};

/**
 * @brief Determine the raw tier from distance (no hysteresis).
 */
static phys_tier_t tier_from_distance(float dist) {
    if (dist < tier_thresholds[0]) { return PHYS_TIER_0_DIRECT; }
    if (dist < tier_thresholds[1]) { return PHYS_TIER_1_NEAR; }
    if (dist < tier_thresholds[2]) { return PHYS_TIER_2_VISIBLE; }
    if (dist < tier_thresholds[3]) { return PHYS_TIER_3_WORLD; }
    return PHYS_TIER_4_BACKGROUND;
}

/**
 * @brief Apply hysteresis: resist demotion unless distance exceeds
 *        the widened threshold for the current tier.
 */
static phys_tier_t apply_hysteresis(phys_tier_t new_tier,
                                    phys_tier_t current_tier,
                                    float dist) {
    /* Only apply hysteresis for demotion (new tier farther than current). */
    if (new_tier <= current_tier) { return new_tier; }
    if (current_tier >= PHYS_TIER_5_SLEEPING) { return new_tier; }
    /* ANIM tier is not distance-based — no hysteresis. */
    if (current_tier == PHYS_TIER_ANIM) { return new_tier; }

    /* Check if distance exceeds the demotion threshold for current tier.
     * Threshold arrays are indexed starting at T0. */
    int thresh_idx = (int)current_tier - (int)PHYS_TIER_0_DIRECT;
    if (thresh_idx >= 0 && thresh_idx < TIER_THRESHOLD_COUNT &&
        dist < tier_demotion_thresholds[thresh_idx]) {
        return current_tier;  /* Stay at current tier. */
    }
    return new_tier;
}

void phys_stage_tier_classify(const phys_tier_classify_args_t *args) {
    if (!args || !args->tier_lists_out || !args->arena) {
        return;
    }

    /* Allocate tier list index arrays from the frame arena. */
    phys_tier_lists_init(args->tier_lists_out, args->arena, args->body_count);

    for (uint32_t i = 0; i < args->body_count; ++i) {
        /* Skip inactive pool slots. */
        if (args->active && !args->active[i]) {
            continue;
        }

        const phys_body_t *body = &args->bodies[i];

        /* Static bodies are always available — they don't need a tier. */
        if (phys_body_is_static(body)) {
            continue;
        }

        if (phys_body_is_sleeping(body)) {
            phys_tier_list_add(
                &args->tier_lists_out->tiers[PHYS_TIER_5_SLEEPING], i);
            if (args->tier_out) {
                args->tier_out[i] = (uint8_t)PHYS_TIER_5_SLEEPING;
            }
            continue;
        }

        /* Animated bodies stay in ANIM tier — not distance-based. */
        if (body->tier == PHYS_TIER_ANIM) {
            phys_tier_list_add(
                &args->tier_lists_out->tiers[PHYS_TIER_ANIM], i);
            if (args->tier_out) {
                args->tier_out[i] = (uint8_t)PHYS_TIER_ANIM;
            }
            continue;
        }

        /* Determine tier by distance to nearest player. */
        phys_tier_t new_tier;
        if (!args->game || args->game->player_count == 0) {
            /* Fallback: no game state → Phase 1 behavior (all T0). */
            new_tier = PHYS_TIER_0_DIRECT;
        } else {
            float dist = phys_game_state_distance_to_nearest_player(
                args->game, body->position);
            phys_tier_t raw_tier = tier_from_distance(dist);
            phys_tier_t current_tier = (phys_tier_t)body->tier;
            new_tier = apply_hysteresis(raw_tier, current_tier, dist);
        }

        phys_tier_t old_tier = (phys_tier_t)body->tier;

        /* Occlusion demotion: T0–T1 bodies that are not visible → demote to T3. */
        if (args->visibility_set && new_tier <= PHYS_TIER_1_NEAR) {
            uint32_t byte_idx = i / 8;
            uint8_t  bit_mask = (uint8_t)(1u << (i % 8));
            bool visible = (args->visibility_set[byte_idx] & bit_mask) != 0;
            if (!visible) {
                new_tier = PHYS_TIER_3_WORLD;
            }
        }

        /* Re-promotion: body was T3 (occluded) but is now visible and
         * distance says T0–T1 → flag for position nudge. */
        if (args->visibility_set &&
            old_tier == PHYS_TIER_3_WORLD &&
            new_tier <= PHYS_TIER_1_NEAR) {
            if (args->repromotion_flags) {
                args->repromotion_flags[i] = 1;
            }
        }

        phys_tier_list_add(
            &args->tier_lists_out->tiers[new_tier], i);
        if (args->tier_out) {
            args->tier_out[i] = (uint8_t)new_tier;
        }
    }
}
