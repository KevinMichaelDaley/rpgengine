/**
 * @file p080_physics_phase4_integration_tests.c
 * @brief Phase 4 integration tests: tiered simulation correctness + benchmark.
 *
 * Validates end-to-end behavior of Phase 4 features:
 *   - Distance-based tier classification (T0–T4)
 *   - Hysteresis preventing tier flapping
 *   - Amortized T4 ticking
 *   - Sphere simplification at distance
 *   - Occlusion-based demotion
 *   - Cross-tier solver mode selection
 *   - Full mixed-tier pipeline stability
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ferrum/physics/world.h"
#include "ferrum/physics/tick.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/game_state.h"
#include "ferrum/physics/tier_classify.h"
#include "ferrum/physics/tier_list.h"
#include "ferrum/physics/step_plan.h"
#include "ferrum/physics/amortized.h"
#include "ferrum/physics/collision/sphere_simplify.h"
#include "ferrum/physics/solver_transition.h"
#include "ferrum/physics/occlusion_nudge.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define TEST_FAIL(msg, ...)                                                    \
    do {                                                                        \
        fprintf(stderr, "FAIL %s:%d: " msg "\n", __FILE__, __LINE__,           \
                ##__VA_ARGS__);                                                \
        return 1;                                                              \
    } while (0)

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                        \
        if (!(cond)) {                                                         \
            TEST_FAIL("%s", #cond);                                            \
        }                                                                       \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, tol)                                       \
    do {                                                                        \
        float _e = (exp), _a = (act), _t = (tol);                              \
        if (fabsf(_e - _a) > _t) {                                             \
            TEST_FAIL("expected %.6f got %.6f (tol %.6f)",                     \
                      (double)_e, (double)_a, (double)_t);                     \
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

/* Float tolerance for comparing positions. */
#define TOLERANCE 1e-4f

/* ── Identity quaternion ───────────────────────────────────────── */

static const phys_quat_t QUAT_IDENTITY = {.x = 0, .y = 0, .z = 0, .w = 1};

/* ── Simple LCG for deterministic random ──────────────────────── */

static uint32_t lcg_next(uint32_t *state) {
    *state = (*state) * 1664525u + 1013904223u;
    return *state;
}

static float lcg_float(uint32_t *state, float lo, float hi) {
    uint32_t r = lcg_next(state);
    float t = (float)(r & 0xFFFFu) / 65535.0f;
    return lo + t * (hi - lo);
}

/* ── Helper: create a test world ───────────────────────────────── */

static int make_test_world(phys_world_t *world) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 128;
    cfg.max_colliders = 128;
    cfg.manifold_cache_size = 128;
    cfg.frame_arena_size = 2u * 1024u * 1024u;
    cfg.default_substeps = 1;
    cfg.default_solver_iterations = 8;
    return phys_world_init(world, &cfg);
}

static int make_large_test_world(phys_world_t *world, uint32_t max_bodies) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = max_bodies;
    cfg.max_colliders = max_bodies;
    cfg.manifold_cache_size = max_bodies;
    cfg.frame_arena_size = 8u * 1024u * 1024u;
    cfg.default_substeps = 1;
    cfg.default_solver_iterations = 4;
    return phys_world_init(world, &cfg);
}

/* ── Body creation helpers ─────────────────────────────────────── */

/**
 * @brief Create a dynamic sphere at a given position.
 * @return Body index, or UINT32_MAX on failure.
 */
static uint32_t create_dynamic_sphere(phys_world_t *world,
                                      phys_vec3_t pos,
                                      phys_vec3_t vel,
                                      float mass, float radius) {
    uint32_t idx = phys_world_create_body(world);
    if (idx == UINT32_MAX) return idx;

    phys_body_t *body = phys_world_get_body(world, idx);
    body->position = pos;
    body->linear_vel = vel;
    body->flags = 0;
    phys_body_set_mass(body, mass);
    phys_body_set_sphere_inertia(body, mass, radius);

    /* Copy to next buffer so integration reads correct initial state. */
    phys_body_t *next = phys_body_pool_get_next(&world->body_pool, idx);
    *next = *body;

    phys_world_set_sphere_collider(world, idx, radius,
                                   (phys_vec3_t){0, 0, 0});
    return idx;
}

