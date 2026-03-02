/**
 * @file phys_overlap_begin.c
 * @brief Overlap-begin detection via callback-based overlap testing.
 *
 * Tests candidate body pairs for interior overlap, tracks which pairs
 * are currently overlapping in a pair set, and emits events only when
 * a pair first begins overlapping.
 */
#include "ferrum/physics/phys_overlap_begin.h"
#include <stdlib.h>
#include <string.h>

/* ── Helpers ──────────────────────────────────────────────────── */

/** Build a canonical pair key from two body indices. */
static uint64_t make_pair_key(uint32_t a, uint32_t b) {
    uint32_t lo = a < b ? a : b;
    uint32_t hi = a < b ? b : a;
    return ((uint64_t)lo << 32) | (uint64_t)hi;
}

/* ── Public API ───────────────────────────────────────────────── */

bool phys_overlap_begin_init(phys_overlap_begin_ctx_t *ctx,
                             uint32_t pair_capacity,
                             uint32_t event_capacity) {
    if (!ctx || event_capacity == 0) return false;

    memset(ctx, 0, sizeof(*ctx));
    if (!phys_pair_set_init(&ctx->pair_set, pair_capacity)) return false;

    ctx->events = (phys_overlap_begin_event_t *)calloc(
        event_capacity, sizeof(phys_overlap_begin_event_t));
    if (!ctx->events) {
        phys_pair_set_destroy(&ctx->pair_set);
        return false;
    }
    ctx->event_capacity = event_capacity;
    ctx->event_count    = 0;
    return true;
}

void phys_overlap_begin_destroy(phys_overlap_begin_ctx_t *ctx) {
    if (!ctx) return;
    phys_pair_set_destroy(&ctx->pair_set);
    free(ctx->events);
    ctx->events         = NULL;
    ctx->event_count    = 0;
    ctx->event_capacity = 0;
}

void phys_overlap_begin_update(phys_overlap_begin_ctx_t *ctx,
                               phys_overlap_test_fn test_fn,
                               void *test_ctx,
                               const phys_overlap_pair_t *pairs,
                               uint32_t pair_count,
                               uint32_t current_tick) {
    if (!ctx) return;
    ctx->event_count = 0;

    if (!test_fn || !pairs || pair_count == 0) {
        /* No pairs to test — prune stale entries. */
        phys_pair_set_prune_before(&ctx->pair_set, current_tick);
        return;
    }

    for (uint32_t i = 0; i < pair_count; i++) {
        uint32_t a = pairs[i].body_a;
        uint32_t b = pairs[i].body_b;
        phys_vec3_t center = {0, 0, 0};

        bool overlaps = test_fn(test_ctx, a, b, &center);
        if (!overlaps) continue;

        /* Overlapping — upsert into pair set. */
        uint64_t key = make_pair_key(a, b);
        bool was_new = phys_pair_set_upsert(&ctx->pair_set, key, current_tick);

        if (was_new && ctx->event_count < ctx->event_capacity) {
            phys_overlap_begin_event_t *ev = &ctx->events[ctx->event_count++];
            ev->body_a = a;
            ev->body_b = b;
            ev->center = center;
        }
    }

    /* Prune pairs not seen this tick (overlap ended). */
    phys_pair_set_prune_before(&ctx->pair_set, current_tick);
}

uint32_t phys_overlap_begin_count(const phys_overlap_begin_ctx_t *ctx) {
    return ctx ? ctx->event_count : 0;
}

const phys_overlap_begin_event_t *phys_overlap_begin_events(
    const phys_overlap_begin_ctx_t *ctx) {
    return ctx ? ctx->events : NULL;
}
