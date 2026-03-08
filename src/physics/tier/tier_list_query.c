/** @file
 * @brief Tier list query functions.
 *
 * Provides read-only queries over tier lists.
 * Mutation functions live in tier_list.c.
 */

#include "ferrum/physics/tier_list.h"

/* ── Total active count ─────────────────────────────────────────── */

uint32_t phys_tier_lists_total_active(const phys_tier_lists_t *lists) {
    if (!lists) {
        return 0;
    }

    uint32_t total = 0;
    /* Sum all active tiers (ANIM through T4); skip T5 (sleeping). */
    for (int t = PHYS_TIER_ANIM; t <= PHYS_TIER_4_BACKGROUND; ++t) {
        total += lists->tiers[t].count;
    }
    return total;
}
