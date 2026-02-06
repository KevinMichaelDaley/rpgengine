/**
 * @file p034_physics_tier_classify_tests.c
 * @brief Unit tests for Stage 1: Tier Classification.
 *
 * Tests cover: all-dynamic, sleeping, static exclusion, mixed,
 * empty body set, and NULL safety.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/tier_classify.h"
#include "ferrum/physics/tier_list.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n",                 \
                    __FILE__, __LINE__, #cond);                                 \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                                \
    do {                                                                        \
        if ((exp) != (act)) {                                                   \
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: "                    \
                    "expected %d got %d\n",                                     \
                    __FILE__, __LINE__, (int)(exp), (int)(act));                 \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define RUN_TEST(fn)                                                           \
    do {                                                                        \
        printf("  %-50s", #fn);                                                 \
        int _r = fn();                                                          \
        printf("%s\n", _r ? "FAIL" : "PASS");                                   \
        if (_r) fail_count++;                                                   \
        test_count++;                                                           \
    } while (0)

/* ── Helpers ────────────────────────────────────────────────────── */

/** Arena size large enough for any test (1 MiB). */
#define TEST_ARENA_SIZE (1024u * 1024u)

/**
 * @brief Initialise a dynamic body with the given mass.
 *
 * Calls phys_body_init (which sets static defaults) then sets mass
 * so that inv_mass > 0 and the STATIC flag is cleared.
 */
static void make_dynamic(phys_body_t *body, float mass) {
    phys_body_init(body);
    phys_body_set_mass(body, mass);
}

/**
 * @brief Initialise a static body (inv_mass == 0, STATIC flag set).
 */
static void make_static(phys_body_t *body) {
    phys_body_init(body);
    /* phys_body_init leaves inv_mass = 0, flags = STATIC. */
}

/**
 * @brief Initialise a sleeping dynamic body.
 */