/**
 * @brief Create a dynamic box at a given position.
 * @return Body index, or UINT32_MAX on failure.
 */
static uint32_t create_dynamic_box(phys_world_t *world,
                                   phys_vec3_t pos,
                                   phys_vec3_t vel,
                                   float mass,
                                   phys_vec3_t half_extents) {
    uint32_t idx = phys_world_create_body(world);
    if (idx == UINT32_MAX) return idx;

    phys_body_t *body = phys_world_get_body(world, idx);
    body->position = pos;
    body->linear_vel = vel;
    body->flags = 0;
    phys_body_set_mass(body, mass);
    phys_body_set_box_inertia(body, mass, half_extents);

    phys_body_t *next = phys_body_pool_get_next(&world->body_pool, idx);
    *next = *body;

    phys_world_set_box_collider(world, idx, half_extents,
                                (phys_vec3_t){0, 0, 0}, QUAT_IDENTITY);
    return idx;
}

/* ── Helper: set up game state with a player at a position ──── */

static void setup_game_state_with_player(phys_game_state_t *game,
                                         phys_vec3_t player_pos) {
    phys_game_state_init(game);
    phys_player_state_t player;
    memset(&player, 0, sizeof(player));
    player.position = player_pos;
    phys_game_state_set_player(game, 0, &player);
}

/* ── Helper: classify bodies and write per-body tier ──────────── */

static void classify_tiers(phys_world_t *world,
                           const phys_game_state_t *game,
                           uint8_t *tier_out,
                           const uint8_t *visibility_set) {
    /* Build active flags. */
    uint32_t cap = world->body_pool.capacity;
    uint8_t active[128];
    memset(active, 0, sizeof(active));
    for (uint32_t i = 0; i < cap && i < 128; ++i) {
        active[i] = world->body_pool.active[i];
    }

    /* Reset frame arena for tier list allocation. */
    phys_frame_arena_reset(&world->frame_arena);

    phys_tier_lists_t tier_lists;
    phys_stage_tier_classify(&(phys_tier_classify_args_t){
        .bodies         = world->body_pool.bodies_curr,
        .active         = active,
        .body_count     = cap,
        .game           = game,
        .tier_lists_out = &tier_lists,
        .arena          = &world->frame_arena,
        .tier_out       = tier_out,
        .visibility_set = visibility_set,
    });
}

/* ================================================================
 * Test 1: Tier promotion on approach
 *
 * Body starts 300m away (T4). Player moves to 3m → body promotes to T0.
 * ================================================================ */
static int test_tier_promotion_on_approach(void) {
    phys_world_t world;
    ASSERT_TRUE(make_test_world(&world) == 0);

    /* Disable gravity for clean tier test. */
    world.config.gravity = (phys_vec3_t){0, 0, 0};

    /* Body at 300m along +X axis (well into T4 range). */
    uint32_t body_idx = create_dynamic_sphere(
        &world, (phys_vec3_t){300.0f, 0, 0},
        (phys_vec3_t){0, 0, 0}, 1.0f, 0.5f);
    ASSERT_TRUE(body_idx != UINT32_MAX);

    /* Player starts far away too — body should be T4. */
    phys_game_state_t game;
    setup_game_state_with_player(&game, (phys_vec3_t){0, 0, 0});

    uint8_t tiers[128];
    memset(tiers, 0, sizeof(tiers));

    /* Classify: body at 300m → T4. */
    classify_tiers(&world, &game, tiers, NULL);
    ASSERT_TRUE(tiers[body_idx] == PHYS_TIER_4_BACKGROUND);

    /* Now move player close to the body. */
    setup_game_state_with_player(&game, (phys_vec3_t){298.0f, 0, 0});

    /* Classify again: distance ~2m → T0. */
    classify_tiers(&world, &game, tiers, NULL);
    ASSERT_TRUE(tiers[body_idx] == PHYS_TIER_0_DIRECT);

    phys_world_destroy(&world);
    return 0;
}

