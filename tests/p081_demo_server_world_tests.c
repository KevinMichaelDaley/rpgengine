/**
 * @file p081_demo_server_world_tests.c
 * @brief Unit tests for the demo server physics world + game logic.
 *
 * Covers init/destroy, player management, input handling, box spawning,
 * and the random distant object tick spawner.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/demo/server_world.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/world.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n",                 \
                    __FILE__, __LINE__, #cond);                                 \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define ASSERT_EQ_INT(exp, act)                                                \
    do {                                                                        \
        int _e = (exp), _a = (act);                                             \
        if (_e != _a) {                                                         \
            fprintf(stderr, "ASSERT_EQ_INT failed: %s:%d: "                    \
                    "expected %d got %d\n",                                      \
                    __FILE__, __LINE__, _e, _a);                                \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define ASSERT_EQ_U32(exp, act)                                                \
    do {                                                                        \
        uint32_t _e = (exp), _a = (act);                                        \
        if (_e != _a) {                                                         \
            fprintf(stderr, "ASSERT_EQ_U32 failed: %s:%d: "                    \
                    "expected %u got %u\n",                                      \
                    __FILE__, __LINE__, _e, _a);                                \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, tol)                                       \
    do {                                                                        \
        float _e = (exp), _a = (act), _t = (tol);                              \
        if (fabsf(_e - _a) > _t) {                                             \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: "               \
                    "expected %.6f got %.6f (tol %.6f)\n",                      \
                    __FILE__, __LINE__, (double)_e, (double)_a, (double)_t);    \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define RUN_TEST(fn)                                                           \
    do {                                                                        \
        printf("  %-55s", #fn);                                                 \
        int _r = fn();                                                          \
        printf("%s\n", _r ? "FAIL" : "PASS");                                   \
        if (_r) fail_count++;                                                   \
        test_count++;                                                           \
    } while (0)

/* ── Init / Destroy tests ──────────────────────────────────────── */

static int test_init_success(void) {
    demo_server_world_t sw;
    ASSERT_EQ_INT(0, demo_server_world_init(&sw, 42));

    /* Ground body should exist at index 0. */
    phys_body_t *gb = phys_world_get_body(&sw.physics, DEMO_GROUND_BODY);
    ASSERT_TRUE(gb != NULL);
    ASSERT_TRUE(phys_body_is_static(gb));
    ASSERT_FLOAT_NEAR(-0.5f, gb->position.y, 0.01f);

    /* No players connected initially. */
    for (int i = 0; i < DEMO_MAX_CLIENTS; i++) {
        ASSERT_TRUE(sw.player_connected[i] == 0);
        ASSERT_EQ_U32(UINT32_MAX, sw.player_body[i]);
    }

    ASSERT_EQ_U32(42u, sw.rng_state);
    demo_server_world_destroy(&sw);
    return 0;
}

static int test_init_default_rng_seed(void) {
    demo_server_world_t sw;
    ASSERT_EQ_INT(0, demo_server_world_init(&sw, 0));
    ASSERT_EQ_U32(12345u, sw.rng_state);
    demo_server_world_destroy(&sw);
    return 0;
}

static int test_destroy_idempotent(void) {
    demo_server_world_t sw;
    ASSERT_EQ_INT(0, demo_server_world_init(&sw, 1));
    demo_server_world_destroy(&sw);
    /* Second destroy should be safe (world zeroed). */
    demo_server_world_destroy(&sw);
    return 0;
}

/* ── Player management tests ───────────────────────────────────── */

