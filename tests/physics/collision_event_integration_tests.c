/**
 * @file collision_event_integration_tests.c
 * @brief Integration tests for the full hit!/overlap! event pipeline.
 *
 * Tests the chain:
 *   manifold_cache → phys_contact_begin → entity flag filter → event output
 *   overlap_test   → phys_overlap_begin → entity flag filter → event output
 *
 * Also tests:
 *   - Events only fire for flagged entities
 *   - Non-flagged contacts produce no events
 *   - Mixed flagged/unflagged entities
 *   - Overlap events use callback correctly
 *   - Sustained contacts/overlaps do not re-fire
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>

#include "ferrum/physics/phys_contact_begin.h"
#include "ferrum/physics/phys_overlap_begin.h"
#include "ferrum/physics/manifold_cache.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/entity/entity_event_flags.h"

/* ── Test harness ─────────────────────────────────────────────── */

static int g_pass = 0, g_fail = 0;

#define ASSERT_TRUE(cond) do {                                     \
    if (!(cond)) {                                                 \
        fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__,__LINE__,  \
                #cond);                                            \
        g_fail++; return;                                          \
    }                                                              \
} while (0)

#define ASSERT_FALSE(cond)  ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a,b)      ASSERT_TRUE((a) == (b))

#define RUN(fn) do {                \
    printf("  %-50s ", #fn);        \
    fn();                           \
    printf("OK\n"); g_pass++;       \
} while (0)

/* ── Helpers ──────────────────────────────────────────────────── */

/** Add a manifold entry to the cache and touch it. */
static void add_manifold(phys_manifold_cache_t *cache,
                         uint32_t body_a, uint32_t body_b,
                         uint32_t tick) {
    phys_manifold_t *m = phys_manifold_cache_get_or_create(
        cache, body_a, body_b, tick);
    if (!m) return;
    m->body_a = body_a;
    m->body_b = body_b;
    m->point_count = 1;
    m->points[0].point_world = (phys_vec3_t){1.0f, 2.0f, 3.0f};
    m->points[0].normal      = (phys_vec3_t){0.0f, 1.0f, 0.0f};
    m->points[0].penetration = 0.01f;
    m->normal_impulse[0] = 10.0f;
    phys_manifold_cache_touch(cache, body_a, body_b, tick);
}

/** Set or clear event flags on an entity_attrs_t. */
static void set_event_flags(entity_attrs_t *attrs, uint32_t flags) {
    entity_attrs_set(attrs, ENTITY_ATTR_KEY_EVENT_FLAGS,
                     SCRIPT_ATTR_U32, &flags, sizeof(flags));
}

/** Simple mock overlap: returns true if body_a < 10 and body_b < 10. */
static bool mock_overlap(void *ctx, uint32_t body_a, uint32_t body_b,
                         phys_vec3_t *out_center) {
    (void)ctx;
    if (body_a < 10 && body_b < 10) {
        if (out_center) *out_center = (phys_vec3_t){0, 0, 0};
        return true;
    }
    return false;
}

/* ── hit! integration tests ───────────────────────────────────── */

/**
 * Full pipeline: manifold_cache → contact_begin → flag filter.
 * Two bodies collide; one has hit flag → event passes filter.
 */
static void test_hit_full_pipeline_flagged(void) {
    /* Setup manifold cache. */
    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, 64);

    /* Setup contact_begin tracker. */
    phys_contact_begin_ctx_t cb;
    phys_contact_begin_init(&cb, 64, 32);

    /* Setup entity attrs — body 1 has hit flag. */
    entity_attrs_t attrs[2];
    memset(attrs, 0, sizeof(attrs));
    entity_attrs_init(&attrs[0]);
    entity_attrs_init(&attrs[1]);
    set_event_flags(&attrs[1], ENTITY_EVENT_FLAG_HIT);

    /* Tick 1: bodies 0 and 1 collide. */
    add_manifold(&cache, 0, 1, 1);
    phys_contact_begin_update(&cb, &cache, 1);

    /* Should detect 1 new contact. */
    ASSERT_EQ(phys_contact_begin_count(&cb), 1u);

    /* Filter by entity flags — body 1 is flagged. */
    const phys_contact_begin_event_t *ev = phys_contact_begin_events(&cb);
    bool a_flagged = entity_has_event_flag(&attrs[ev[0].body_a],
                                           ENTITY_EVENT_FLAG_HIT);
    bool b_flagged = entity_has_event_flag(&attrs[ev[0].body_b],
                                           ENTITY_EVENT_FLAG_HIT);
    ASSERT_TRUE(a_flagged || b_flagged);

    /* Cleanup. */
    phys_contact_begin_destroy(&cb);
    phys_manifold_cache_destroy(&cache);
}