/* ================================================================
 * Test 2: Tier demotion on distance
 *
 * Body starts near (T0). Player moves far away → body demotes.
 * ================================================================ */
static int test_tier_demotion_on_distance(void) {
    phys_world_t world;
    ASSERT_TRUE(make_test_world(&world) == 0);
    world.config.gravity = (phys_vec3_t){0, 0, 0};

    /* Body at origin. */
    uint32_t body_idx = create_dynamic_sphere(
        &world, (phys_vec3_t){0, 0, 0},
        (phys_vec3_t){0, 0, 0}, 1.0f, 0.5f);
    ASSERT_TRUE(body_idx != UINT32_MAX);

    /* Player at origin → body should be T0. */
    phys_game_state_t game;
    setup_game_state_with_player(&game, (phys_vec3_t){0, 0, 0});

    uint8_t tiers[128];
    memset(tiers, 0, sizeof(tiers));
    classify_tiers(&world, &game, tiers, NULL);
    ASSERT_TRUE(tiers[body_idx] == PHYS_TIER_0_DIRECT);

    /* Set body's tier field so hysteresis can reference it. */
    phys_body_t *body = phys_world_get_body(&world, body_idx);
    body->tier = (uint8_t)PHYS_TIER_0_DIRECT;
    phys_body_t *next = phys_body_pool_get_next(&world.body_pool, body_idx);
    next->tier = body->tier;

    /* Move player to 250m away → body at origin is 250m from player → T4.
     * Hysteresis demotion threshold for T0 is 6m, so 250m > 6m → demotes. */
    setup_game_state_with_player(&game, (phys_vec3_t){250.0f, 0, 0});
    classify_tiers(&world, &game, tiers, NULL);
    ASSERT_TRUE(tiers[body_idx] == PHYS_TIER_4_BACKGROUND);

    phys_world_destroy(&world);
    return 0;
}

/* ================================================================
 * Test 3: Hysteresis prevents flapping
 *
 * Body is at current tier T0. Distance oscillates around the T0
 * demotion threshold (6m). Hysteresis should keep it at T0 when
 * the distance doesn't exceed the demotion threshold.
 * ================================================================ */
static int test_hysteresis_prevents_flapping(void) {
    phys_world_t world;
    ASSERT_TRUE(make_test_world(&world) == 0);
    world.config.gravity = (phys_vec3_t){0, 0, 0};

    /* Body at origin. */
    uint32_t body_idx = create_dynamic_sphere(
        &world, (phys_vec3_t){0, 0, 0},
        (phys_vec3_t){0, 0, 0}, 1.0f, 0.5f);
    ASSERT_TRUE(body_idx != UINT32_MAX);

    /* Set body's current tier to T0. */
    phys_body_t *body = phys_world_get_body(&world, body_idx);
    body->tier = (uint8_t)PHYS_TIER_0_DIRECT;
    phys_body_t *next = phys_body_pool_get_next(&world.body_pool, body_idx);
    next->tier = body->tier;

    phys_game_state_t game;
    uint8_t tiers[128];
    memset(tiers, 0, sizeof(tiers));

    /* Player at 5.5m — past the raw T0 threshold (5m) but below
     * the hysteresis demotion threshold (6m). Should stay at T0. */
    setup_game_state_with_player(&game, (phys_vec3_t){5.5f, 0, 0});
    classify_tiers(&world, &game, tiers, NULL);
    ASSERT_TRUE(tiers[body_idx] == PHYS_TIER_0_DIRECT);

    /* Player at 4.5m — back inside T0 raw threshold. Still T0. */
    setup_game_state_with_player(&game, (phys_vec3_t){4.5f, 0, 0});
    classify_tiers(&world, &game, tiers, NULL);
    ASSERT_TRUE(tiers[body_idx] == PHYS_TIER_0_DIRECT);

    /* Player at 5.9m — still below demotion threshold (6m). Stay T0. */
    setup_game_state_with_player(&game, (phys_vec3_t){5.9f, 0, 0});
    classify_tiers(&world, &game, tiers, NULL);
    ASSERT_TRUE(tiers[body_idx] == PHYS_TIER_0_DIRECT);

    /* Player at 7.0m — exceeds demotion threshold (6m). Should demote to T1. */
    setup_game_state_with_player(&game, (phys_vec3_t){7.0f, 0, 0});
    classify_tiers(&world, &game, tiers, NULL);
    ASSERT_TRUE(tiers[body_idx] == PHYS_TIER_1_NEAR);

    phys_world_destroy(&world);
    return 0;
}

