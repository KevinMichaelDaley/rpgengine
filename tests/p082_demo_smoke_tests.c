/**
 * @file p082_demo_smoke_tests.c
 * @brief Demo smoke test: in-process server world integration test.
 *
 * Exercises the demo server world API end-to-end without networking.
 * Verifies ground plane, player management, box spawning, movement,
 * gravity, and the random distant object spawner.
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

#define ASSERT_NEAR(a, b, tol)                                                 \
    do {                                                                        \
        float _a = (float)(a), _b = (float)(b), _t = (float)(tol);             \
        if (fabsf(_a - _b) > _t) {                                             \
            fprintf(stderr, "ASSERT_NEAR failed: %s:%d: "                      \
                    "%.6f vs %.6f (tol %.6f)\n",                                \
                    __FILE__, __LINE__, (double)_a, (double)_b, (double)_t);    \
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

/* ── Test 1: world init creates ground body ────────────────────── */

static int test_world_init_has_ground(void) {
    demo_server_world_t sw;
    ASSERT_TRUE(demo_server_world_init(&sw, 42) == 0);

    /* Body 0 (DEMO_GROUND_BODY) must exist and be static at y=-0.5. */
    phys_body_t *ground = phys_world_get_body(&sw.physics, DEMO_GROUND_BODY);
    ASSERT_TRUE(ground != NULL);
    ASSERT_TRUE((ground->flags & PHYS_BODY_FLAG_STATIC) != 0);
    ASSERT_NEAR(ground->position.y, -0.5f, 0.01f);

    demo_server_world_destroy(&sw);
    return 0;
}

/* ── Test 2: add 4 players ─────────────────────────────────────── */

static int test_add_4_players(void) {
    demo_server_world_t sw;
    ASSERT_TRUE(demo_server_world_init(&sw, 42) == 0);

    int slots[4];
    for (int i = 0; i < 4; i++) {
        slots[i] = demo_server_world_add_player(&sw);
        ASSERT_TRUE(slots[i] >= 0);
    }

    /* 1 ground + 4 players = 5 active bodies. */
    uint32_t count = phys_world_body_count(&sw.physics);
    ASSERT_TRUE(count == 5);

    /* Each player body must be kinematic. */
    for (int i = 0; i < 4; i++) {
        phys_body_t *pb = phys_world_get_body(&sw.physics,
                                               sw.player_body[slots[i]]);
        ASSERT_TRUE(pb != NULL);
        ASSERT_TRUE(phys_body_is_kinematic(pb));
    }

    demo_server_world_destroy(&sw);
    return 0;
}

/* ── Test 3: spawn a box ───────────────────────────────────────── */

static int test_spawn_box(void) {
    demo_server_world_t sw;
    ASSERT_TRUE(demo_server_world_init(&sw, 42) == 0);

    int slot = demo_server_world_add_player(&sw);
    ASSERT_TRUE(slot >= 0);

    /* Record player position before spawn. */
    phys_body_t *player = phys_world_get_body(&sw.physics,
                                               sw.player_body[slot]);
    ASSERT_TRUE(player != NULL);
    float px = player->position.x;
    float py = player->position.y;
    float pz = player->position.z;

    /* Spawn a box (500mm half-extents). */
    demo_input_spawn_t spawn;
    memset(&spawn, 0, sizeof(spawn));
    spawn.half_x_mm = 500;
    spawn.half_y_mm = 500;
    spawn.half_z_mm = 500;
    spawn.color_seed = 0xDEAD;

    uint32_t body_idx = demo_server_world_spawn_box(&sw, slot, &spawn);
    ASSERT_TRUE(body_idx != UINT32_MAX);

    /* The new body must be dynamic (has mass). */
    phys_body_t *box = phys_world_get_body(&sw.physics, body_idx);
    ASSERT_TRUE(box != NULL);
    ASSERT_TRUE(!phys_body_is_static(box));
    ASSERT_TRUE(!phys_body_is_kinematic(box));
    ASSERT_TRUE(box->inv_mass > 0.0f);

    /*
     * Box should be ~2m in front of the player.  Player yaw is 0,
     * so forward is (sin(0), 0, -cos(0)) = (0, 0, -1).
     * Expected position: (px + 0, py + 0.9, pz - 2).
     */
    ASSERT_NEAR(box->position.x, px, 0.1f);
    ASSERT_NEAR(box->position.y, py + 0.9f, 0.1f);
    ASSERT_NEAR(box->position.z, pz - 2.0f, 0.1f);

    demo_server_world_destroy(&sw);
    return 0;
}