static int test_add_player(void) {
    demo_server_world_t sw;
    ASSERT_EQ_INT(0, demo_server_world_init(&sw, 100));

    int slot = demo_server_world_add_player(&sw);
    ASSERT_TRUE(slot >= 0 && slot < DEMO_MAX_CLIENTS);
    ASSERT_TRUE(sw.player_connected[slot] == 1);
    ASSERT_TRUE(sw.player_body[slot] != UINT32_MAX);

    /* Player body should be kinematic. */
    phys_body_t *pb = phys_world_get_body(&sw.physics, sw.player_body[slot]);
    ASSERT_TRUE(pb != NULL);
    ASSERT_TRUE(phys_body_is_kinematic(pb));

    /* Player should be at expected spawn height. */
    ASSERT_FLOAT_NEAR(1.0f, pb->position.y, 0.01f);

    demo_server_world_destroy(&sw);
    return 0;
}

static int test_add_max_players(void) {
    demo_server_world_t sw;
    ASSERT_EQ_INT(0, demo_server_world_init(&sw, 100));

    for (int i = 0; i < DEMO_MAX_CLIENTS; i++) {
        int slot = demo_server_world_add_player(&sw);
        ASSERT_TRUE(slot >= 0);
    }

    /* Fifth player should fail. */
    int slot = demo_server_world_add_player(&sw);
    ASSERT_EQ_INT(-1, slot);

    demo_server_world_destroy(&sw);
    return 0;
}

static int test_remove_player(void) {
    demo_server_world_t sw;
    ASSERT_EQ_INT(0, demo_server_world_init(&sw, 100));

    int slot = demo_server_world_add_player(&sw);
    ASSERT_TRUE(slot >= 0);
    uint32_t body_idx = sw.player_body[slot];

    demo_server_world_remove_player(&sw, slot);
    ASSERT_TRUE(sw.player_connected[slot] == 0);
    ASSERT_EQ_U32(UINT32_MAX, sw.player_body[slot]);

    /* Body should be destroyed (get_body returns NULL for removed). */
    phys_body_t *pb = phys_world_get_body(&sw.physics, body_idx);
    ASSERT_TRUE(pb == NULL);

    demo_server_world_destroy(&sw);
    return 0;
}

static int test_remove_player_then_add(void) {
    demo_server_world_t sw;
    ASSERT_EQ_INT(0, demo_server_world_init(&sw, 100));

    int slot1 = demo_server_world_add_player(&sw);
    demo_server_world_remove_player(&sw, slot1);

    /* Should be able to re-use the slot. */
    int slot2 = demo_server_world_add_player(&sw);
    ASSERT_TRUE(slot2 >= 0);
    ASSERT_TRUE(sw.player_connected[slot2] == 1);

    demo_server_world_destroy(&sw);
    return 0;
}

/* ── Input movement tests ──────────────────────────────────────── */

static int test_apply_input_forward(void) {
    demo_server_world_t sw;
    ASSERT_EQ_INT(0, demo_server_world_init(&sw, 100));
    int slot = demo_server_world_add_player(&sw);
    ASSERT_TRUE(slot >= 0);

    phys_body_t *pb = phys_world_get_body(&sw.physics, sw.player_body[slot]);
    float start_z = pb->position.z;

    /* Apply forward movement (W key) with yaw=0, for 1 second. */
    demo_input_move_t input;
    memset(&input, 0, sizeof(input));
    input.move_flags = DEMO_MOVE_W;
    input.yaw_snorm16 = 0;
    input.pitch_snorm16 = 0;

    demo_server_world_apply_input(&sw, slot, &input, 1.0f);

    pb = phys_world_get_body(&sw.physics, sw.player_body[slot]);
    /* Player should have moved in some direction (forward from yaw=0). */
    float dist = fabsf(pb->position.z - start_z) + fabsf(pb->position.x);
    ASSERT_TRUE(dist > 0.1f);

    demo_server_world_destroy(&sw);
    return 0;
}

