/**
 * @file p073_physics_tier_distance_tests.c
 * @brief Unit tests for distance-based tier classification (phys-401).
 *
 * Tests cover: T0–T4 distance thresholds, sleeping override,
 * NULL game state fallback, hysteresis demotion prevention,
 * and tier_out array writing.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/game_state.h"
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
 * @brief Create a dynamic body at a given position.
 */
static void make_dynamic_at(phys_body_t *body, float mass, phys_vec3_t pos) {
    phys_body_init(body);
    phys_body_set_mass(body, mass);
    body->position = pos;
}

/**
 * @brief Create a sleeping dynamic body at a given position.
 */
static void make_sleeping_at(phys_body_t *body, float mass, phys_vec3_t pos) {
    phys_body_init(body);
    phys_body_set_mass(body, mass);
    phys_body_set_sleeping(body, true);
    body->position = pos;
}

/**
 * @brief Set up a game state with a single player at the origin.
 */
static void setup_game_with_player_at_origin(phys_game_state_t *game) {
    phys_game_state_init(game);
    phys_player_state_t player;
    memset(&player, 0, sizeof(player));
    player.position = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    phys_game_state_set_player(game, 0, &player);
}

/**
 * @brief Helper to check that exactly one body is in the expected tier.
 */
static int check_single_body_in_tier(const phys_tier_lists_t *lists,
                                     phys_tier_t expected_tier) {
    for (int t = 0; t < PHYS_TIER_COUNT; ++t) {
        if (t == (int)expected_tier) {
            ASSERT_INT_EQ(1, (int)lists->tiers[t].count);
        } else {
            ASSERT_INT_EQ(0, (int)lists->tiers[t].count);
        }
    }
    return 0;
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * Body at 3m from player → T0 (threshold < 5m).
 */
static int test_tier_t0_near_player(void) {
    phys_body_t bodies[1];
    make_dynamic_at(&bodies[0], 1.0f, (phys_vec3_t){3.0f, 0.0f, 0.0f});

    uint8_t active[1] = {1};
    phys_game_state_t game;
    setup_game_with_player_at_origin(&game);

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, TEST_ARENA_SIZE);

    phys_tier_lists_t lists;
    memset(&lists, 0, sizeof(lists));

    phys_tier_classify_args_t args = {
        .bodies = bodies,
        .active = active,
        .body_count = 1,
        .game = &game,
        .tier_lists_out = &lists,
        .arena = &arena,
        .tier_out = NULL,
    };

    phys_stage_tier_classify(&args);
    int ret = check_single_body_in_tier(&lists, PHYS_TIER_0_DIRECT);

    phys_frame_arena_destroy(&arena);
    return ret;
}

/**
 * Body at 15m from player → T1 (threshold 5m ≤ d < 20m).
 */
static int test_tier_t1_medium(void) {
    phys_body_t bodies[1];
    make_dynamic_at(&bodies[0], 1.0f, (phys_vec3_t){15.0f, 0.0f, 0.0f});

    uint8_t active[1] = {1};
    phys_game_state_t game;
    setup_game_with_player_at_origin(&game);

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, TEST_ARENA_SIZE);

    phys_tier_lists_t lists;
    memset(&lists, 0, sizeof(lists));

    phys_tier_classify_args_t args = {
        .bodies = bodies,
        .active = active,
        .body_count = 1,
        .game = &game,
        .tier_lists_out = &lists,
        .arena = &arena,
        .tier_out = NULL,
    };

    phys_stage_tier_classify(&args);
    int ret = check_single_body_in_tier(&lists, PHYS_TIER_1_NEAR);

    phys_frame_arena_destroy(&arena);
    return ret;
}

/**
 * Body at 40m from player → T2 (threshold 20m ≤ d < 50m).
 */
static int test_tier_t2_visible(void) {
    phys_body_t bodies[1];
    make_dynamic_at(&bodies[0], 1.0f, (phys_vec3_t){40.0f, 0.0f, 0.0f});

    uint8_t active[1] = {1};
    phys_game_state_t game;
    setup_game_with_player_at_origin(&game);

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, TEST_ARENA_SIZE);

    phys_tier_lists_t lists;
    memset(&lists, 0, sizeof(lists));

    phys_tier_classify_args_t args = {
        .bodies = bodies,
        .active = active,
        .body_count = 1,
        .game = &game,
        .tier_lists_out = &lists,
        .arena = &arena,
        .tier_out = NULL,
    };

    phys_stage_tier_classify(&args);
    int ret = check_single_body_in_tier(&lists, PHYS_TIER_2_VISIBLE);

    phys_frame_arena_destroy(&arena);
    return ret;
}