/* ── Test 4: apply input moves player forward ──────────────────── */

static int test_apply_input_moves_player(void) {
    demo_server_world_t sw;
    ASSERT_TRUE(demo_server_world_init(&sw, 42) == 0);

    int slot = demo_server_world_add_player(&sw);
    ASSERT_TRUE(slot >= 0);

    phys_body_t *player = phys_world_get_body(&sw.physics,
                                               sw.player_body[slot]);
    ASSERT_TRUE(player != NULL);
    float start_z = player->position.z;

    /* Build a movement input: W key pressed, yaw=0 (forward = -Z). */
    demo_input_move_t input;
    memset(&input, 0, sizeof(input));
    input.move_flags = DEMO_MOVE_W;
    input.yaw_snorm16 = 0;
    input.pitch_snorm16 = 0;

    /* Apply for 30 ticks at 1/60s each. */
    float dt = 1.0f / 60.0f;
    for (int i = 0; i < 30; i++) {
        demo_server_world_apply_input(&sw, slot, &input, dt);
    }

    /* Re-fetch body pointer (may have moved buffers). */
    player = phys_world_get_body(&sw.physics, sw.player_body[slot]);
    ASSERT_TRUE(player != NULL);

    /* Player should have moved forward (-Z direction). */
    float dz = player->position.z - start_z;
    ASSERT_TRUE(dz < -0.1f);  /* Moved at least 0.1m in -Z. */

    demo_server_world_destroy(&sw);
    return 0;
}

/* ── Test 5: gravity pulls spawned box down ────────────────────── */

static int test_physics_tick_gravity(void) {
    demo_server_world_t sw;
    ASSERT_TRUE(demo_server_world_init(&sw, 42) == 0);

    int slot = demo_server_world_add_player(&sw);
    ASSERT_TRUE(slot >= 0);

    /* Spawn a box, then move it to 10m up manually. */
    demo_input_spawn_t spawn;
    memset(&spawn, 0, sizeof(spawn));
    spawn.half_x_mm = 250;
    spawn.half_y_mm = 250;
    spawn.half_z_mm = 250;

    uint32_t box_idx = demo_server_world_spawn_box(&sw, slot, &spawn);
    ASSERT_TRUE(box_idx != UINT32_MAX);

    /* Place box at y=10 with zero velocity for a clean gravity test. */
    phys_body_t *box = phys_world_get_body(&sw.physics, box_idx);
    ASSERT_TRUE(box != NULL);
    box->position.x = 0.0f;
    box->position.y = 10.0f;
    box->position.z = 0.0f;
    box->linear_vel = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    box->angular_vel = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    box->flags = 0; /* dynamic */
    box->sleep_counter = 0;

    /* Tick 60 times (~1 second of simulation). */
    for (int i = 0; i < 60; i++) {
        demo_server_world_tick(&sw);
    }

    /* Re-fetch after ticks (double-buffered). */
    box = phys_world_get_body(&sw.physics, box_idx);
    ASSERT_TRUE(box != NULL);

    /* Under gravity (9.81 m/s²) for 1 second, the box should have
     * fallen significantly.  Free-fall distance = 0.5 * 9.81 * 1² ≈ 4.9m.
     * With collisions and substeps it won't be exact, but y < 8 is safe. */
    ASSERT_TRUE(box->position.y < 8.0f);

    demo_server_world_destroy(&sw);
    return 0;
}

/* ── Test 6: random spawner creates bodies ─────────────────────── */

static int test_random_spawner(void) {
    demo_server_world_t sw;
    ASSERT_TRUE(demo_server_world_init(&sw, 42) == 0);

    /* Tick 100 times — the random spawner fires every 30-60 ticks,
     * so at least one distant body should appear. */
    for (int i = 0; i < 100; i++) {
        demo_server_world_tick(&sw);
    }

    ASSERT_TRUE(sw.dynamic_body_count > 0);

    demo_server_world_destroy(&sw);
    return 0;
}

/* ── Main ──────────────────────────────────────────────────────── */

int main(void) {
    int test_count = 0;
    int fail_count = 0;

    printf("p082_demo_smoke_tests\n");

    RUN_TEST(test_world_init_has_ground);
    RUN_TEST(test_add_4_players);
    RUN_TEST(test_spawn_box);
    RUN_TEST(test_apply_input_moves_player);
    RUN_TEST(test_physics_tick_gravity);
    RUN_TEST(test_random_spawner);

    printf("\n%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