/**
 * Two bodies collide but NEITHER has hit flag → event filtered out.
 */
static void test_hit_full_pipeline_unflagged(void) {
    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, 64);

    phys_contact_begin_ctx_t cb;
    phys_contact_begin_init(&cb, 64, 32);

    /* No entity flags set. */
    entity_attrs_t attrs[2];
    memset(attrs, 0, sizeof(attrs));
    entity_attrs_init(&attrs[0]);
    entity_attrs_init(&attrs[1]);

    add_manifold(&cache, 0, 1, 1);
    phys_contact_begin_update(&cb, &cache, 1);

    ASSERT_EQ(phys_contact_begin_count(&cb), 1u);

    /* Filter: neither body is flagged → event discarded. */
    const phys_contact_begin_event_t *ev = phys_contact_begin_events(&cb);
    bool a_flagged = entity_has_event_flag(&attrs[ev[0].body_a],
                                           ENTITY_EVENT_FLAG_HIT);
    bool b_flagged = entity_has_event_flag(&attrs[ev[0].body_b],
                                           ENTITY_EVENT_FLAG_HIT);
    ASSERT_FALSE(a_flagged || b_flagged);

    phys_contact_begin_destroy(&cb);
    phys_manifold_cache_destroy(&cache);
}

/**
 * Multiple collisions; mixed flagged/unflagged entities.
 * Only events involving at least one flagged entity should pass.
 */
static void test_hit_mixed_flags(void) {
    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, 64);

    phys_contact_begin_ctx_t cb;
    phys_contact_begin_init(&cb, 64, 32);

    /* Bodies 0,1,2,3. Only body 2 has hit flag. */
    entity_attrs_t attrs[4];
    for (int i = 0; i < 4; i++) {
        memset(&attrs[i], 0, sizeof(attrs[i]));
        entity_attrs_init(&attrs[i]);
    }
    set_event_flags(&attrs[2], ENTITY_EVENT_FLAG_HIT);

    /* Pairs: (0,1) unflagged, (2,3) body 2 flagged. */
    add_manifold(&cache, 0, 1, 1);
    add_manifold(&cache, 2, 3, 1);
    phys_contact_begin_update(&cb, &cache, 1);

    ASSERT_EQ(phys_contact_begin_count(&cb), 2u);

    /* Count how many pass the entity flag filter. */
    uint32_t passed = 0;
    for (uint32_t i = 0; i < phys_contact_begin_count(&cb); i++) {
        const phys_contact_begin_event_t *e = &phys_contact_begin_events(&cb)[i];
        bool af = entity_has_event_flag(&attrs[e->body_a], ENTITY_EVENT_FLAG_HIT);
        bool bf = entity_has_event_flag(&attrs[e->body_b], ENTITY_EVENT_FLAG_HIT);
        if (af || bf) passed++;
    }
    ASSERT_EQ(passed, 1u);

    phys_contact_begin_destroy(&cb);
    phys_manifold_cache_destroy(&cache);
}

/**
 * Sustained contact does not re-fire even with flags set.
 */