/* ================================================================
 * Test 4: T4 amortized skip
 *
 * T4 bodies are only physics-ticked every 3rd frame. Verify that
 * the amortized snapshot/interpolate cycle works correctly: the
 * snapshot captures the pre-tick pose, and interpolation blends
 * between snapshot and current pose on off-frames.
 * ================================================================ */
static int test_t4_amortized_skip(void) {
    /* Test the amortized state snapshot/interpolation directly. */
    phys_amortized_state_t amort;
    ASSERT_TRUE(phys_amortized_init(&amort, 4));

    /* Set up a mock body with a known position. Tier must be T4
     * because snapshot/interpolate only operate on T4 bodies. */
    phys_body_t bodies[4];
    memset(bodies, 0, sizeof(bodies));

    bodies[0].position = (phys_vec3_t){10.0f, 0.0f, 0.0f};
    bodies[0].orientation = (phys_quat_t){0, 0, 0, 1};
    bodies[0].tier = (uint8_t)PHYS_TIER_4_BACKGROUND;

    /* Frame 0 is a tick frame (0 % 3 == 0) — snapshot captures pose. */
    phys_amortized_snapshot(&amort, bodies, 1, 0);

    /* Simulate the body moving to x=13 after the tick. */
    bodies[0].position.x = 13.0f;

    /* Frame 1 (off-frame): interpolation with alpha = 1/3. */
    phys_vec3_t vis_pos[4];
    phys_quat_t vis_rot[4];
    phys_amortized_interpolate(&amort, bodies, 1, 1, vis_pos, vis_rot);

    /* Expected: lerp(10, 13, 1/3) ≈ 11.0 */
    ASSERT_FLOAT_NEAR(11.0f, vis_pos[0].x, 0.2f);

    /* Frame 2 (off-frame): alpha = 2/3. */
    phys_amortized_interpolate(&amort, bodies, 1, 2, vis_pos, vis_rot);
    /* Expected: lerp(10, 13, 2/3) ≈ 12.0 */
    ASSERT_FLOAT_NEAR(12.0f, vis_pos[0].x, 0.2f);

    /* Frame 3 is next tick frame — snapshot again. */
    phys_amortized_snapshot(&amort, bodies, 1, 3);

    /* Verify the tick interval constant. */
    ASSERT_TRUE(PHYS_T4_TICK_INTERVAL == 3);

    phys_amortized_destroy(&amort);
    return 0;
}

/* ================================================================
 * Test 5: Sphere simplification at distance
 *
 * A near-cubical box (equal half-extents) has a bounding-sphere
 * ratio close to sqrt(3) ≈ 1.73 (not simplified). A sphere always
 * has ratio 1.0 (simplified).
 * ================================================================ */
