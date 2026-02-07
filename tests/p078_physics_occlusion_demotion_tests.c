/**
 * @file p078_physics_occlusion_demotion_tests.c
 * @brief Unit tests for occlusion-based tier demotion (phys-406).
 *
 * Tests cover: occluded T1 demotion to T3, visible T1 stays T1,
 * NULL visibility backward compat, T2 unaffected, re-promotion flag,
 * nudge toward target, and nudge max correction cap.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/game_state.h"
#include "ferrum/physics/occlusion_nudge.h"
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

#define ASSERT_FLOAT_NEAR(exp, act, tol)                                       \
    do {                                                                        \
        float _e = (float)(exp);                                               \
        float _a = (float)(act);                                               \
        if (fabsf(_e - _a) > (float)(tol)) {                                  \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: "               \
                    "expected %.6f got %.6f (tol %.6f)\n",                     \
                    __FILE__, __LINE__, (double)_e, (double)_a,                \
                    (double)(tol));                                             \
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

#define TEST_ARENA_SIZE (1024u * 1024u)

/** Create a dynamic body at a given position with a preset tier. */
static void make_dynamic_at(phys_body_t *body, float mass,
                            phys_vec3_t pos, uint8_t tier) {
    phys_body_init(body);
    phys_body_set_mass(body, mass);
    body->position = pos;
    body->tier = tier;
}

/** Set up a game state with a single player at the origin. */
static void setup_game_with_player_at_origin(phys_game_state_t *game) {
    phys_game_state_init(game);
    phys_player_state_t player;
    memset(&player, 0, sizeof(player));
    player.position = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    phys_game_state_set_player(game, 0, &player);
}

