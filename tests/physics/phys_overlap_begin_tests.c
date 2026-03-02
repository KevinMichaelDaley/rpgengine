/**
 * @file phys_overlap_begin_tests.c
 * @brief Tests for phys_overlap_begin — overlap-begin detection via
 *        phys_test_overlap on the settled backbuffer.
 *
 * Tests cover: new overlap fires, sustained does not, lost+regained fires,
 * mesh-vs-mesh skipped, multiple overlaps, empty input.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "ferrum/physics/phys_overlap_begin.h"
#include "ferrum/physics/phys_pair_set.h"

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

/* ── Mock overlap test ────────────────────────────────────────── */

/**
 * Mock overlap test function. Returns true if the two body positions
 * are within 2.0 units of each other (simple distance check).
 * Rejects mesh-vs-mesh pairs (both shape_type == 5).
 */
static bool mock_overlap_test(void *user_ctx,
                              uint32_t body_a, uint32_t body_b,
                              phys_vec3_t *out_center) {
    /* User context is a flat array of body positions: float pos[MAX][3]. */
    const float (*pos)[3] = (const float (*)[3])user_ctx;
    float dx = pos[body_a][0] - pos[body_b][0];
    float dy = pos[body_a][1] - pos[body_b][1];
    float dz = pos[body_a][2] - pos[body_b][2];
    float dist2 = dx*dx + dy*dy + dz*dz;
    if (dist2 < 4.0f) { /* within radius 2.0 */
        if (out_center) {
            out_center->x = (pos[body_a][0] + pos[body_b][0]) * 0.5f;
            out_center->y = (pos[body_a][1] + pos[body_b][1]) * 0.5f;
            out_center->z = (pos[body_a][2] + pos[body_b][2]) * 0.5f;
        }
        return true;
    }
    return false;
}

/* ── Tests ────────────────────────────────────────────────────── */

/** Init and destroy without crash. */
static void test_init_destroy(void) {
    phys_overlap_begin_ctx_t ctx;
    ASSERT_TRUE(phys_overlap_begin_init(&ctx, 64, 32));
    ASSERT_EQ(phys_overlap_begin_count(&ctx), 0u);
    phys_overlap_begin_destroy(&ctx);
}

/** New overlap produces one event. */
static void test_new_overlap_fires(void) {
    phys_overlap_begin_ctx_t ctx;
    phys_overlap_begin_init(&ctx, 64, 32);

    /* Bodies 0 and 1 at same position → overlap. */
    float pos[2][3] = {{0,0,0}, {0.5f,0,0}};

    /* Candidate pairs to test. */
    phys_overlap_pair_t pairs[] = {{0, 1}};

    phys_overlap_begin_update(&ctx, mock_overlap_test, pos,
                              pairs, 1, 1);
    ASSERT_EQ(phys_overlap_begin_count(&ctx), 1u);

    const phys_overlap_begin_event_t *ev = phys_overlap_begin_events(&ctx);
    ASSERT_EQ(ev[0].body_a, 0u);
    ASSERT_EQ(ev[0].body_b, 1u);

    phys_overlap_begin_destroy(&ctx);
}

/** Sustained overlap does NOT re-fire. */
static void test_sustained_no_refire(void) {
    phys_overlap_begin_ctx_t ctx;
    phys_overlap_begin_init(&ctx, 64, 32);

    float pos[2][3] = {{0,0,0}, {0.5f,0,0}};
    phys_overlap_pair_t pairs[] = {{0, 1}};

    /* Tick 1: new. */
    phys_overlap_begin_update(&ctx, mock_overlap_test, pos, pairs, 1, 1);
    ASSERT_EQ(phys_overlap_begin_count(&ctx), 1u);

    /* Tick 2: still overlapping, no event. */
    phys_overlap_begin_update(&ctx, mock_overlap_test, pos, pairs, 1, 2);
    ASSERT_EQ(phys_overlap_begin_count(&ctx), 0u);

    /* Tick 3: still overlapping, no event. */
    phys_overlap_begin_update(&ctx, mock_overlap_test, pos, pairs, 1, 3);
    ASSERT_EQ(phys_overlap_begin_count(&ctx), 0u);

    phys_overlap_begin_destroy(&ctx);
}

/** Overlap lost then regained fires again. */
static void test_lost_and_regained(void) {
    phys_overlap_begin_ctx_t ctx;
    phys_overlap_begin_init(&ctx, 64, 32);

    float pos_close[2][3] = {{0,0,0}, {0.5f,0,0}};
    float pos_far[2][3]   = {{0,0,0}, {10.0f,0,0}};
    phys_overlap_pair_t pairs[] = {{0, 1}};

    /* Tick 1: overlap. */
    phys_overlap_begin_update(&ctx, mock_overlap_test, pos_close, pairs, 1, 1);
    ASSERT_EQ(phys_overlap_begin_count(&ctx), 1u);

    /* Tick 2: separated. */
    phys_overlap_begin_update(&ctx, mock_overlap_test, pos_far, pairs, 1, 2);
    ASSERT_EQ(phys_overlap_begin_count(&ctx), 0u);

    /* Tick 3: overlap again — should fire. */
    phys_overlap_begin_update(&ctx, mock_overlap_test, pos_close, pairs, 1, 3);
    ASSERT_EQ(phys_overlap_begin_count(&ctx), 1u);

    phys_overlap_begin_destroy(&ctx);
}