static int test_sphere_simplify_at_distance(void) {
    phys_world_t world;
    ASSERT_TRUE(make_test_world(&world) == 0);
    world.config.gravity = (phys_vec3_t){0, 0, 0};

    /* Sphere body — ratio should be 1.0 (perfect sphere). */
    uint32_t sphere_idx = create_dynamic_sphere(
        &world, (phys_vec3_t){0, 0, 0},
        (phys_vec3_t){0, 0, 0}, 1.0f, 1.0f);
    ASSERT_TRUE(sphere_idx != UINT32_MAX);

    /* Box body with equal half-extents (cube). */
    uint32_t box_idx = create_dynamic_box(
        &world, (phys_vec3_t){10, 0, 0},
        (phys_vec3_t){0, 0, 0}, 1.0f,
        (phys_vec3_t){1.0f, 1.0f, 1.0f});
    ASSERT_TRUE(box_idx != UINT32_MAX);

    /* Query sphere ratio for the sphere collider. */
    const phys_collider_t *sphere_col = phys_world_get_collider(&world, sphere_idx);
    ASSERT_TRUE(sphere_col != NULL);
    float sphere_ratio = phys_sphere_ratio(sphere_col,
                                           world.spheres, world.boxes, world.capsules);
    ASSERT_FLOAT_NEAR(1.0f, sphere_ratio, 0.01f);

    /* Query sphere ratio for the cube collider.
     * Cube circumradius = sqrt(3), inradius = 1 → ratio ≈ 1.73. */
    const phys_collider_t *box_col = phys_world_get_collider(&world, box_idx);
    ASSERT_TRUE(box_col != NULL);
    float box_ratio = phys_sphere_ratio(box_col,
                                        world.spheres, world.boxes, world.capsules);
    /* Box ratio should be > 1.3 (not a simplification candidate). */
    ASSERT_TRUE(box_ratio > 1.3f);

    /* Query the simplify radius for the sphere — should equal its actual radius. */
    float sphere_bounding_r = phys_sphere_simplify_radius(sphere_col,
                                                          world.spheres, world.boxes,
                                                          world.capsules);
    ASSERT_FLOAT_NEAR(1.0f, sphere_bounding_r, 0.01f);

    /* The box's bounding radius should be sqrt(3) ≈ 1.732. */
    float box_bounding_r = phys_sphere_simplify_radius(box_col,
                                                       world.spheres, world.boxes,
                                                       world.capsules);
    ASSERT_TRUE(box_bounding_r > 1.5f);

    phys_world_destroy(&world);
    return 0;
}

/* ================================================================
 * Test 6: Occlusion demotes T1 to T3
 *
 * A body in T1 range with its visibility bit cleared should be
 * demoted to T3.
 * ================================================================ */
static int test_occlusion_demotes_t1_to_t3(void) {
    phys_world_t world;
    ASSERT_TRUE(make_test_world(&world) == 0);
    world.config.gravity = (phys_vec3_t){0, 0, 0};

    /* Body at 10m along X (within T1 range: 5–20m). */
    uint32_t body_idx = create_dynamic_sphere(
        &world, (phys_vec3_t){10.0f, 0, 0},
        (phys_vec3_t){0, 0, 0}, 1.0f, 0.5f);
    ASSERT_TRUE(body_idx != UINT32_MAX);

    /* Player at origin. */
    phys_game_state_t game;
    setup_game_state_with_player(&game, (phys_vec3_t){0, 0, 0});

    /* Build a visibility set with the body's bit cleared (not visible). */
    uint8_t visibility[16]; /* enough bits for 128 bodies */
    memset(visibility, 0xFF, sizeof(visibility)); /* all visible initially */

    /* Clear the bit for body_idx. */
    visibility[body_idx / 8] &= (uint8_t)~(1u << (body_idx % 8));

    uint8_t tiers[128];
    memset(tiers, 0, sizeof(tiers));

    classify_tiers(&world, &game, tiers, visibility);

    /* Without occlusion, body at 10m would be T1.
     * With visibility cleared, it should be demoted to T3. */
    ASSERT_TRUE(tiers[body_idx] == PHYS_TIER_3_WORLD);

    phys_world_destroy(&world);
    return 0;
}