/**
 * Body at 150m from player → T3 (threshold 50m ≤ d < 200m).
 */
static int test_tier_t3_world(void) {
    phys_body_t bodies[1];
    make_dynamic_at(&bodies[0], 1.0f, (phys_vec3_t){150.0f, 0.0f, 0.0f});

    uint8_t active[1] = {1};
    phys_game_state_t game;
    setup_game_with_player_at_origin(&game);

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, TEST_ARENA_SIZE);

    phys_tier_lists_t lists;
    memset(&lists, 0, sizeof(lists));

    phys_tier_classify_args_t args = {
        .bodies = bodies,
        .active = active,
        .body_count = 1,
        .game = &game,
        .tier_lists_out = &lists,
        .arena = &arena,
        .tier_out = NULL,
    };

    phys_stage_tier_classify(&args);
    int ret = check_single_body_in_tier(&lists, PHYS_TIER_3_WORLD);

    phys_frame_arena_destroy(&arena);
    return ret;
}

/**
 * Body at 300m from player → T4 (threshold d ≥ 200m).
 */
static int test_tier_t4_background(void) {
    phys_body_t bodies[1];
    make_dynamic_at(&bodies[0], 1.0f, (phys_vec3_t){300.0f, 0.0f, 0.0f});

    uint8_t active[1] = {1};
    phys_game_state_t game;
    setup_game_with_player_at_origin(&game);

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, TEST_ARENA_SIZE);

    phys_tier_lists_t lists;
    memset(&lists, 0, sizeof(lists));

    phys_tier_classify_args_t args = {
        .bodies = bodies,
        .active = active,
        .body_count = 1,
        .game = &game,
        .tier_lists_out = &lists,
        .arena = &arena,
        .tier_out = NULL,
    };

    phys_stage_tier_classify(&args);
    int ret = check_single_body_in_tier(&lists, PHYS_TIER_4_BACKGROUND);

    phys_frame_arena_destroy(&arena);
    return ret;
}

/**
 * Sleeping body at 3m from player → T5 regardless of distance.
 */
static int test_tier_sleeping_ignores_distance(void) {
    phys_body_t bodies[1];
    make_sleeping_at(&bodies[0], 1.0f, (phys_vec3_t){3.0f, 0.0f, 0.0f});

    uint8_t active[1] = {1};
    phys_game_state_t game;
    setup_game_with_player_at_origin(&game);

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, TEST_ARENA_SIZE);

    phys_tier_lists_t lists;
    memset(&lists, 0, sizeof(lists));

    phys_tier_classify_args_t args = {
        .bodies = bodies,
        .active = active,
        .body_count = 1,
        .game = &game,
        .tier_lists_out = &lists,
        .arena = &arena,
        .tier_out = NULL,
    };

    phys_stage_tier_classify(&args);
    int ret = check_single_body_in_tier(&lists, PHYS_TIER_5_SLEEPING);

    phys_frame_arena_destroy(&arena);
    return ret;
}

/**
 * NULL game state → all dynamic bodies fall back to T0 (Phase 1 compat).
 */
static int test_tier_no_game_state_fallback(void) {
    phys_body_t bodies[2];
    make_dynamic_at(&bodies[0], 1.0f, (phys_vec3_t){150.0f, 0.0f, 0.0f});
    make_dynamic_at(&bodies[1], 1.0f, (phys_vec3_t){300.0f, 0.0f, 0.0f});

    uint8_t active[2] = {1, 1};

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, TEST_ARENA_SIZE);

    phys_tier_lists_t lists;
    memset(&lists, 0, sizeof(lists));

    phys_tier_classify_args_t args = {
        .bodies = bodies,
        .active = active,
        .body_count = 2,
        .game = NULL,
        .tier_lists_out = &lists,
        .arena = &arena,
        .tier_out = NULL,
    };

    phys_stage_tier_classify(&args);

    /* Both bodies should be in T0 when no game state is available. */
    ASSERT_INT_EQ(2, (int)lists.tiers[PHYS_TIER_0_DIRECT].count);
    for (int t = PHYS_TIER_1_NEAR; t < PHYS_TIER_COUNT; ++t) {
        ASSERT_INT_EQ(0, (int)lists.tiers[t].count);
    }

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Hysteresis prevents demotion:
 *   - Body's stored tier is T0, distance is 5.5m (between 5.0 and 6.0 demotion threshold).
 *     With hysteresis, body stays T0 since 5.5 < 6.0 (demotion threshold).
 *   - Body's stored tier is T0, distance is 7.0m (above 6.0 demotion threshold).
 *     Body demotes to T1.
 */
