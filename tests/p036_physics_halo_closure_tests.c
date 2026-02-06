/**
 * @file p036_physics_halo_closure_tests.c
 * @brief Unit tests for Stage 3: Halo Closure.
 *
 * Tests cover: swept AABB computation, no promotion when all T0,
 * T2→T1 promotion, static exclusion, velocity margin extension,
 * and NULL safety.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/aabb.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/halo_closure.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/spatial_grid.h"
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

/** Grid cell count (power of 2). */
#define TEST_GRID_CELLS 64u

/** Grid cell size (world units). */
#define TEST_GRID_CELL_SIZE 4.0f

/**
 * @brief Initialize a dynamic body at a position with given velocity.
 */
static void make_dynamic_at(phys_body_t *body, float mass,
                            float px, float py, float pz,
                            float vx, float vy, float vz) {
    phys_body_init(body);
    phys_body_set_mass(body, mass);
    body->position = (phys_vec3_t){px, py, pz};
    body->linear_vel = (phys_vec3_t){vx, vy, vz};
}

/**
 * @brief Initialize a static body at a position.
 */
static void make_static_at(phys_body_t *body,
                           float px, float py, float pz) {
    phys_body_init(body);
    body->position = (phys_vec3_t){px, py, pz};
    /* phys_body_init leaves inv_mass = 0, flags = STATIC. */
}

/**
 * @brief Compute a unit-sphere AABB centered at the body's position.
 */
