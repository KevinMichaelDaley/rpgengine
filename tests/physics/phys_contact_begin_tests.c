/**
 * @file phys_contact_begin_tests.c
 * @brief Tests for phys_contact_begin — contact-begin detection from manifold cache.
 *
 * Tests cover: new contact fires, sustained does not, lost+regained fires,
 * multiple contacts, empty cache, prune lifecycle.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "ferrum/physics/phys_contact_begin.h"
#include "ferrum/physics/manifold_cache.h"
#include "ferrum/physics/manifold.h"

/* ── Test harness ─────────────────────────────────────────────── */

static int g_pass = 0, g_fail = 0;

#define ASSERT_TRUE(cond) do {                                     \
    if (!(cond)) {                                                 \
        fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__,__LINE__,  \
                #cond);                                            \
        g_fail++; return;                                          \
    }                                                              \
} while (0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a,b)     ASSERT_TRUE((a) == (b))

#define RUN(fn) do {                \
    printf("  %-50s ", #fn);        \
    fn();                           \
    printf("OK\n"); g_pass++;       \
} while (0)

/* ── Helpers ──────────────────────────────────────────────────── */

/** Add a contact pair to the manifold cache at the given tick. */
static void add_manifold(phys_manifold_cache_t *cache,
                         uint32_t body_a, uint32_t body_b,
                         uint32_t tick,
                         float px, float py, float pz,
                         float nx, float ny, float nz) {
    phys_manifold_t *m = phys_manifold_cache_get_or_create(
        cache, body_a, body_b, tick);
    if (!m) return;
    m->body_a = body_a;
    m->body_b = body_b;
    m->point_count = 1;
    m->points[0].point_world = (phys_vec3_t){px, py, pz};
    m->points[0].normal      = (phys_vec3_t){nx, ny, nz};
    m->points[0].penetration = 0.01f;
    m->normal_impulse[0] = 5.0f;
    phys_manifold_cache_touch(cache, body_a, body_b, tick);
}

/* ── Tests ────────────────────────────────────────────────────── */

/** Init and destroy without crash. */
static void test_init_destroy(void) {
    phys_contact_begin_ctx_t ctx;
    ASSERT_TRUE(phys_contact_begin_init(&ctx, 64, 32));
    ASSERT_EQ(phys_contact_begin_count(&ctx), 0u);
    phys_contact_begin_destroy(&ctx);
}

/** New contact pair produces exactly one event. */
static void test_new_contact_fires(void) {
    phys_contact_begin_ctx_t ctx;
    phys_contact_begin_init(&ctx, 64, 32);

    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, 64);

    /* Tick 1: bodies 0 and 1 collide. */
    add_manifold(&cache, 0, 1, 1, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    phys_contact_begin_update(&ctx, &cache, 1);

    ASSERT_EQ(phys_contact_begin_count(&ctx), 1u);
    const phys_contact_begin_event_t *ev = phys_contact_begin_events(&ctx);
    ASSERT_EQ(ev[0].body_a, 0u);
    ASSERT_EQ(ev[0].body_b, 1u);

    phys_manifold_cache_destroy(&cache);
    phys_contact_begin_destroy(&ctx);
}

/** Sustained contact (same pair, next tick) does NOT fire again. */
static void test_sustained_no_refire(void) {
    phys_contact_begin_ctx_t ctx;
    phys_contact_begin_init(&ctx, 64, 32);
    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, 64);

    /* Tick 1: contact begins. */
    add_manifold(&cache, 0, 1, 1, 0, 0, 0, 0, 1, 0);
    phys_contact_begin_update(&ctx, &cache, 1);
    ASSERT_EQ(phys_contact_begin_count(&ctx), 1u);

    /* Tick 2: same contact sustained. */
    phys_manifold_cache_touch(&cache, 0, 1, 2);
    phys_contact_begin_update(&ctx, &cache, 2);
    ASSERT_EQ(phys_contact_begin_count(&ctx), 0u);

    /* Tick 3: still sustained. */
    phys_manifold_cache_touch(&cache, 0, 1, 3);
    phys_contact_begin_update(&ctx, &cache, 3);
    ASSERT_EQ(phys_contact_begin_count(&ctx), 0u);

    phys_manifold_cache_destroy(&cache);
    phys_contact_begin_destroy(&ctx);
}

