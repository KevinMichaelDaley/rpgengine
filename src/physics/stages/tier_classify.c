/**
 * @file tier_classify.c
 * @brief Stage 1 implementation — base tier classification.
 *
 * Phase 1 logic:
 *   - Static bodies  → excluded (not in any tier list)
 *   - Sleeping bodies → T5
 *   - Dynamic bodies  → T0
 */

#include "ferrum/physics/tier_classify.h"

#include "ferrum/physics/body.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/tier_list.h"

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
        } else {
            /* Phase 1: all non-sleeping dynamic bodies → T0. */
            phys_tier_list_add(
                &args->tier_lists_out->tiers[PHYS_TIER_0_DIRECT], i);
        }
    }
}