static void compute_sphere_aabb(phys_aabb_t *aabb, const phys_body_t *body) {
    phys_aabb_from_sphere(aabb, body->position, 1.0f);
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * Test 1: Fast-moving T0 body at origin with vel=(100,0,0), dt=1/30.
 * Two neighbor bodies at (4,0,0).  After halo, verify that neighbors
 * are found by the grid query of the swept AABB.
 */
static int test_halo_swept_aabb(void) {
    /* Set up 3 bodies: body 0 is fast-moving, bodies 1-2 are nearby. */
    phys_body_t bodies[3];
    make_dynamic_at(&bodies[0], 1.0f, 0, 0, 0, 100, 0, 0);
    make_dynamic_at(&bodies[1], 1.0f, 4, 0, 0, 0, 0, 0);
    make_dynamic_at(&bodies[2], 1.0f, 4, 0.5f, 0, 0, 0, 0);

    /* Mark body 0 as T0, bodies 1-2 as T2 (so they're eligible). */
    bodies[0].tier = PHYS_TIER_0_DIRECT;
    bodies[1].tier = PHYS_TIER_2_VISIBLE;
    bodies[2].tier = PHYS_TIER_2_VISIBLE;

    /* Compute AABBs (unit spheres). */
    phys_aabb_t aabbs[3];
    compute_sphere_aabb(&aabbs[0], &bodies[0]);
    compute_sphere_aabb(&aabbs[1], &bodies[1]);
    compute_sphere_aabb(&aabbs[2], &bodies[2]);

    /* Init arena and grid. */
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, TEST_ARENA_SIZE);

    phys_spatial_grid_t grid;
    phys_spatial_grid_init(&grid, TEST_GRID_CELLS, TEST_GRID_CELL_SIZE, &arena);

    /* Insert all bodies into grid. */
    for (uint32_t i = 0; i < 3; ++i) {
        phys_spatial_grid_insert(&grid, i, &aabbs[i]);
    }

    /* Build tier lists: body 0 in T0. */
    phys_tier_lists_t lists;
    phys_tier_lists_init(&lists, &arena, 3);
    phys_tier_list_add(&lists.tiers[PHYS_TIER_0_DIRECT], 0);

    /* Run halo closure. dt = 1/30, vel=100 → motion=3.33 in x. */
    phys_halo_closure_args_t args = {
        .bodies = bodies,
        .aabbs = aabbs,
        .grid = &grid,
        .tier_lists = &lists,
        .velocity_margin = 0.1f,
        .dt = 1.0f / 30.0f,
        .body_count = 3,
    };
    phys_stage_halo_closure(&args);

    /* Both neighbors at x=4 should be found (swept AABB extends to ~4.43). */
    ASSERT_TRUE(lists.tiers[PHYS_TIER_1_NEAR].count >= 2);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Test 2: All bodies already T0 — no changes to tier lists.
 * When all neighbors are already T0, none should be promoted to T1.
 */
static int test_halo_no_promotion_if_already_t0(void) {
    phys_body_t bodies[3];
    make_dynamic_at(&bodies[0], 1.0f, 0, 0, 0, 10, 0, 0);
    make_dynamic_at(&bodies[1], 1.0f, 2, 0, 0, 0, 0, 0);
    make_dynamic_at(&bodies[2], 1.0f, 3, 0, 0, 0, 0, 0);

    /* All are T0. */
    bodies[0].tier = PHYS_TIER_0_DIRECT;
    bodies[1].tier = PHYS_TIER_0_DIRECT;
    bodies[2].tier = PHYS_TIER_0_DIRECT;

    phys_aabb_t aabbs[3];
    compute_sphere_aabb(&aabbs[0], &bodies[0]);
    compute_sphere_aabb(&aabbs[1], &bodies[1]);
    compute_sphere_aabb(&aabbs[2], &bodies[2]);

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, TEST_ARENA_SIZE);

    phys_spatial_grid_t grid;
    phys_spatial_grid_init(&grid, TEST_GRID_CELLS, TEST_GRID_CELL_SIZE, &arena);

    for (uint32_t i = 0; i < 3; ++i) {
        phys_spatial_grid_insert(&grid, i, &aabbs[i]);
    }

    phys_tier_lists_t lists;
    phys_tier_lists_init(&lists, &arena, 3);
    phys_tier_list_add(&lists.tiers[PHYS_TIER_0_DIRECT], 0);
    phys_tier_list_add(&lists.tiers[PHYS_TIER_0_DIRECT], 1);
    phys_tier_list_add(&lists.tiers[PHYS_TIER_0_DIRECT], 2);

    phys_halo_closure_args_t args = {
        .bodies = bodies,
        .aabbs = aabbs,
        .grid = &grid,
        .tier_lists = &lists,
        .velocity_margin = 0.5f,
        .dt = 1.0f / 30.0f,
        .body_count = 3,
    };
    phys_stage_halo_closure(&args);

    /* T1 should remain empty since all bodies are already T0. */
    ASSERT_INT_EQ(0, (int)lists.tiers[PHYS_TIER_1_NEAR].count);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Test 3: T0 body sweeps near a T2 body.  After halo, the T2 body
 * should be promoted to T1.
 */
static int test_halo_promotes_neighbor_to_t1(void) {
    phys_body_t bodies[2];
    make_dynamic_at(&bodies[0], 1.0f, 0, 0, 0, 50, 0, 0);
    make_dynamic_at(&bodies[1], 1.0f, 3, 0, 0, 0, 0, 0);

    bodies[0].tier = PHYS_TIER_0_DIRECT;
    bodies[1].tier = PHYS_TIER_2_VISIBLE;

    phys_aabb_t aabbs[2];
    compute_sphere_aabb(&aabbs[0], &bodies[0]);
    compute_sphere_aabb(&aabbs[1], &bodies[1]);

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, TEST_ARENA_SIZE);

    phys_spatial_grid_t grid;
    phys_spatial_grid_init(&grid, TEST_GRID_CELLS, TEST_GRID_CELL_SIZE, &arena);

    phys_spatial_grid_insert(&grid, 0, &aabbs[0]);
    phys_spatial_grid_insert(&grid, 1, &aabbs[1]);

    phys_tier_lists_t lists;
    phys_tier_lists_init(&lists, &arena, 2);
    phys_tier_list_add(&lists.tiers[PHYS_TIER_0_DIRECT], 0);

    phys_halo_closure_args_t args = {
        .bodies = bodies,
        .aabbs = aabbs,
        .grid = &grid,
        .tier_lists = &lists,
        .velocity_margin = 0.1f,
        .dt = 1.0f / 30.0f,
        .body_count = 2,
    };
    phys_stage_halo_closure(&args);

    /* Body 1 (T2) should now be in T1. */
    ASSERT_INT_EQ(1, (int)lists.tiers[PHYS_TIER_1_NEAR].count);
    ASSERT_INT_EQ(1, (int)lists.tiers[PHYS_TIER_1_NEAR].indices[0]);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Test 4: Static body (inv_mass=0) near T0 should NOT be promoted.
 */
static int test_halo_static_not_promoted(void) {
    phys_body_t bodies[2];
    make_dynamic_at(&bodies[0], 1.0f, 0, 0, 0, 50, 0, 0);
    make_static_at(&bodies[1], 3, 0, 0);

    bodies[0].tier = PHYS_TIER_0_DIRECT;
    bodies[1].tier = PHYS_TIER_2_VISIBLE;

    phys_aabb_t aabbs[2];
    compute_sphere_aabb(&aabbs[0], &bodies[0]);
    compute_sphere_aabb(&aabbs[1], &bodies[1]);

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, TEST_ARENA_SIZE);

    phys_spatial_grid_t grid;
    phys_spatial_grid_init(&grid, TEST_GRID_CELLS, TEST_GRID_CELL_SIZE, &arena);

    phys_spatial_grid_insert(&grid, 0, &aabbs[0]);
    phys_spatial_grid_insert(&grid, 1, &aabbs[1]);

    phys_tier_lists_t lists;
    phys_tier_lists_init(&lists, &arena, 2);
    phys_tier_list_add(&lists.tiers[PHYS_TIER_0_DIRECT], 0);

    phys_halo_closure_args_t args = {
        .bodies = bodies,
        .aabbs = aabbs,
        .grid = &grid,
        .tier_lists = &lists,
        .velocity_margin = 0.1f,
        .dt = 1.0f / 30.0f,
        .body_count = 2,
    };
    phys_stage_halo_closure(&args);

    /* Static body should NOT be promoted to T1. */
    ASSERT_INT_EQ(0, (int)lists.tiers[PHYS_TIER_1_NEAR].count);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Test 5: Small velocity but large margin still finds neighbors.
 * Body 0 has tiny velocity but a big margin that extends the swept
 * AABB far enough to reach body 1.
 */
static int test_halo_velocity_margin_extends(void) {
    phys_body_t bodies[2];
    /* Very small velocity, won't sweep far. */
    make_dynamic_at(&bodies[0], 1.0f, 0, 0, 0, 0.1f, 0, 0);
    /* Neighbor at distance 3 — out of range without margin. */
    make_dynamic_at(&bodies[1], 1.0f, 3, 0, 0, 0, 0, 0);

    bodies[0].tier = PHYS_TIER_0_DIRECT;
    bodies[1].tier = PHYS_TIER_2_VISIBLE;

    phys_aabb_t aabbs[2];
    compute_sphere_aabb(&aabbs[0], &bodies[0]);
    compute_sphere_aabb(&aabbs[1], &bodies[1]);

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, TEST_ARENA_SIZE);

    phys_spatial_grid_t grid;
    phys_spatial_grid_init(&grid, TEST_GRID_CELLS, TEST_GRID_CELL_SIZE, &arena);

    phys_spatial_grid_insert(&grid, 0, &aabbs[0]);
    phys_spatial_grid_insert(&grid, 1, &aabbs[1]);

    phys_tier_lists_t lists;
    phys_tier_lists_init(&lists, &arena, 2);
    phys_tier_list_add(&lists.tiers[PHYS_TIER_0_DIRECT], 0);

    /* Large margin (5.0) ensures the swept AABB reaches body 1. */
    phys_halo_closure_args_t args = {
        .bodies = bodies,
        .aabbs = aabbs,
        .grid = &grid,
        .tier_lists = &lists,
        .velocity_margin = 5.0f,
        .dt = 1.0f / 30.0f,
        .body_count = 2,
    };
    phys_stage_halo_closure(&args);

    /* Body 1 should be promoted to T1 due to the large margin. */
    ASSERT_INT_EQ(1, (int)lists.tiers[PHYS_TIER_1_NEAR].count);
    ASSERT_INT_EQ(1, (int)lists.tiers[PHYS_TIER_1_NEAR].indices[0]);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Test 6: NULL args doesn't crash.
 */
static int test_halo_null_safe(void) {
    /* NULL args pointer. */
    phys_stage_halo_closure(NULL);

    /* Args with NULL fields. */
    phys_halo_closure_args_t args;
    memset(&args, 0, sizeof(args));
    phys_stage_halo_closure(&args);

    /* If we got here without a crash, the test passes. */
    return 0;
}

/* ── Runner ─────────────────────────────────────────────────────── */

int main(void) {
    int test_count = 0;
    int fail_count = 0;

    printf("p036_physics_halo_closure_tests\n");

    RUN_TEST(test_halo_swept_aabb);
    RUN_TEST(test_halo_no_promotion_if_already_t0);
    RUN_TEST(test_halo_promotes_neighbor_to_t1);
    RUN_TEST(test_halo_static_not_promoted);
    RUN_TEST(test_halo_velocity_margin_extends);
    RUN_TEST(test_halo_null_safe);

    printf("\n%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
