/**
 * @file p038_physics_broadphase_tests.c
 * @brief Unit tests for Stage 5: Broadphase.
 *
 * Tests cover: overlapping pair detection, no-overlap, duplicate
 * avoidance, self-pair exclusion, sleeping body handling,
 * static-static exclusion, and NULL safety.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/aabb.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/broadphase.h"
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

/* ── Constants ──────────────────────────────────────────────────── */

/** Arena size large enough for any test (1 MiB). */
#define TEST_ARENA_SIZE (1024u * 1024u)

/** Grid cell count (power of 2). */
#define TEST_GRID_CELLS 64u

/** Grid cell size (world units). */
#define TEST_GRID_CELL_SIZE 4.0f

/** Maximum output pairs for tests. */
#define TEST_MAX_PAIRS 64u

/* ── Helpers ────────────────────────────────────────────────────── */

/**
 * @brief Initialize a dynamic body at a given position.
 */
static void make_dynamic_at(phys_body_t *body, float mass,
                            float px, float py, float pz) {
    phys_body_init(body);
    phys_body_set_mass(body, mass);
    body->position = (phys_vec3_t){px, py, pz};
}

/**
 * @brief Initialize a static body at a given position (inv_mass = 0).
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
 * Test 1: Two sphere bodies with overlapping AABBs → 1 pair.
 * Bodies at (0,0,0) and (1.5,0,0) with radius 1 overlap.
 */