static void test_hit_sustained_no_refire(void) {
    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, 64);

    phys_contact_begin_ctx_t cb;
    phys_contact_begin_init(&cb, 64, 32);

    entity_attrs_t attrs[2];
    memset(attrs, 0, sizeof(attrs));
    entity_attrs_init(&attrs[0]);
    entity_attrs_init(&attrs[1]);
    set_event_flags(&attrs[0], ENTITY_EVENT_FLAG_HIT);
    set_event_flags(&attrs[1], ENTITY_EVENT_FLAG_HIT);

    /* Tick 1: contact begins. */
    add_manifold(&cache, 0, 1, 1);
    phys_contact_begin_update(&cb, &cache, 1);
    ASSERT_EQ(phys_contact_begin_count(&cb), 1u);

    /* Tick 2: sustained — still in cache with updated tick. */
    phys_manifold_cache_touch(&cache, 0, 1, 2);
    phys_contact_begin_update(&cb, &cache, 2);
    ASSERT_EQ(phys_contact_begin_count(&cb), 0u);

    phys_contact_begin_destroy(&cb);
    phys_manifold_cache_destroy(&cache);
}

/* ── overlap! integration tests ───────────────────────────────── */

/**
 * Full overlap pipeline with flagged entity.
 */
static void test_overlap_full_pipeline_flagged(void) {
    phys_overlap_begin_ctx_t ob;
    phys_overlap_begin_init(&ob, 64, 32);

    entity_attrs_t attrs[2];
    memset(attrs, 0, sizeof(attrs));
    entity_attrs_init(&attrs[0]);
    entity_attrs_init(&attrs[1]);
    set_event_flags(&attrs[0], ENTITY_EVENT_FLAG_OVERLAP);

    phys_overlap_pair_t pairs[] = {{0, 1}};
    phys_overlap_begin_update(&ob, mock_overlap, NULL, pairs, 1, 1);
    ASSERT_EQ(phys_overlap_begin_count(&ob), 1u);

    /* Filter by entity flag. */
    const phys_overlap_begin_event_t *ev = phys_overlap_begin_events(&ob);
    bool af = entity_has_event_flag(&attrs[ev[0].body_a], ENTITY_EVENT_FLAG_OVERLAP);
    bool bf = entity_has_event_flag(&attrs[ev[0].body_b], ENTITY_EVENT_FLAG_OVERLAP);
    ASSERT_TRUE(af || bf);

    phys_overlap_begin_destroy(&ob);
}

/**
 * Overlap with unflagged entities → filtered out.
 */
static void test_overlap_full_pipeline_unflagged(void) {
    phys_overlap_begin_ctx_t ob;
    phys_overlap_begin_init(&ob, 64, 32);

    entity_attrs_t attrs[2];
    memset(attrs, 0, sizeof(attrs));
    entity_attrs_init(&attrs[0]);
    entity_attrs_init(&attrs[1]);

    phys_overlap_pair_t pairs[] = {{0, 1}};
    phys_overlap_begin_update(&ob, mock_overlap, NULL, pairs, 1, 1);
    ASSERT_EQ(phys_overlap_begin_count(&ob), 1u);

    /* Neither entity flagged → event not published. */
    const phys_overlap_begin_event_t *ev = phys_overlap_begin_events(&ob);
    bool af = entity_has_event_flag(&attrs[ev[0].body_a], ENTITY_EVENT_FLAG_OVERLAP);
    bool bf = entity_has_event_flag(&attrs[ev[0].body_b], ENTITY_EVENT_FLAG_OVERLAP);
    ASSERT_FALSE(af || bf);

    phys_overlap_begin_destroy(&ob);
}

/**
 * Hit flag does NOT enable overlap events and vice versa.
 */
