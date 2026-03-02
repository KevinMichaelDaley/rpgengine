/**
 * @file phys_contact_begin.c
 * @brief Contact-begin detection from the manifold cache.
 *
 * Iterates settled manifold cache entries, upserts into pair set,
 * emits events for newly seen pairs, prunes stale entries.
 */
#include "ferrum/physics/phys_contact_begin.h"
#include "ferrum/physics/manifold_cache.h"
#include "ferrum/physics/manifold.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ── Public API ───────────────────────────────────────────────── */

bool phys_contact_begin_init(phys_contact_begin_ctx_t *ctx,
                             uint32_t pair_capacity,
                             uint32_t event_capacity) {
    if (!ctx || event_capacity == 0) return false;

    memset(ctx, 0, sizeof(*ctx));
    if (!phys_pair_set_init(&ctx->pair_set, pair_capacity)) return false;

    ctx->events = (phys_contact_begin_event_t *)calloc(
        event_capacity, sizeof(phys_contact_begin_event_t));
    if (!ctx->events) {
        phys_pair_set_destroy(&ctx->pair_set);
        return false;
    }
    ctx->event_capacity = event_capacity;
    ctx->event_count    = 0;
    return true;
}

void phys_contact_begin_destroy(phys_contact_begin_ctx_t *ctx) {
    if (!ctx) return;
    phys_pair_set_destroy(&ctx->pair_set);
    free(ctx->events);
    ctx->events         = NULL;
    ctx->event_count    = 0;
    ctx->event_capacity = 0;
}

void phys_contact_begin_update(phys_contact_begin_ctx_t *ctx,
                               const struct phys_manifold_cache *cache,
                               uint32_t current_tick) {
    if (!ctx || !cache) return;

    /* Reset event output. */
    ctx->event_count = 0;

    /* Iterate all active entries in the manifold cache. */
    uint32_t cache_count = phys_manifold_cache_count(cache);
    const phys_manifold_cache_entry_t *entries = cache->entries;

    for (uint32_t i = 0; i < cache_count; i++) {
        const phys_manifold_cache_entry_t *e = &entries[i];

        /* Only consider entries active this tick. */
        if (e->last_used_tick != current_tick) continue;

        /* Try to upsert into the pair set. */
        bool was_new = phys_pair_set_upsert(
            &ctx->pair_set, e->pair_key, current_tick);

        if (was_new && ctx->event_count < ctx->event_capacity) {
            /* New contact — emit event. */
            const phys_manifold_t *m = &e->manifold;
            phys_contact_begin_event_t *ev = &ctx->events[ctx->event_count++];
            ev->body_a = m->body_a;
            ev->body_b = m->body_b;

            /* Use the first contact point. */
            if (m->point_count > 0) {
                ev->point   = m->points[0].point_world;
                ev->normal  = m->points[0].normal;
                ev->impulse = fabsf(m->normal_impulse[0]);
            } else {
                ev->point   = (phys_vec3_t){0, 0, 0};
                ev->normal  = (phys_vec3_t){0, 1, 0};
                ev->impulse = 0.0f;
            }
        }
    }

    /* Prune pairs not seen this tick (contact lost). */
    phys_pair_set_prune_before(&ctx->pair_set, current_tick);
}

uint32_t phys_contact_begin_count(const phys_contact_begin_ctx_t *ctx) {
    return ctx ? ctx->event_count : 0;
}

const phys_contact_begin_event_t *phys_contact_begin_events(
    const phys_contact_begin_ctx_t *ctx) {
    return ctx ? ctx->events : NULL;
}