/* ================================================================
 * Test 7: Solver mode cross-tier selection
 *
 * A constraint between T0 and T2 bodies should use TGS (high
 * fidelity wins).  A constraint between T2 and T3 should use XPBD.
 * ================================================================ */
static int test_solver_mode_cross_tier(void) {
    /* T0 × T2 → TGS (T0 is high fidelity). */
    phys_solver_mode_t mode_t0_t2 = phys_tier_cross_solver_mode(
        PHYS_TIER_0_DIRECT, PHYS_TIER_2_VISIBLE);
    ASSERT_TRUE(mode_t0_t2 == PHYS_SOLVER_TGS);

    /* T1 × T3 → TGS (T1 is high fidelity). */
    phys_solver_mode_t mode_t1_t3 = phys_tier_cross_solver_mode(
        PHYS_TIER_1_NEAR, PHYS_TIER_3_WORLD);
    ASSERT_TRUE(mode_t1_t3 == PHYS_SOLVER_TGS);

    /* T2 × T3 → XPBD (neither is high fidelity). */
    phys_solver_mode_t mode_t2_t3 = phys_tier_cross_solver_mode(
        PHYS_TIER_2_VISIBLE, PHYS_TIER_3_WORLD);
    ASSERT_TRUE(mode_t2_t3 == PHYS_SOLVER_XPBD);

    /* T0 × T0 → TGS. */
    phys_solver_mode_t mode_t0_t0 = phys_tier_cross_solver_mode(
        PHYS_TIER_0_DIRECT, PHYS_TIER_0_DIRECT);
    ASSERT_TRUE(mode_t0_t0 == PHYS_SOLVER_TGS);

    /* T4 × T4 → XPBD. */
    phys_solver_mode_t mode_t4_t4 = phys_tier_cross_solver_mode(
        PHYS_TIER_4_BACKGROUND, PHYS_TIER_4_BACKGROUND);
    ASSERT_TRUE(mode_t4_t4 == PHYS_SOLVER_XPBD);

    return 0;
}

/* ================================================================
 * Test 8: Full pipeline with mixed tiers
 *
 * 20 bodies at various distances, 5 ticks with game state.
 * Verifies no crashes and T0 bodies move normally.
 * ================================================================ */