static void test_flag_isolation(void) {
    entity_attrs_t attrs;
    memset(&attrs, 0, sizeof(attrs));
    entity_attrs_init(&attrs);

    /* Set only hit flag. */
    set_event_flags(&attrs, ENTITY_EVENT_FLAG_HIT);
    ASSERT_TRUE(entity_has_event_flag(&attrs, ENTITY_EVENT_FLAG_HIT));
    ASSERT_FALSE(entity_has_event_flag(&attrs, ENTITY_EVENT_FLAG_OVERLAP));

    /* Set only overlap flag. */
    set_event_flags(&attrs, ENTITY_EVENT_FLAG_OVERLAP);
    ASSERT_FALSE(entity_has_event_flag(&attrs, ENTITY_EVENT_FLAG_HIT));
    ASSERT_TRUE(entity_has_event_flag(&attrs, ENTITY_EVENT_FLAG_OVERLAP));

    /* Set both. */
    set_event_flags(&attrs, ENTITY_EVENT_FLAG_HIT | ENTITY_EVENT_FLAG_OVERLAP);
    ASSERT_TRUE(entity_has_event_flag(&attrs, ENTITY_EVENT_FLAG_HIT));
    ASSERT_TRUE(entity_has_event_flag(&attrs, ENTITY_EVENT_FLAG_OVERLAP));
}

/**
 * End-to-end: contact_begin + overlap_begin running simultaneously,
 * same bodies, different flag requirements.
 */
static void test_combined_hit_and_overlap(void) {
    /* Setup manifold cache for contact_begin. */
    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, 64);

    phys_contact_begin_ctx_t cb;
    phys_contact_begin_init(&cb, 64, 32);

    phys_overlap_begin_ctx_t ob;
    phys_overlap_begin_init(&ob, 64, 32);

    /* Body 0 has hit flag, body 1 has overlap flag. */
    entity_attrs_t attrs[2];
    memset(attrs, 0, sizeof(attrs));
    entity_attrs_init(&attrs[0]);
    entity_attrs_init(&attrs[1]);
    set_event_flags(&attrs[0], ENTITY_EVENT_FLAG_HIT);
    set_event_flags(&attrs[1], ENTITY_EVENT_FLAG_OVERLAP);

    /* Both collide AND overlap. */
    add_manifold(&cache, 0, 1, 1);
    phys_contact_begin_update(&cb, &cache, 1);

    phys_overlap_pair_t pairs[] = {{0, 1}};
    phys_overlap_begin_update(&ob, mock_overlap, NULL, pairs, 1, 1);

    /* Contact_begin detected (body 0 has hit flag). */
    ASSERT_EQ(phys_contact_begin_count(&cb), 1u);
    {
        const phys_contact_begin_event_t *ev = phys_contact_begin_events(&cb);
        bool pass = entity_has_event_flag(&attrs[ev[0].body_a], ENTITY_EVENT_FLAG_HIT)
                 || entity_has_event_flag(&attrs[ev[0].body_b], ENTITY_EVENT_FLAG_HIT);
        ASSERT_TRUE(pass);
    }

    /* Overlap_begin detected (body 1 has overlap flag). */
    ASSERT_EQ(phys_overlap_begin_count(&ob), 1u);
    {
        const phys_overlap_begin_event_t *ev = phys_overlap_begin_events(&ob);
        bool pass = entity_has_event_flag(&attrs[ev[0].body_a], ENTITY_EVENT_FLAG_OVERLAP)
                 || entity_has_event_flag(&attrs[ev[0].body_b], ENTITY_EVENT_FLAG_OVERLAP);
        ASSERT_TRUE(pass);
    }

    phys_contact_begin_destroy(&cb);
    phys_overlap_begin_destroy(&ob);
    phys_manifold_cache_destroy(&cache);
}

/* ── Main ─────────────────────────────────────────────────────── */

int main(void) {
    printf("=== collision_event_integration_tests ===\n");

    /* hit! pipeline */
    RUN(test_hit_full_pipeline_flagged);
    RUN(test_hit_full_pipeline_unflagged);
    RUN(test_hit_mixed_flags);
    RUN(test_hit_sustained_no_refire);

    /* overlap! pipeline */
    RUN(test_overlap_full_pipeline_flagged);
    RUN(test_overlap_full_pipeline_unflagged);

    /* Cross-cutting */
    RUN(test_flag_isolation);
    RUN(test_combined_hit_and_overlap);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
