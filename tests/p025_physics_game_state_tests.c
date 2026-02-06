/**
 * @file p025_physics_game_state_tests.c
 * @brief Unit tests for game state input structure (phys-014).
 */

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "ferrum/math/constants.h"
#include "ferrum/physics/game_state.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond)                                                                                \
    do {                                                                                                 \
        if (!(cond)) {                                                                                   \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                                                          \
    do {                                                                                                 \
        if ((exp) != (act)) {                                                                            \
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d got %d\n", __FILE__, __LINE__,     \
                    (int)(exp), (int)(act));                                                              \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, eps)                                                                 \
    do {                                                                                                 \
        float _e = (float)(exp);                                                                         \
        float _a = (float)(act);                                                                         \
        if (fabsf(_e - _a) > (eps)) {                                                                    \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: expected %f got %f (eps=%f)\n", __FILE__,  \
                    __LINE__, (double)_e, (double)_a, (double)(eps));                                     \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_VEC3_NEAR(exp, act, eps)                                                                  \
    do {                                                                                                 \
        phys_vec3_t _ev = (exp);                                                                         \
        phys_vec3_t _av = (act);                                                                         \
        if (fabsf(_ev.x - _av.x) > (eps) || fabsf(_ev.y - _av.y) > (eps) ||                             \
            fabsf(_ev.z - _av.z) > (eps)) {                                                              \
            fprintf(stderr,                                                                              \
                    "ASSERT_VEC3_NEAR failed: %s:%d: expected (%f,%f,%f) got (%f,%f,%f) (eps=%f)\n",      \
                    __FILE__, __LINE__, (double)_ev.x, (double)_ev.y, (double)_ev.z, (double)_av.x,      \
                    (double)_av.y, (double)_av.z, (double)(eps));                                         \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

/* ── Tests ──────────────────────────────────────────────────────── */

static int test_game_state_init(void) {
    phys_game_state_t state;
    phys_game_state_init(&state);
    ASSERT_INT_EQ(0, (int)state.player_count);
    ASSERT_INT_EQ(0, (int)state.hazard_count);
    ASSERT_FLOAT_NEAR(1.0f, state.time_scale, 1e-6f);
    ASSERT_FLOAT_NEAR(0.0f, state.game_time, 1e-6f);
    ASSERT_FLOAT_NEAR(0.0f, state.camera_fov_rad, 1e-6f);
    return 0;
}

static int test_set_player(void) {
    phys_game_state_t state;
    phys_game_state_init(&state);

    phys_player_state_t player;
    memset(&player, 0, sizeof(player));
    player.position = (phys_vec3_t){10.0f, 0.0f, 10.0f};
    player.velocity = (phys_vec3_t){1.0f, 0.0f, 0.0f};
    player.interaction_radius = 3.0f;
    player.has_manipulation = false;

    phys_game_state_set_player(&state, 0, &player);
    ASSERT_INT_EQ(1, (int)state.player_count);
    ASSERT_VEC3_NEAR(((phys_vec3_t){10.0f, 0.0f, 10.0f}), state.players[0].position, 1e-6f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){1.0f, 0.0f, 0.0f}), state.players[0].velocity, 1e-6f);
    ASSERT_FLOAT_NEAR(3.0f, state.players[0].interaction_radius, 1e-6f);
    return 0;
}

static int test_set_player_multiple(void) {
    phys_game_state_t state;
    phys_game_state_init(&state);

    phys_player_state_t p0;
    memset(&p0, 0, sizeof(p0));
    p0.position = (phys_vec3_t){1.0f, 2.0f, 3.0f};

    phys_player_state_t p3;
    memset(&p3, 0, sizeof(p3));
    p3.position = (phys_vec3_t){4.0f, 5.0f, 6.0f};

    phys_game_state_set_player(&state, 0, &p0);
    phys_game_state_set_player(&state, 3, &p3);
    /* player_count should be max(player_count, index+1) = 4 */
    ASSERT_INT_EQ(4, (int)state.player_count);
    ASSERT_VEC3_NEAR(((phys_vec3_t){1.0f, 2.0f, 3.0f}), state.players[0].position, 1e-6f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){4.0f, 5.0f, 6.0f}), state.players[3].position, 1e-6f);
    return 0;
}

static int test_set_player_out_of_bounds(void) {
    /* Setting a player at index >= PHYS_MAX_PLAYERS should be a no-op. */
    phys_game_state_t state;
    phys_game_state_init(&state);

    phys_player_state_t player;
    memset(&player, 0, sizeof(player));
    player.position = (phys_vec3_t){99.0f, 99.0f, 99.0f};

    phys_game_state_set_player(&state, PHYS_MAX_PLAYERS, &player);
    ASSERT_INT_EQ(0, (int)state.player_count);
    return 0;
}

static int test_set_camera(void) {
    phys_game_state_t state;
    phys_game_state_init(&state);

    phys_vec3_t pos = {5.0f, 10.0f, 15.0f};
    phys_vec3_t fwd = {0.0f, 0.0f, 1.0f};
    float fov = (float)(FERRUM_PI / 3.0);

    phys_game_state_set_camera(&state, pos, fwd, fov);
    ASSERT_VEC3_NEAR(pos, state.camera_position, 1e-6f);
    ASSERT_VEC3_NEAR(fwd, state.camera_forward, 1e-6f);
    ASSERT_FLOAT_NEAR(fov, state.camera_fov_rad, 1e-6f);
    return 0;
}

static int test_add_hazard(void) {
    phys_game_state_t state;
    phys_game_state_init(&state);

    phys_game_state_add_hazard(&state, 42);
    ASSERT_INT_EQ(1, (int)state.hazard_count);
    ASSERT_INT_EQ(42, (int)state.hazard_indices[0]);

    phys_game_state_add_hazard(&state, 99);
    ASSERT_INT_EQ(2, (int)state.hazard_count);
    ASSERT_INT_EQ(99, (int)state.hazard_indices[1]);
    return 0;
}

static int test_clear_hazards(void) {
    phys_game_state_t state;
    phys_game_state_init(&state);

    phys_game_state_add_hazard(&state, 1);
    phys_game_state_add_hazard(&state, 2);
    ASSERT_INT_EQ(2, (int)state.hazard_count);

    phys_game_state_clear_hazards(&state);
    ASSERT_INT_EQ(0, (int)state.hazard_count);
    return 0;
}

static int test_hazard_overflow(void) {
    phys_game_state_t state;
    phys_game_state_init(&state);

    /* Fill to max and then try one more. */
    for (uint32_t i = 0; i < PHYS_MAX_HAZARDS; i++) {
        phys_game_state_add_hazard(&state, i);
    }
    ASSERT_INT_EQ((int)PHYS_MAX_HAZARDS, (int)state.hazard_count);

    /* Overflow: should be silently ignored. */
    phys_game_state_add_hazard(&state, 9999);
    ASSERT_INT_EQ((int)PHYS_MAX_HAZARDS, (int)state.hazard_count);
    return 0;
}

static int test_is_manipulated_true(void) {
    phys_game_state_t state;
    phys_game_state_init(&state);

    phys_player_state_t player;
    memset(&player, 0, sizeof(player));
    player.has_manipulation = true;
    player.manipulation_body = (pool_handle_t){.index = 42, .generation = 1, .flags = 0};
    player.manipulation_type = 1; /* grab */

    phys_game_state_set_player(&state, 0, &player);

    pool_handle_t query = {.index = 42, .generation = 1, .flags = 0};
    ASSERT_TRUE(phys_game_state_is_manipulated(&state, query));
    return 0;
}

static int test_is_manipulated_false(void) {
    phys_game_state_t state;
    phys_game_state_init(&state);

    phys_player_state_t player;
    memset(&player, 0, sizeof(player));
    player.has_manipulation = true;
    player.manipulation_body = (pool_handle_t){.index = 42, .generation = 1, .flags = 0};
    player.manipulation_type = 1;

    phys_game_state_set_player(&state, 0, &player);

    /* Different index: should not match. */
    pool_handle_t query_wrong_index = {.index = 43, .generation = 1, .flags = 0};
    ASSERT_TRUE(!phys_game_state_is_manipulated(&state, query_wrong_index));

    /* Same index, different generation: should not match. */
    pool_handle_t query_wrong_gen = {.index = 42, .generation = 2, .flags = 0};
    ASSERT_TRUE(!phys_game_state_is_manipulated(&state, query_wrong_gen));
    return 0;
}

static int test_is_manipulated_no_players(void) {
    phys_game_state_t state;
    phys_game_state_init(&state);

    pool_handle_t query = {.index = 1, .generation = 1, .flags = 0};
    ASSERT_TRUE(!phys_game_state_is_manipulated(&state, query));
    return 0;
}

static int test_distance_to_nearest_player(void) {
    phys_game_state_t state;
    phys_game_state_init(&state);

    phys_player_state_t p0;
    memset(&p0, 0, sizeof(p0));
    p0.position = (phys_vec3_t){10.0f, 0.0f, 10.0f};

    phys_player_state_t p1;
    memset(&p1, 0, sizeof(p1));
    p1.position = (phys_vec3_t){20.0f, 0.0f, 10.0f};

    phys_game_state_set_player(&state, 0, &p0);
    phys_game_state_set_player(&state, 1, &p1);

    /* Point at (12, 0, 10): distance to p0 = 2, distance to p1 = 8 => nearest = 2 */
    float dist = phys_game_state_distance_to_nearest_player(&state, (phys_vec3_t){12.0f, 0.0f, 10.0f});
    ASSERT_FLOAT_NEAR(2.0f, dist, 0.01f);
    return 0;
}

static int test_distance_no_players(void) {
    phys_game_state_t state;
    phys_game_state_init(&state);

    float dist = phys_game_state_distance_to_nearest_player(&state, (phys_vec3_t){0.0f, 0.0f, 0.0f});
    ASSERT_TRUE(dist >= FLT_MAX * 0.5f);
    return 0;
}

static int test_is_in_view_ahead(void) {
    phys_game_state_t state;
    phys_game_state_init(&state);

    /* Camera at origin, looking down +Z, 60° FOV. */
    phys_game_state_set_camera(&state,
                               (phys_vec3_t){0.0f, 0.0f, 0.0f},
                               (phys_vec3_t){0.0f, 0.0f, 1.0f},
                               (float)(FERRUM_PI / 3.0));

    /* Object directly ahead. */
    ASSERT_TRUE(phys_game_state_is_in_view(&state, (phys_vec3_t){0.0f, 0.0f, 10.0f}, 1.0f));
    return 0;
}

static int test_is_in_view_behind(void) {
    phys_game_state_t state;
    phys_game_state_init(&state);

    /* Camera at origin, looking down +Z, 60° FOV. */
    phys_game_state_set_camera(&state,
                               (phys_vec3_t){0.0f, 0.0f, 0.0f},
                               (phys_vec3_t){0.0f, 0.0f, 1.0f},
                               (float)(FERRUM_PI / 3.0));

    /* Object behind camera: should not be in view. */
    ASSERT_TRUE(!phys_game_state_is_in_view(&state, (phys_vec3_t){0.0f, 0.0f, -10.0f}, 1.0f));
    return 0;
}

static int test_is_in_view_edge(void) {
    phys_game_state_t state;
    phys_game_state_init(&state);

    /* Camera at origin, looking down +Z, 90° FOV (half = 45°). */
    phys_game_state_set_camera(&state,
                               (phys_vec3_t){0.0f, 0.0f, 0.0f},
                               (phys_vec3_t){0.0f, 0.0f, 1.0f},
                               (float)(FERRUM_PI / 2.0));

    /* Object at 50° off-axis (> 45° half-FOV), but with large radius
     * its angular extent should bring it into view.
     * At (10, 0, 8.39): angle ~ 50°, distance ~ 13.05, radius = 5 =>
     * angular_radius ~ atan(5/13.05) ~ 21° => 50° < 45° + 21° = 66° => in view */
    ASSERT_TRUE(phys_game_state_is_in_view(&state, (phys_vec3_t){10.0f, 0.0f, 8.39f}, 5.0f));

    /* Same position but tiny radius: should NOT be in view. */
    ASSERT_TRUE(!phys_game_state_is_in_view(&state, (phys_vec3_t){10.0f, 0.0f, 8.39f}, 0.01f));
    return 0;
}

static int test_is_in_view_at_camera(void) {
    phys_game_state_t state;
    phys_game_state_init(&state);

    phys_game_state_set_camera(&state,
                               (phys_vec3_t){5.0f, 5.0f, 5.0f},
                               (phys_vec3_t){0.0f, 0.0f, 1.0f},
                               (float)(FERRUM_PI / 3.0));

    /* Object at the camera position itself: always in view. */
    ASSERT_TRUE(phys_game_state_is_in_view(&state, (phys_vec3_t){5.0f, 5.0f, 5.0f}, 0.0f));
    return 0;
}

static int test_null_safe(void) {
    /* All functions with NULL should not crash. */
    phys_game_state_init(NULL);

    phys_player_state_t player;
    memset(&player, 0, sizeof(player));
    phys_game_state_set_player(NULL, 0, &player);

    phys_game_state_t state;
    phys_game_state_init(&state);
    phys_game_state_set_player(&state, 0, NULL);
    ASSERT_INT_EQ(0, (int)state.player_count);

    phys_game_state_set_camera(NULL, (phys_vec3_t){0, 0, 0}, (phys_vec3_t){0, 0, 1}, 1.0f);

    phys_game_state_add_hazard(NULL, 42);
    phys_game_state_clear_hazards(NULL);

    pool_handle_t handle = {.index = 1, .generation = 1, .flags = 0};
    ASSERT_TRUE(!phys_game_state_is_manipulated(NULL, handle));

    float dist = phys_game_state_distance_to_nearest_player(NULL, (phys_vec3_t){0, 0, 0});
    ASSERT_TRUE(dist >= FLT_MAX * 0.5f);

    ASSERT_TRUE(!phys_game_state_is_in_view(NULL, (phys_vec3_t){0, 0, 10}, 1.0f));
    return 0;
}

/* ── Test runner ────────────────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct test_case TESTS[] = {
    {"game_state_init",                test_game_state_init},
    {"set_player",                     test_set_player},
    {"set_player_multiple",            test_set_player_multiple},
    {"set_player_out_of_bounds",       test_set_player_out_of_bounds},
    {"set_camera",                     test_set_camera},
    {"add_hazard",                     test_add_hazard},
    {"clear_hazards",                  test_clear_hazards},
    {"hazard_overflow",                test_hazard_overflow},
    {"is_manipulated_true",            test_is_manipulated_true},
    {"is_manipulated_false",           test_is_manipulated_false},
    {"is_manipulated_no_players",      test_is_manipulated_no_players},
    {"distance_to_nearest_player",     test_distance_to_nearest_player},
    {"distance_no_players",            test_distance_no_players},
    {"is_in_view_ahead",              test_is_in_view_ahead},
    {"is_in_view_behind",             test_is_in_view_behind},
    {"is_in_view_edge",               test_is_in_view_edge},
    {"is_in_view_at_camera",          test_is_in_view_at_camera},
    {"null_safe",                      test_null_safe},
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0;
    for (size_t i = 0; i < total; ++i) {
        struct test_case *tc = &TESTS[i];
        printf("RUN %s\n", tc->name);
        int rc = tc->fn();
        if (rc == 0) {
            printf("OK %s\n", tc->name);
            passed++;
        } else {
            fprintf(stderr, "FAIL %s (rc=%d)\n", tc->name, rc);
            break;
        }
    }
    if (passed == total) {
        printf("All %zu tests passed\n", passed);
        return 0;
    }
    return 1;
}