static void make_sleeping(phys_body_t *body, float mass) {
    phys_body_init(body);
    phys_body_set_mass(body, mass);
    phys_body_set_sleeping(body, true);
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * All 3 bodies are dynamic → all should land in T0.
 */
static int test_classify_all_dynamic_to_t0(void) {
    phys_body_t bodies[3];
    make_dynamic(&bodies[0], 1.0f);
    make_dynamic(&bodies[1], 2.0f);
    make_dynamic(&bodies[2], 0.5f);

    uint8_t active[3] = {1, 1, 1};

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, TEST_ARENA_SIZE);

    phys_tier_lists_t lists;
    memset(&lists, 0, sizeof(lists));

    phys_tier_classify_args_t args = {
        .bodies = bodies,
        .active = active,
        .body_count = 3,
        .game = NULL,
        .tier_lists_out = &lists,
        .arena = &arena,
    };

    phys_stage_tier_classify(&args);

    ASSERT_INT_EQ(3, (int)lists.tiers[PHYS_TIER_0_DIRECT].count);
    ASSERT_INT_EQ(0, (int)lists.tiers[PHYS_TIER_5_SLEEPING].count);

    /* Verify T1–T4 are empty. */
    for (int t = PHYS_TIER_1_NEAR; t <= PHYS_TIER_4_BACKGROUND; ++t) {
        ASSERT_INT_EQ(0, (int)lists.tiers[t].count);
    }

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * 1 sleeping + 2 dynamic → T5 count=1, T0 count=2.
 */
static int test_classify_sleeping_to_t5(void) {
    phys_body_t bodies[3];
    make_dynamic(&bodies[0], 1.0f);
    make_sleeping(&bodies[1], 1.0f);
    make_dynamic(&bodies[2], 1.0f);

    uint8_t active[3] = {1, 1, 1};

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, TEST_ARENA_SIZE);

    phys_tier_lists_t lists;
    memset(&lists, 0, sizeof(lists));

    phys_tier_classify_args_t args = {
        .bodies = bodies,
        .active = active,
        .body_count = 3,
        .game = NULL,
        .tier_lists_out = &lists,
        .arena = &arena,
    };

    phys_stage_tier_classify(&args);

    ASSERT_INT_EQ(2, (int)lists.tiers[PHYS_TIER_0_DIRECT].count);
    ASSERT_INT_EQ(1, (int)lists.tiers[PHYS_TIER_5_SLEEPING].count);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * A static body must not appear in any tier list.
 */
static int test_classify_static_excluded(void) {
    phys_body_t bodies[3];
    make_dynamic(&bodies[0], 1.0f);
    make_static(&bodies[1]);
    make_dynamic(&bodies[2], 1.0f);

    uint8_t active[3] = {1, 1, 1};

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, TEST_ARENA_SIZE);

    phys_tier_lists_t lists;
    memset(&lists, 0, sizeof(lists));

    phys_tier_classify_args_t args = {
        .bodies = bodies,
        .active = active,
        .body_count = 3,
        .game = NULL,
        .tier_lists_out = &lists,
        .arena = &arena,
    };

    phys_stage_tier_classify(&args);

    /* Only the 2 dynamic bodies should be in T0. */
    ASSERT_INT_EQ(2, (int)lists.tiers[PHYS_TIER_0_DIRECT].count);

    /* Static body must not be in any tier (T0–T5). */
    for (int t = 0; t < PHYS_TIER_COUNT; ++t) {
        for (uint32_t j = 0; j < lists.tiers[t].count; ++j) {
            ASSERT_TRUE(lists.tiers[t].indices[j] != 1);
        }
    }

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Mixed: 1 dynamic + 1 sleeping + 1 static → T0=1, T5=1, total in tiers=2.
 */
static int test_classify_mixed(void) {
    phys_body_t bodies[3];
    make_dynamic(&bodies[0], 1.0f);
    make_sleeping(&bodies[1], 1.0f);
    make_static(&bodies[2]);

    uint8_t active[3] = {1, 1, 1};

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, TEST_ARENA_SIZE);

    phys_tier_lists_t lists;
    memset(&lists, 0, sizeof(lists));

    phys_tier_classify_args_t args = {
        .bodies = bodies,
        .active = active,
        .body_count = 3,
        .game = NULL,
        .tier_lists_out = &lists,
        .arena = &arena,
    };

    phys_stage_tier_classify(&args);

    ASSERT_INT_EQ(1, (int)lists.tiers[PHYS_TIER_0_DIRECT].count);
    ASSERT_INT_EQ(1, (int)lists.tiers[PHYS_TIER_5_SLEEPING].count);

    /* T1–T4 should be empty. */
    for (int t = PHYS_TIER_1_NEAR; t <= PHYS_TIER_4_BACKGROUND; ++t) {
        ASSERT_INT_EQ(0, (int)lists.tiers[t].count);
    }

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * body_count=0 → all tier counts should be 0.
 */
static int test_classify_empty(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, TEST_ARENA_SIZE);

    phys_tier_lists_t lists;
    memset(&lists, 0, sizeof(lists));

    phys_tier_classify_args_t args = {
        .bodies = NULL,
        .active = NULL,
        .body_count = 0,
        .game = NULL,
        .tier_lists_out = &lists,
        .arena = &arena,
    };

    phys_stage_tier_classify(&args);

    for (int t = 0; t < PHYS_TIER_COUNT; ++t) {
        ASSERT_INT_EQ(0, (int)lists.tiers[t].count);
    }

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * NULL args must not crash.
 */
static int test_classify_null_safe(void) {
    /* NULL args pointer. */
    phys_stage_tier_classify(NULL);

    /* NULL tier_lists_out. */
    phys_tier_classify_args_t args;
    memset(&args, 0, sizeof(args));
    phys_stage_tier_classify(&args);

    /* NULL arena. */
    phys_tier_lists_t lists;
    memset(&lists, 0, sizeof(lists));
    args.tier_lists_out = &lists;
    args.arena = NULL;
    phys_stage_tier_classify(&args);

    /* If we got here without a crash, the test passes. */
    return 0;
}

/* ── Runner ─────────────────────────────────────────────────────── */

int main(void) {
    int test_count = 0;
    int fail_count = 0;

    printf("p034_physics_tier_classify_tests\n");

    RUN_TEST(test_classify_all_dynamic_to_t0);
    RUN_TEST(test_classify_sleeping_to_t5);
    RUN_TEST(test_classify_static_excluded);
    RUN_TEST(test_classify_mixed);
    RUN_TEST(test_classify_empty);
    RUN_TEST(test_classify_null_safe);

    printf("\n%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