/** Contact lost then regained fires again. */
static void test_lost_and_regained(void) {
    phys_contact_begin_ctx_t ctx;
    phys_contact_begin_init(&ctx, 64, 32);
    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, 64);

    /* Tick 1: contact. */
    add_manifold(&cache, 0, 1, 1, 0, 0, 0, 0, 1, 0);
    phys_contact_begin_update(&ctx, &cache, 1);
    ASSERT_EQ(phys_contact_begin_count(&ctx), 1u);

    /* Tick 2: contact lost (expire from cache). */
    phys_manifold_cache_expire(&cache, 2, 0); /* expire entries not touched at tick 2 */
    phys_contact_begin_update(&ctx, &cache, 2);
    ASSERT_EQ(phys_contact_begin_count(&ctx), 0u);

    /* Tick 3: contact regained — should fire again. */
    add_manifold(&cache, 0, 1, 3, 0, 0, 0, 0, 1, 0);
    phys_contact_begin_update(&ctx, &cache, 3);
    ASSERT_EQ(phys_contact_begin_count(&ctx), 1u);

    phys_manifold_cache_destroy(&cache);
    phys_contact_begin_destroy(&ctx);
}

/** Multiple contact-begin events in one tick. */
static void test_multiple_new_contacts(void) {
    phys_contact_begin_ctx_t ctx;
    phys_contact_begin_init(&ctx, 64, 32);
    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, 64);

    /* Tick 1: three new contacts. */
    add_manifold(&cache, 0, 1, 1, 0, 0, 0, 0, 1, 0);
    add_manifold(&cache, 2, 3, 1, 1, 0, 0, 0, 1, 0);
    add_manifold(&cache, 4, 5, 1, 2, 0, 0, 0, 1, 0);
    phys_contact_begin_update(&ctx, &cache, 1);
    ASSERT_EQ(phys_contact_begin_count(&ctx), 3u);

    phys_manifold_cache_destroy(&cache);
    phys_contact_begin_destroy(&ctx);
}

/** Mix of new and sustained contacts. */
static void test_mixed_new_and_sustained(void) {
    phys_contact_begin_ctx_t ctx;
    phys_contact_begin_init(&ctx, 64, 32);
    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, 64);

    /* Tick 1: two contacts. */
    add_manifold(&cache, 0, 1, 1, 0, 0, 0, 0, 1, 0);
    add_manifold(&cache, 2, 3, 1, 0, 0, 0, 0, 1, 0);
    phys_contact_begin_update(&ctx, &cache, 1);
    ASSERT_EQ(phys_contact_begin_count(&ctx), 2u);

    /* Tick 2: (0,1) sustained, (2,3) sustained, (4,5) new. */
    phys_manifold_cache_touch(&cache, 0, 1, 2);
    phys_manifold_cache_touch(&cache, 2, 3, 2);
    add_manifold(&cache, 4, 5, 2, 0, 0, 0, 0, 1, 0);
    phys_contact_begin_update(&ctx, &cache, 2);
    ASSERT_EQ(phys_contact_begin_count(&ctx), 1u);

    const phys_contact_begin_event_t *ev = phys_contact_begin_events(&ctx);
    ASSERT_EQ(ev[0].body_a, 4u);
    ASSERT_EQ(ev[0].body_b, 5u);

    phys_manifold_cache_destroy(&cache);
    phys_contact_begin_destroy(&ctx);
}

/** Empty manifold cache produces no events. */
static void test_empty_cache(void) {
    phys_contact_begin_ctx_t ctx;
    phys_contact_begin_init(&ctx, 64, 32);
    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, 64);

    phys_contact_begin_update(&ctx, &cache, 1);
    ASSERT_EQ(phys_contact_begin_count(&ctx), 0u);

    phys_manifold_cache_destroy(&cache);
    phys_contact_begin_destroy(&ctx);
}