static int test_apply_input_updates_yaw_pitch(void) {
    demo_server_world_t sw;
    ASSERT_EQ_INT(0, demo_server_world_init(&sw, 100));
    int slot = demo_server_world_add_player(&sw);

    demo_input_move_t input;
    memset(&input, 0, sizeof(input));
    /* Set yaw to ~90 degrees (half of range). */
    input.yaw_snorm16 = 16383;
    input.pitch_snorm16 = 8191;

    demo_server_world_apply_input(&sw, slot, &input, 0.016f);

    /* Yaw/pitch should be updated from snorm16. */
    ASSERT_TRUE(fabsf(sw.player_yaw[slot]) > 0.01f);
    ASSERT_TRUE(fabsf(sw.player_pitch[slot]) > 0.01f);

    demo_server_world_destroy(&sw);
    return 0;
}

/* ── Box spawning tests ────────────────────────────────────────── */

static int test_spawn_box(void) {
    demo_server_world_t sw;
    ASSERT_EQ_INT(0, demo_server_world_init(&sw, 100));
    int slot = demo_server_world_add_player(&sw);

    demo_input_spawn_t spawn;
    memset(&spawn, 0, sizeof(spawn));
    spawn.half_x_mm = 250;   /* 0.25m */
    spawn.half_y_mm = 250;
    spawn.half_z_mm = 250;
    spawn.color_seed = 0xDEADBEEF;

    uint32_t body_idx = demo_server_world_spawn_box(&sw, slot, &spawn);
    ASSERT_TRUE(body_idx != UINT32_MAX);

    /* The body should exist and be dynamic. */
    phys_body_t *b = phys_world_get_body(&sw.physics, body_idx);
    ASSERT_TRUE(b != NULL);
    ASSERT_TRUE(!phys_body_is_static(b));
    ASSERT_TRUE(!phys_body_is_kinematic(b));

    /* Metadata should be set. */
    ASSERT_EQ_U32(0xDEADBEEF, sw.body_color_seed[body_idx]);
    ASSERT_TRUE(sw.body_shape_type[body_idx] == 0); /* box */

    /* Body should have upward + forward velocity. */
    ASSERT_TRUE(b->linear_vel.y > 0.0f);

    demo_server_world_destroy(&sw);
    return 0;
}

static int test_spawn_box_invalid_slot(void) {
    demo_server_world_t sw;
    ASSERT_EQ_INT(0, demo_server_world_init(&sw, 100));

    demo_input_spawn_t spawn;
    memset(&spawn, 0, sizeof(spawn));
    spawn.half_x_mm = 250;
    spawn.half_y_mm = 250;
    spawn.half_z_mm = 250;

    /* No player connected at slot 0. */
    uint32_t body_idx = demo_server_world_spawn_box(&sw, 0, &spawn);
    ASSERT_EQ_U32(UINT32_MAX, body_idx);

    demo_server_world_destroy(&sw);
    return 0;
}

static int test_spawn_box_mass_proportional_to_volume(void) {
    demo_server_world_t sw;
    ASSERT_EQ_INT(0, demo_server_world_init(&sw, 100));
    int slot = demo_server_world_add_player(&sw);

    /* Spawn a 1m x 1m x 1m box (half-extents = 500mm). */
    demo_input_spawn_t spawn;
    memset(&spawn, 0, sizeof(spawn));
    spawn.half_x_mm = 500;
    spawn.half_y_mm = 500;
    spawn.half_z_mm = 500;

    uint32_t body_idx = demo_server_world_spawn_box(&sw, slot, &spawn);
    ASSERT_TRUE(body_idx != UINT32_MAX);

    phys_body_t *b = phys_world_get_body(&sw.physics, body_idx);
    /* Volume = 8 * 0.5 * 0.5 * 0.5 = 1.0 m³, density 500 => mass = 500 kg. */
    /* inv_mass should be 1/500. */
    ASSERT_FLOAT_NEAR(1.0f / 500.0f, b->inv_mass, 0.001f);

    demo_server_world_destroy(&sw);
    return 0;
}

/* ── Tick tests ────────────────────────────────────────────────── */

