/** @file
 * @brief Tier list initialization, mutation, and clearing.
 *
 * Provides init, add, clear, and clear_all for tier lists.
 * Query functions live in tier_list_query.c.
 */

#include "ferrum/physics/tier_list.h"

#include <stdalign.h>
#include <stddef.h>

#include "ferrum/physics/phys_pool.h" /* phys_frame_arena_alloc */

/* ── Init ───────────────────────────────────────────────────────── */

void phys_tier_lists_init(phys_tier_lists_t *lists,
                          struct phys_frame_arena *arena,
                          uint32_t max_bodies) {
    if (!lists || !arena) {
        return;
    }

    for (int t = 0; t < PHYS_TIER_COUNT; ++t) {
        phys_tier_list_t *tier = &lists->tiers[t];
        size_t alloc_size = (size_t)max_bodies * sizeof(uint32_t);
        void *mem = phys_frame_arena_alloc(arena, alloc_size,
                                           alignof(uint32_t));
        if (mem) {
            tier->indices = (uint32_t *)mem;
            tier->capacity = max_bodies;
        } else {
            tier->indices = NULL;
            tier->capacity = 0;
        }
        tier->count = 0;
    }
}

/* ── Add ────────────────────────────────────────────────────────── */

void phys_tier_list_add(phys_tier_list_t *list, uint32_t body_index) {
    if (!list || !list->indices) {
        return;
    }
    if (list->count < list->capacity) {
        list->indices[list->count] = body_index;
        list->count++;
    }
}

/* ── Clear ──────────────────────────────────────────────────────── */

void phys_tier_list_clear(phys_tier_list_t *list) {
    if (!list) {
        return;
    }
    list->count = 0;
}

/* ── Clear all ──────────────────────────────────────────────────── */

void phys_tier_lists_clear_all(phys_tier_lists_t *lists) {
    if (!lists) {
        return;
    }
    for (int t = 0; t < PHYS_TIER_COUNT; ++t) {
        phys_tier_list_clear(&lists->tiers[t]);
    }
}