static int test_full_pipeline_mixed_tiers(void) {
    phys_world_t world;
    ASSERT_TRUE(make_test_world(&world) == 0);

    /* Player at origin — bodies at various distances get different tiers. */
    phys_game_state_t game;
    setup_game_state_with_player(&game, (phys_vec3_t){0, 0, 0});

    /*
     * Distance-to-tier mapping (promotion thresholds):
     *   < 5m   → T0
     *   < 20m  → T1
     *   < 50m  → T2
     *   < 200m → T3
     *   ≥ 200m → T4
     */
    float distances[] = {
        /* T0 bodies (5 near bodies) */
        1.0f, 2.0f, 3.0f, 4.0f, 4.5f,
        /* T1 bodies (5) */
        8.0f, 10.0f, 12.0f, 15.0f, 18.0f,
        /* T2 bodies (4) */
        25.0f, 30.0f, 40.0f, 45.0f,
        /* T3 bodies (3) */
        60.0f, 100.0f, 150.0f,
        /* T4 bodies (3) */
        250.0f, 400.0f, 500.0f,
    };
    uint32_t body_count = sizeof(distances) / sizeof(distances[0]);
    ASSERT_TRUE(body_count == 20);

    uint32_t body_indices[20];
    for (uint32_t i = 0; i < body_count; ++i) {
        body_indices[i] = create_dynamic_sphere(
            &world,
            (phys_vec3_t){distances[i], 0.0f, 0.0f},
            (phys_vec3_t){0, 0, 0},
            1.0f, 0.5f);
        ASSERT_TRUE(body_indices[i] != UINT32_MAX);
    }

    /* Tick 5 times with game state. */
    for (int tick = 0; tick < 5; ++tick) {
        phys_world_tick(&world, &game);
    }

    ASSERT_TRUE(phys_world_tick_count(&world) == 5);

    /* T0 bodies (near the player) should have moved under gravity.
     * At dt=1/60, after 5 ticks they'll have fallen slightly. */
    for (uint32_t i = 0; i < 5; ++i) {
        phys_body_t *b = phys_world_get_body(&world, body_indices[i]);
        ASSERT_TRUE(b != NULL);
        /* Gravity should have pulled them down from y=0. */
        ASSERT_TRUE(b->position.y < 0.0f);
    }

    /* All bodies should still be valid (no crashes). */
    for (uint32_t i = 0; i < body_count; ++i) {
        phys_body_t *b = phys_world_get_body(&world, body_indices[i]);
        ASSERT_TRUE(b != NULL);
        /* Position should be finite. */
        ASSERT_TRUE(isfinite(b->position.x));
        ASSERT_TRUE(isfinite(b->position.y));
        ASSERT_TRUE(isfinite(b->position.z));
    }

    /* Verify tier classification is correct for final state. */
    uint8_t tiers[128];
    memset(tiers, 0, sizeof(tiers));
    classify_tiers(&world, &game, tiers, NULL);

    /* Near bodies should be T0. */
    for (uint32_t i = 0; i < 5; ++i) {
        ASSERT_TRUE(tiers[body_indices[i]] == PHYS_TIER_0_DIRECT);
    }
    /* Far bodies (250m+) should be T4. */
    for (uint32_t i = 17; i < 20; ++i) {
        ASSERT_TRUE(tiers[body_indices[i]] == PHYS_TIER_4_BACKGROUND);
    }

    phys_world_destroy(&world);
    return 0;
}

/* ================================================================
 * Benchmark: 100 bodies, 10 ticks, average tick time.
 * ================================================================ */
static int bench_100_bodies_10_ticks(void) {
    phys_world_t world;
    ASSERT_TRUE(make_large_test_world(&world, 256) == 0);

    phys_game_state_t game;
    setup_game_state_with_player(&game, (phys_vec3_t){0, 0, 0});

    uint32_t rng = 42;
    for (int i = 0; i < 100; ++i) {
        float x = lcg_float(&rng, -50.0f, 300.0f);
        float y = lcg_float(&rng, 2.0f, 10.0f);
        float z = lcg_float(&rng, -50.0f, 50.0f);
        uint32_t idx = create_dynamic_sphere(
            &world,
            (phys_vec3_t){x, y, z},
            (phys_vec3_t){0, 0, 0},
            1.0f, 0.5f);
        (void)idx;
    }

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int tick = 0; tick < 10; ++tick) {
        phys_world_tick(&world, &game);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed_ms = (double)(end.tv_sec - start.tv_sec) * 1000.0
                      + (double)(end.tv_nsec - start.tv_nsec) / 1e6;
    printf(" [%.2f ms total, %.2f ms/tick]", elapsed_ms, elapsed_ms / 10.0);

    phys_world_destroy(&world);
    return 0;
}

/* ── Main ──────────────────────────────────────────────────────── */

int main(void) {
    int test_count = 0;
    int fail_count = 0;

    printf("p080_physics_phase4_integration_tests\n");
    RUN_TEST(test_tier_promotion_on_approach);
    RUN_TEST(test_tier_demotion_on_distance);
    RUN_TEST(test_hysteresis_prevents_flapping);
    RUN_TEST(test_t4_amortized_skip);
    RUN_TEST(test_sphere_simplify_at_distance);
    RUN_TEST(test_occlusion_demotes_t1_to_t3);
    RUN_TEST(test_solver_mode_cross_tier);
    RUN_TEST(test_full_pipeline_mixed_tiers);

    printf("\nBenchmarks:\n");
    RUN_TEST(bench_100_bodies_10_ticks);

    printf("\n%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