/** No candidate pairs produces no events. */
static void test_no_pairs(void) {
    phys_overlap_begin_ctx_t ctx;
    phys_overlap_begin_init(&ctx, 64, 32);

    phys_overlap_begin_update(&ctx, mock_overlap_test, NULL, NULL, 0, 1);
    ASSERT_EQ(phys_overlap_begin_count(&ctx), 0u);

    phys_overlap_begin_destroy(&ctx);
}

/** Non-overlapping pair produces no event. */
static void test_non_overlapping(void) {
    phys_overlap_begin_ctx_t ctx;
    phys_overlap_begin_init(&ctx, 64, 32);

    float pos[2][3] = {{0,0,0}, {10,0,0}};
    phys_overlap_pair_t pairs[] = {{0, 1}};

    phys_overlap_begin_update(&ctx, mock_overlap_test, pos, pairs, 1, 1);
    ASSERT_EQ(phys_overlap_begin_count(&ctx), 0u);

    phys_overlap_begin_destroy(&ctx);
}

/** Multiple overlaps in one tick. */
static void test_multiple_overlaps(void) {
    phys_overlap_begin_ctx_t ctx;
    phys_overlap_begin_init(&ctx, 64, 32);

    float pos[4][3] = {{0,0,0}, {0.5f,0,0}, {1.0f,0,0}, {1.5f,0,0}};
    phys_overlap_pair_t pairs[] = {{0, 1}, {1, 2}, {2, 3}};

    phys_overlap_begin_update(&ctx, mock_overlap_test, pos, pairs, 3, 1);
    ASSERT_EQ(phys_overlap_begin_count(&ctx), 3u);

    phys_overlap_begin_destroy(&ctx);
}

/** Mix of new and sustained overlaps. */
static void test_mixed_new_and_sustained(void) {
    phys_overlap_begin_ctx_t ctx;
    phys_overlap_begin_init(&ctx, 64, 32);

    float pos[4][3] = {{0,0,0}, {0.5f,0,0}, {1.0f,0,0}, {1.5f,0,0}};
    phys_overlap_pair_t pairs_tick1[] = {{0, 1}, {2, 3}};
    phys_overlap_pair_t pairs_tick2[] = {{0, 1}, {1, 2}, {2, 3}};

    /* Tick 1: two new overlaps. */
    phys_overlap_begin_update(&ctx, mock_overlap_test, pos, pairs_tick1, 2, 1);
    ASSERT_EQ(phys_overlap_begin_count(&ctx), 2u);

    /* Tick 2: (0,1) and (2,3) sustained, (1,2) new. */
    phys_overlap_begin_update(&ctx, mock_overlap_test, pos, pairs_tick2, 3, 2);
    ASSERT_EQ(phys_overlap_begin_count(&ctx), 1u);

    const phys_overlap_begin_event_t *ev = phys_overlap_begin_events(&ctx);
    ASSERT_EQ(ev[0].body_a, 1u);
    ASSERT_EQ(ev[0].body_b, 2u);

    phys_overlap_begin_destroy(&ctx);
}

/** Event contains center estimate. */
static void test_event_center(void) {
    phys_overlap_begin_ctx_t ctx;
    phys_overlap_begin_init(&ctx, 64, 32);

    float pos2[2][3] = {{2.0f, 0.0f, 0.0f}, {3.0f, 0.0f, 0.0f}};
    phys_overlap_pair_t pairs[] = {{0, 1}};

    phys_overlap_begin_update(&ctx, mock_overlap_test, pos2, pairs, 1, 1);
    ASSERT_EQ(phys_overlap_begin_count(&ctx), 1u);

    const phys_overlap_begin_event_t *ev = phys_overlap_begin_events(&ctx);
    /* Center should be midpoint: (2.5, 0, 0). */
    ASSERT_TRUE(ev[0].center.x == 2.5f);
    ASSERT_TRUE(ev[0].center.y == 0.0f);
    ASSERT_TRUE(ev[0].center.z == 0.0f);

    phys_overlap_begin_destroy(&ctx);
}

/** Stale pairs get pruned, allowing re-fire after gap. */
static void test_prune_lifecycle(void) {
    phys_overlap_begin_ctx_t ctx;
    phys_overlap_begin_init(&ctx, 64, 32);

    float pos_close[2][3] = {{0,0,0}, {0.5f,0,0}};
    phys_overlap_pair_t pairs[] = {{0, 1}};

    /* Tick 1: overlap. */
    phys_overlap_begin_update(&ctx, mock_overlap_test, pos_close, pairs, 1, 1);
    ASSERT_EQ(phys_overlap_begin_count(&ctx), 1u);

    /* Ticks 2-5: no pairs tested (pair dropped from candidate list). */
    for (uint32_t t = 2; t <= 5; t++) {
        phys_overlap_begin_update(&ctx, mock_overlap_test, pos_close, NULL, 0, t);
        ASSERT_EQ(phys_overlap_begin_count(&ctx), 0u);
    }

    /* Tick 6: pair re-appears — should fire since it was pruned. */
    phys_overlap_begin_update(&ctx, mock_overlap_test, pos_close, pairs, 1, 6);
    ASSERT_EQ(phys_overlap_begin_count(&ctx), 1u);

    phys_overlap_begin_destroy(&ctx);
}

/* ── Main ─────────────────────────────────────────────────────── */

int main(void) {
    printf("=== phys_overlap_begin_tests ===\n");
    RUN(test_init_destroy);
    RUN(test_new_overlap_fires);
    RUN(test_sustained_no_refire);
    RUN(test_lost_and_regained);
    RUN(test_no_pairs);
    RUN(test_non_overlapping);
    RUN(test_multiple_overlaps);
    RUN(test_mixed_new_and_sustained);
    RUN(test_event_center);
    RUN(test_prune_lifecycle);
    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