static int test_broadphase_finds_overlapping(void) {
    phys_body_t bodies[2];
    make_dynamic_at(&bodies[0], 1.0f, 0, 0, 0);
    make_dynamic_at(&bodies[1], 1.0f, 1.5f, 0, 0);
    bodies[0].tier = PHYS_TIER_0_DIRECT;
    bodies[1].tier = PHYS_TIER_0_DIRECT;

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
    phys_tier_list_add(&lists.tiers[PHYS_TIER_0_DIRECT], 1);

    phys_collision_pair_t pairs[TEST_MAX_PAIRS];
    uint32_t pair_count = 0;

    phys_broadphase_args_t args = {
        .bodies = bodies,
        .aabbs = aabbs,
        .grid = &grid,
        .tier_lists = &lists,
        .pairs_out = pairs,
        .max_pairs = TEST_MAX_PAIRS,
        .pair_count_out = &pair_count,
    };
    phys_stage_broadphase(&args);

    ASSERT_INT_EQ(1, (int)pair_count);
    ASSERT_INT_EQ(0, (int)pairs[0].body_a);
    ASSERT_INT_EQ(1, (int)pairs[0].body_b);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Test 2: Two bodies far apart → 0 pairs.
 */
static int test_broadphase_no_overlap(void) {
    phys_body_t bodies[2];
    make_dynamic_at(&bodies[0], 1.0f, 0, 0, 0);
    make_dynamic_at(&bodies[1], 1.0f, 100, 0, 0);
    bodies[0].tier = PHYS_TIER_0_DIRECT;
    bodies[1].tier = PHYS_TIER_0_DIRECT;

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
    phys_tier_list_add(&lists.tiers[PHYS_TIER_0_DIRECT], 1);

    phys_collision_pair_t pairs[TEST_MAX_PAIRS];
    uint32_t pair_count = 0;

    phys_broadphase_args_t args = {
        .bodies = bodies,
        .aabbs = aabbs,
        .grid = &grid,
        .tier_lists = &lists,
        .pairs_out = pairs,
        .max_pairs = TEST_MAX_PAIRS,
        .pair_count_out = &pair_count,
    };
    phys_stage_broadphase(&args);

    ASSERT_INT_EQ(0, (int)pair_count);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Test 3: Three mutually overlapping bodies → exactly 3 pairs.
 * Bodies at (0,0,0), (1,0,0), (0.5,0.5,0) with radius 1 all overlap.
 */
static int test_broadphase_no_duplicates(void) {
    phys_body_t bodies[3];
    make_dynamic_at(&bodies[0], 1.0f, 0, 0, 0);
    make_dynamic_at(&bodies[1], 1.0f, 1.0f, 0, 0);
    make_dynamic_at(&bodies[2], 1.0f, 0.5f, 0.5f, 0);
    bodies[0].tier = PHYS_TIER_0_DIRECT;
    bodies[1].tier = PHYS_TIER_1_NEAR;
    bodies[2].tier = PHYS_TIER_2_VISIBLE;

    phys_aabb_t aabbs[3];
    compute_sphere_aabb(&aabbs[0], &bodies[0]);
    compute_sphere_aabb(&aabbs[1], &bodies[1]);
    compute_sphere_aabb(&aabbs[2], &bodies[2]);

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, TEST_ARENA_SIZE);

    phys_spatial_grid_t grid;
    phys_spatial_grid_init(&grid, TEST_GRID_CELLS, TEST_GRID_CELL_SIZE, &arena);
    phys_spatial_grid_insert(&grid, 0, &aabbs[0]);
    phys_spatial_grid_insert(&grid, 1, &aabbs[1]);
    phys_spatial_grid_insert(&grid, 2, &aabbs[2]);

    phys_tier_lists_t lists;
    phys_tier_lists_init(&lists, &arena, 3);
    phys_tier_list_add(&lists.tiers[PHYS_TIER_0_DIRECT], 0);
    phys_tier_list_add(&lists.tiers[PHYS_TIER_1_NEAR], 1);
    phys_tier_list_add(&lists.tiers[PHYS_TIER_2_VISIBLE], 2);

    phys_collision_pair_t pairs[TEST_MAX_PAIRS];
    uint32_t pair_count = 0;

    phys_broadphase_args_t args = {
        .bodies = bodies,
        .aabbs = aabbs,
        .grid = &grid,
        .tier_lists = &lists,
        .pairs_out = pairs,
        .max_pairs = TEST_MAX_PAIRS,
        .pair_count_out = &pair_count,
    };
    phys_stage_broadphase(&args);

    ASSERT_INT_EQ(3, (int)pair_count);

    /* Verify all 3 expected pairs are present (order may vary). */
    int found_01 = 0, found_02 = 0, found_12 = 0;
    for (uint32_t i = 0; i < pair_count; ++i) {
        if (pairs[i].body_a == 0 && pairs[i].body_b == 1) found_01 = 1;
        if (pairs[i].body_a == 0 && pairs[i].body_b == 2) found_02 = 1;
        if (pairs[i].body_a == 1 && pairs[i].body_b == 2) found_12 = 1;
    }
    ASSERT_TRUE(found_01);
    ASSERT_TRUE(found_02);
    ASSERT_TRUE(found_12);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Test 4: Single body → 0 pairs (no self-pairs).
 */
static int test_broadphase_no_self_pairs(void) {
    phys_body_t bodies[1];
    make_dynamic_at(&bodies[0], 1.0f, 0, 0, 0);
    bodies[0].tier = PHYS_TIER_0_DIRECT;

    phys_aabb_t aabbs[1];
    compute_sphere_aabb(&aabbs[0], &bodies[0]);

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, TEST_ARENA_SIZE);

    phys_spatial_grid_t grid;
    phys_spatial_grid_init(&grid, TEST_GRID_CELLS, TEST_GRID_CELL_SIZE, &arena);
    phys_spatial_grid_insert(&grid, 0, &aabbs[0]);

    phys_tier_lists_t lists;
    phys_tier_lists_init(&lists, &arena, 1);
    phys_tier_list_add(&lists.tiers[PHYS_TIER_0_DIRECT], 0);

    phys_collision_pair_t pairs[TEST_MAX_PAIRS];
    uint32_t pair_count = 0;

    phys_broadphase_args_t args = {
        .bodies = bodies,
        .aabbs = aabbs,
        .grid = &grid,
        .tier_lists = &lists,
        .pairs_out = pairs,
        .max_pairs = TEST_MAX_PAIRS,
        .pair_count_out = &pair_count,
    };
    phys_stage_broadphase(&args);

    ASSERT_INT_EQ(0, (int)pair_count);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Test 5: T5 (sleeping) body overlapping a T0 body.
 * Only active tiers are iterated.  The T5 body is in the grid but
 * not in an active tier list, so the T0 body finds it as a candidate
 * via grid query.  This should produce 1 pair (the T0 body iterates
 * and finds the sleeping body as candidate with body_a < body_b).
 */
static int test_broadphase_skips_sleeping(void) {
    phys_body_t bodies[2];
    make_dynamic_at(&bodies[0], 1.0f, 0, 0, 0);
    make_dynamic_at(&bodies[1], 1.0f, 1.5f, 0, 0);
    bodies[0].tier = PHYS_TIER_0_DIRECT;
    bodies[1].tier = PHYS_TIER_5_SLEEPING;
    phys_body_set_sleeping(&bodies[1], true);

    phys_aabb_t aabbs[2];
    compute_sphere_aabb(&aabbs[0], &bodies[0]);
    compute_sphere_aabb(&aabbs[1], &bodies[1]);

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, TEST_ARENA_SIZE);

    phys_spatial_grid_t grid;
    phys_spatial_grid_init(&grid, TEST_GRID_CELLS, TEST_GRID_CELL_SIZE, &arena);
    phys_spatial_grid_insert(&grid, 0, &aabbs[0]);
    phys_spatial_grid_insert(&grid, 1, &aabbs[1]);

    /* Only body 0 in active tier list; body 1 in T5 (not iterated). */
    phys_tier_lists_t lists;
    phys_tier_lists_init(&lists, &arena, 2);
    phys_tier_list_add(&lists.tiers[PHYS_TIER_0_DIRECT], 0);
    phys_tier_list_add(&lists.tiers[PHYS_TIER_5_SLEEPING], 1);

    phys_collision_pair_t pairs[TEST_MAX_PAIRS];
    uint32_t pair_count = 0;

    phys_broadphase_args_t args = {
        .bodies = bodies,
        .aabbs = aabbs,
        .grid = &grid,
        .tier_lists = &lists,
        .pairs_out = pairs,
        .max_pairs = TEST_MAX_PAIRS,
        .pair_count_out = &pair_count,
    };
    phys_stage_broadphase(&args);

    /*
     * Body 0 (active, T0) queries grid and finds body 1 as candidate.
     * body_a=0 < body_b=1, AABBs overlap, and body 1 is dynamic (not
     * static-static).  So we expect 1 pair.
     */
    ASSERT_INT_EQ(1, (int)pair_count);
    ASSERT_INT_EQ(0, (int)pairs[0].body_a);
    ASSERT_INT_EQ(1, (int)pairs[0].body_b);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Test 6: Two static bodies (inv_mass=0) overlapping → 0 pairs.
 */
static int test_broadphase_static_static_excluded(void) {
    phys_body_t bodies[2];
    make_static_at(&bodies[0], 0, 0, 0);
    make_static_at(&bodies[1], 1.5f, 0, 0);
    bodies[0].tier = PHYS_TIER_4_BACKGROUND;
    bodies[1].tier = PHYS_TIER_4_BACKGROUND;

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
    phys_tier_list_add(&lists.tiers[PHYS_TIER_4_BACKGROUND], 0);
    phys_tier_list_add(&lists.tiers[PHYS_TIER_4_BACKGROUND], 1);

    phys_collision_pair_t pairs[TEST_MAX_PAIRS];
    uint32_t pair_count = 0;

    phys_broadphase_args_t args = {
        .bodies = bodies,
        .aabbs = aabbs,
        .grid = &grid,
        .tier_lists = &lists,
        .pairs_out = pairs,
        .max_pairs = TEST_MAX_PAIRS,
        .pair_count_out = &pair_count,
    };
    phys_stage_broadphase(&args);

    ASSERT_INT_EQ(0, (int)pair_count);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Test 7: NULL args doesn't crash.
 */
static int test_broadphase_null_safe(void) {
    /* NULL args pointer. */
    phys_stage_broadphase(NULL);

    /* Args with NULL fields. */
    phys_broadphase_args_t args;
    memset(&args, 0, sizeof(args));
    phys_stage_broadphase(&args);

    /* If we got here without a crash, the test passes. */
    return 0;
}

/* ── Runner ─────────────────────────────────────────────────────── */

int main(void) {
    int test_count = 0;
    int fail_count = 0;

    printf("p038_physics_broadphase_tests\n");

    RUN_TEST(test_broadphase_finds_overlapping);
    RUN_TEST(test_broadphase_no_overlap);
    RUN_TEST(test_broadphase_no_duplicates);
    RUN_TEST(test_broadphase_no_self_pairs);
    RUN_TEST(test_broadphase_skips_sleeping);
    RUN_TEST(test_broadphase_static_static_excluded);
    RUN_TEST(test_broadphase_null_safe);

    printf("\n%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