/** Build a visibility bitfield with the given bit set or cleared. */
static void set_visibility_bit(uint8_t *vis, uint32_t index, bool visible) {
    uint32_t byte_idx = index / 8;
    uint8_t  bit_mask = (uint8_t)(1u << (index % 8));
    if (visible) {
        vis[byte_idx] |= bit_mask;
    } else {
        vis[byte_idx] &= (uint8_t)~bit_mask;
    }
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * Test 1: Body at T1 distance, visibility bit 0 → demoted to T3.
 */
static int test_occluded_t1_demotes_to_t3(void) {
    phys_body_t bodies[1];
    /* 15m from origin → T1 by distance. */
    make_dynamic_at(&bodies[0], 1.0f,
                    (phys_vec3_t){15.0f, 0.0f, 0.0f},
                    PHYS_TIER_1_NEAR);

    uint8_t active[1] = {1};
    uint8_t tier_out[1] = {0};
    uint8_t visibility[1];
    memset(visibility, 0, sizeof(visibility));
    /* Bit 0 = 0 → body 0 is NOT visible. */

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
        .tier_out = tier_out,
        .visibility_set = visibility,
        .repromotion_flags = NULL,
    };

    phys_stage_tier_classify(&args);

    /* Should be demoted to T3. */
    ASSERT_INT_EQ(0, (int)lists.tiers[PHYS_TIER_1_NEAR].count);
    ASSERT_INT_EQ(1, (int)lists.tiers[PHYS_TIER_3_WORLD].count);
    ASSERT_INT_EQ((int)PHYS_TIER_3_WORLD, (int)tier_out[0]);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Test 2: Body at T1 distance, visibility bit 1 → stays T1.
 */
static int test_visible_t1_stays_t1(void) {
    phys_body_t bodies[1];
    make_dynamic_at(&bodies[0], 1.0f,
                    (phys_vec3_t){15.0f, 0.0f, 0.0f},
                    PHYS_TIER_1_NEAR);

    uint8_t active[1] = {1};
    uint8_t tier_out[1] = {0};
    uint8_t visibility[1];
    memset(visibility, 0, sizeof(visibility));
    set_visibility_bit(visibility, 0, true);

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
        .tier_out = tier_out,
        .visibility_set = visibility,
        .repromotion_flags = NULL,
    };

    phys_stage_tier_classify(&args);

    ASSERT_INT_EQ(1, (int)lists.tiers[PHYS_TIER_1_NEAR].count);
    ASSERT_INT_EQ(0, (int)lists.tiers[PHYS_TIER_3_WORLD].count);
    ASSERT_INT_EQ((int)PHYS_TIER_1_NEAR, (int)tier_out[0]);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Test 3: NULL visibility_set → no demotion (backward compat).
 */
static int test_null_visibility_all_visible(void) {
    phys_body_t bodies[1];
    make_dynamic_at(&bodies[0], 1.0f,
                    (phys_vec3_t){15.0f, 0.0f, 0.0f},
                    PHYS_TIER_1_NEAR);

    uint8_t active[1] = {1};
    uint8_t tier_out[1] = {0};

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
        .tier_out = tier_out,
        .visibility_set = NULL,
        .repromotion_flags = NULL,
    };

    phys_stage_tier_classify(&args);

    /* No visibility_set → treated as all visible → stays T1. */
    ASSERT_INT_EQ(1, (int)lists.tiers[PHYS_TIER_1_NEAR].count);
    ASSERT_INT_EQ((int)PHYS_TIER_1_NEAR, (int)tier_out[0]);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Test 4: T2 body not affected by occlusion (only T0-T1 demoted).
 */
static int test_t2_not_affected(void) {
    phys_body_t bodies[1];
    /* 40m → T2 by distance. */
    make_dynamic_at(&bodies[0], 1.0f,
                    (phys_vec3_t){40.0f, 0.0f, 0.0f},
                    PHYS_TIER_2_VISIBLE);

    uint8_t active[1] = {1};
    uint8_t tier_out[1] = {0};
    uint8_t visibility[1];
    memset(visibility, 0, sizeof(visibility));
    /* Body 0 not visible, but it's T2 so shouldn't be affected. */

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
        .tier_out = tier_out,
        .visibility_set = visibility,
        .repromotion_flags = NULL,
    };

    phys_stage_tier_classify(&args);

    /* T2 stays T2 regardless of visibility. */
    ASSERT_INT_EQ(1, (int)lists.tiers[PHYS_TIER_2_VISIBLE].count);
    ASSERT_INT_EQ(0, (int)lists.tiers[PHYS_TIER_3_WORLD].count);
    ASSERT_INT_EQ((int)PHYS_TIER_2_VISIBLE, (int)tier_out[0]);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Test 5: Body was T3 (occluded), now visible at T1 distance →
 * repromotion_flags[i] = 1.
 */
static int test_repromotion_flag_set(void) {
    phys_body_t bodies[1];
    /* 15m → T1 by distance, but old tier is T3 (was occluded). */
    make_dynamic_at(&bodies[0], 1.0f,
                    (phys_vec3_t){15.0f, 0.0f, 0.0f},
                    PHYS_TIER_3_WORLD);

    uint8_t active[1] = {1};
    uint8_t tier_out[1] = {0};
    uint8_t repromotion[1] = {0};
    uint8_t visibility[1];
    memset(visibility, 0, sizeof(visibility));
    set_visibility_bit(visibility, 0, true);  /* Now visible. */

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
        .tier_out = tier_out,
        .visibility_set = visibility,
        .repromotion_flags = repromotion,
    };

    phys_stage_tier_classify(&args);

    /* Distance says T1, was T3, now visible → re-promoted with flag. */
    ASSERT_INT_EQ(1, (int)lists.tiers[PHYS_TIER_1_NEAR].count);
    ASSERT_INT_EQ((int)PHYS_TIER_1_NEAR, (int)tier_out[0]);
    ASSERT_INT_EQ(1, (int)repromotion[0]);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Test 6: Nudge moves body position toward target.
 */
static int test_nudge_apply_moves_toward_target(void) {
    phys_body_t bodies[1];
    phys_body_init(&bodies[0]);
    phys_body_set_mass(&bodies[0], 1.0f);
    bodies[0].position = (phys_vec3_t){0.0f, 0.0f, 0.0f};

    uint8_t repromotion[1] = {1};
    phys_vec3_t targets[1] = {{0.003f, 0.0f, 0.0f}};  /* 3mm away. */

    phys_occlusion_nudge_apply(bodies, repromotion, targets, 1, 3);

    /* After one call with nudge_frames=3, should move 1/3 toward target. */
    float expected_x = 0.003f / 3.0f;  /* 1mm. */
    ASSERT_FLOAT_NEAR(expected_x, bodies[0].position.x, 0.0001f);

    return 0;
}

/**
 * Test 7: Nudge doesn't exceed 5mm total correction cap.
 */
static int test_nudge_max_correction(void) {
    phys_body_t bodies[1];
    phys_body_init(&bodies[0]);
    phys_body_set_mass(&bodies[0], 1.0f);
    bodies[0].position = (phys_vec3_t){0.0f, 0.0f, 0.0f};

    uint8_t repromotion[1] = {1};
    /* Target is 50mm (0.05m) away — well over the 5mm cap. */
    phys_vec3_t targets[1] = {{0.05f, 0.0f, 0.0f}};

    phys_occlusion_nudge_apply(bodies, repromotion, targets, 1, 3);

    /*
     * Per-frame cap = 5mm / nudge_frames = 5mm / 3 ≈ 1.667mm = 0.001667m.
     * The uncapped step would be delta/3 = 0.05/3 ≈ 0.01667m (16.67mm).
     * So the cap should apply: movement ≤ 0.001667m.
     */
    float max_per_frame = 0.005f / 3.0f;
    ASSERT_TRUE(bodies[0].position.x <= max_per_frame + 0.0001f);
    ASSERT_TRUE(bodies[0].position.x > 0.0f);

    return 0;
}

/* ── Runner ─────────────────────────────────────────────────────── */

int main(void) {
    int test_count = 0;
    int fail_count = 0;

    printf("p078_physics_occlusion_demotion_tests\n");

    RUN_TEST(test_occluded_t1_demotes_to_t3);
    RUN_TEST(test_visible_t1_stays_t1);
    RUN_TEST(test_null_visibility_all_visible);
    RUN_TEST(test_t2_not_affected);
    RUN_TEST(test_repromotion_flag_set);
    RUN_TEST(test_nudge_apply_moves_toward_target);
    RUN_TEST(test_nudge_max_correction);

    printf("\n%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