/** Event payload has correct contact point and normal. */
static void test_event_payload(void) {
    phys_contact_begin_ctx_t ctx;
    phys_contact_begin_init(&ctx, 64, 32);
    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, 64);

    add_manifold(&cache, 10, 20, 1,
                 3.0f, 4.0f, 5.0f,   /* point */
                 0.0f, 0.0f, 1.0f);  /* normal */
    phys_contact_begin_update(&ctx, &cache, 1);

    const phys_contact_begin_event_t *ev = phys_contact_begin_events(&ctx);
    ASSERT_EQ(ev[0].body_a, 10u);
    ASSERT_EQ(ev[0].body_b, 20u);
    ASSERT_TRUE(ev[0].point.x == 3.0f);
    ASSERT_TRUE(ev[0].point.y == 4.0f);
    ASSERT_TRUE(ev[0].point.z == 5.0f);
    ASSERT_TRUE(ev[0].normal.x == 0.0f);
    ASSERT_TRUE(ev[0].normal.y == 0.0f);
    ASSERT_TRUE(ev[0].normal.z == 1.0f);
    ASSERT_TRUE(ev[0].impulse == 5.0f);

    phys_manifold_cache_destroy(&cache);
    phys_contact_begin_destroy(&ctx);
}

/** Many ticks lifecycle — insert, sustain, lose, regain across 10 ticks. */
static void test_lifecycle_10_ticks(void) {
    phys_contact_begin_ctx_t ctx;
    phys_contact_begin_init(&ctx, 64, 32);
    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, 64);

    /* Tick 1: A-B contact. */
    add_manifold(&cache, 0, 1, 1, 0, 0, 0, 0, 1, 0);
    phys_contact_begin_update(&ctx, &cache, 1);
    ASSERT_EQ(phys_contact_begin_count(&ctx), 1u);

    /* Ticks 2-5: sustained. */
    for (uint32_t t = 2; t <= 5; t++) {
        phys_manifold_cache_touch(&cache, 0, 1, t);
        phys_contact_begin_update(&ctx, &cache, t);
        ASSERT_EQ(phys_contact_begin_count(&ctx), 0u);
    }

    /* Tick 6: contact lost. */
    phys_manifold_cache_expire(&cache, 6, 0);
    phys_contact_begin_update(&ctx, &cache, 6);
    ASSERT_EQ(phys_contact_begin_count(&ctx), 0u);

    /* Ticks 7-8: still no contact. */
    phys_contact_begin_update(&ctx, &cache, 7);
    ASSERT_EQ(phys_contact_begin_count(&ctx), 0u);
    phys_contact_begin_update(&ctx, &cache, 8);
    ASSERT_EQ(phys_contact_begin_count(&ctx), 0u);

    /* Tick 9: contact regained! */
    add_manifold(&cache, 0, 1, 9, 0, 0, 0, 0, 1, 0);
    phys_contact_begin_update(&ctx, &cache, 9);
    ASSERT_EQ(phys_contact_begin_count(&ctx), 1u);

    /* Tick 10: sustained again. */
    phys_manifold_cache_touch(&cache, 0, 1, 10);
    phys_contact_begin_update(&ctx, &cache, 10);
    ASSERT_EQ(phys_contact_begin_count(&ctx), 0u);

    phys_manifold_cache_destroy(&cache);
    phys_contact_begin_destroy(&ctx);
}

/** Event buffer overflow: more new contacts than event_capacity. */
static void test_event_overflow(void) {
    phys_contact_begin_ctx_t ctx;
    phys_contact_begin_init(&ctx, 128, 4); /* only 4 event slots */
    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, 128);

    /* 8 new contacts but only 4 event slots. */
    for (uint32_t i = 0; i < 8; i++) {
        add_manifold(&cache, i, i + 100, 1, 0, 0, 0, 0, 1, 0);
    }
    phys_contact_begin_update(&ctx, &cache, 1);

    /* Should cap at event_capacity. */
    ASSERT_TRUE(phys_contact_begin_count(&ctx) <= 4u);

    phys_manifold_cache_destroy(&cache);
    phys_contact_begin_destroy(&ctx);
}

/* ── Main ─────────────────────────────────────────────────────── */

int main(void) {
    printf("=== phys_contact_begin_tests ===\n");
    RUN(test_init_destroy);
    RUN(test_new_contact_fires);
    RUN(test_sustained_no_refire);
    RUN(test_lost_and_regained);
    RUN(test_multiple_new_contacts);
    RUN(test_mixed_new_and_sustained);
    RUN(test_empty_cache);
    RUN(test_event_payload);
    RUN(test_lifecycle_10_ticks);
    RUN(test_event_overflow);
    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