static int test_tick_advances_physics(void) {
    demo_server_world_t sw;
    ASSERT_EQ_INT(0, demo_server_world_init(&sw, 100));

    uint64_t before = sw.physics.tick_count;
    demo_server_world_tick(&sw, NULL);
    ASSERT_TRUE(sw.physics.tick_count > before);

    demo_server_world_destroy(&sw);
    return 0;
}

static int test_tick_increments_spawn_counter(void) {
    demo_server_world_t sw;
    ASSERT_EQ_INT(0, demo_server_world_init(&sw, 100));

    ASSERT_EQ_U32(0u, sw.ticks_since_spawn);
    demo_server_world_tick(&sw, NULL);
    ASSERT_EQ_U32(1u, sw.ticks_since_spawn);
    demo_server_world_tick(&sw, NULL);
    ASSERT_EQ_U32(2u, sw.ticks_since_spawn);

    demo_server_world_destroy(&sw);
    return 0;
}

static int test_tick_spawns_distant_object_eventually(void) {
    demo_server_world_t sw;
    ASSERT_EQ_INT(0, demo_server_world_init(&sw, 42));

    /* Body count before ticking. Ground = 1 body. */
    uint32_t bodies_before = phys_world_body_count(&sw.physics);

    /* Tick enough times that the random spawner should fire at least once. */
    for (int i = 0; i < 120; i++) {
        demo_server_world_tick(&sw, NULL);
    }

    uint32_t bodies_after = phys_world_body_count(&sw.physics);
    /* At least one distant object should have spawned. */
    ASSERT_TRUE(bodies_after > bodies_before);

    demo_server_world_destroy(&sw);
    return 0;
}

/* ── Edge case tests ───────────────────────────────────────────── */

static int test_remove_invalid_slot(void) {
    demo_server_world_t sw;
    ASSERT_EQ_INT(0, demo_server_world_init(&sw, 100));

    /* Removing a player from an unoccupied slot should be safe. */
    demo_server_world_remove_player(&sw, 0);
    demo_server_world_remove_player(&sw, -1);
    demo_server_world_remove_player(&sw, DEMO_MAX_CLIENTS);

    demo_server_world_destroy(&sw);
    return 0;
}

static int test_apply_input_invalid_slot(void) {
    demo_server_world_t sw;
    ASSERT_EQ_INT(0, demo_server_world_init(&sw, 100));

    demo_input_move_t input;
    memset(&input, 0, sizeof(input));

    /* Should not crash on invalid slot. */
    demo_server_world_apply_input(&sw, -1, &input, 0.016f);
    demo_server_world_apply_input(&sw, DEMO_MAX_CLIENTS, &input, 0.016f);
    demo_server_world_apply_input(&sw, 0, &input, 0.016f); /* not connected */

    demo_server_world_destroy(&sw);
    return 0;
}

/* ── Main ──────────────────────────────────────────────────────── */

int main(void) {
    int test_count = 0, fail_count = 0;
    printf("p081_demo_server_world_tests\n");

    /* Init / Destroy */
    RUN_TEST(test_init_success);
    RUN_TEST(test_init_default_rng_seed);
    RUN_TEST(test_destroy_idempotent);

    /* Player management */
    RUN_TEST(test_add_player);
    RUN_TEST(test_add_max_players);
    RUN_TEST(test_remove_player);
    RUN_TEST(test_remove_player_then_add);

    /* Input */
    RUN_TEST(test_apply_input_forward);
    RUN_TEST(test_apply_input_updates_yaw_pitch);

    /* Spawning */
    RUN_TEST(test_spawn_box);
    RUN_TEST(test_spawn_box_invalid_slot);
    RUN_TEST(test_spawn_box_mass_proportional_to_volume);

    /* Tick */
    RUN_TEST(test_tick_advances_physics);
    RUN_TEST(test_tick_increments_spawn_counter);
    RUN_TEST(test_tick_spawns_distant_object_eventually);

    /* Edge cases */
    RUN_TEST(test_remove_invalid_slot);
    RUN_TEST(test_apply_input_invalid_slot);

    printf("\n%d/%d passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