static int test_tier_hysteresis_prevents_demotion(void) {
    phys_frame_arena_t arena;
    phys_game_state_t game;
    phys_tier_lists_t lists;

    /* Case 1: body at T0, distance 5.5m → stays T0 (5.5 < 6.0 demotion threshold). */
    {
        phys_body_t bodies[1];
        make_dynamic_at(&bodies[0], 1.0f, (phys_vec3_t){5.5f, 0.0f, 0.0f});
        bodies[0].tier = PHYS_TIER_0_DIRECT;  /* Current tier is T0. */

        uint8_t active[1] = {1};
        uint8_t tier_out[1] = {0};

        setup_game_with_player_at_origin(&game);
        phys_frame_arena_init(&arena, TEST_ARENA_SIZE);
        memset(&lists, 0, sizeof(lists));

        phys_tier_classify_args_t args = {
            .bodies = bodies,
            .active = active,
            .body_count = 1,
            .game = &game,
            .tier_lists_out = &lists,
            .arena = &arena,
            .tier_out = tier_out,
        };

        phys_stage_tier_classify(&args);

        /* Should stay T0 due to hysteresis. */
        ASSERT_INT_EQ(1, (int)lists.tiers[PHYS_TIER_0_DIRECT].count);
        ASSERT_INT_EQ(0, (int)lists.tiers[PHYS_TIER_1_NEAR].count);
        ASSERT_INT_EQ((int)PHYS_TIER_0_DIRECT, (int)tier_out[0]);

        phys_frame_arena_destroy(&arena);
    }

    /* Case 2: body at T0, distance 7.0m → demotes to T1 (7.0 > 6.0). */
    {
        phys_body_t bodies[1];
        make_dynamic_at(&bodies[0], 1.0f, (phys_vec3_t){7.0f, 0.0f, 0.0f});
        bodies[0].tier = PHYS_TIER_0_DIRECT;  /* Current tier is T0. */

        uint8_t active[1] = {1};
        uint8_t tier_out[1] = {0};

        setup_game_with_player_at_origin(&game);
        phys_frame_arena_init(&arena, TEST_ARENA_SIZE);
        memset(&lists, 0, sizeof(lists));

        phys_tier_classify_args_t args = {
            .bodies = bodies,
            .active = active,
            .body_count = 1,
            .game = &game,
            .tier_lists_out = &lists,
            .arena = &arena,
            .tier_out = tier_out,
        };

        phys_stage_tier_classify(&args);

        /* Should demote to T1 since 7.0 > 6.0 demotion threshold. */
        ASSERT_INT_EQ(0, (int)lists.tiers[PHYS_TIER_0_DIRECT].count);
        ASSERT_INT_EQ(1, (int)lists.tiers[PHYS_TIER_1_NEAR].count);
        ASSERT_INT_EQ((int)PHYS_TIER_1_NEAR, (int)tier_out[0]);

        phys_frame_arena_destroy(&arena);
    }

    return 0;
}

/* ── Runner ─────────────────────────────────────────────────────── */

int main(void) {
    int test_count = 0;
    int fail_count = 0;

    printf("p073_physics_tier_distance_tests\n");

    RUN_TEST(test_tier_t0_near_player);
    RUN_TEST(test_tier_t1_medium);
    RUN_TEST(test_tier_t2_visible);
    RUN_TEST(test_tier_t3_world);
    RUN_TEST(test_tier_t4_background);
    RUN_TEST(test_tier_sleeping_ignores_distance);
    RUN_TEST(test_tier_no_game_state_fallback);
    RUN_TEST(test_tier_hysteresis_prevents_demotion);

    printf("\n%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
