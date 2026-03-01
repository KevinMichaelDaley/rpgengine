/**
 * @file p088_static_bvh_raycast_tests.c
 * @brief Tests for phys_static_bvh_raycast() — BVH ray traversal.
 *
 * Tests broadphase BVH ray traversal returning candidate leaf item IDs.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/static_bvh.h"
#include "ferrum/physics/phys_pool.h"

/* ── Test harness ─────────────────────────────────────────────── */

static int g_pass = 0;
static int g_fail = 0;

#define RUN(fn)                                        \
    do {                                               \
        printf("RUN  %s\n", #fn);                      \
        fn();                                          \
        printf("OK   %s\n", #fn);                      \
    } while (0)

#define ASSERT_TRUE(expr)                                              \
    do {                                                               \
        if (!(expr)) {                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);     \
            g_fail++;                                                  \
            return;                                                    \
        }                                                              \
    } while (0)

#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))

#define PASS() g_pass++

/* ── Helpers ──────────────────────────────────────────────────── */

static phys_aabb_t make_aabb(float cx, float cy, float cz, float half) {
    phys_aabb_t a;
    a.min = (phys_vec3_t){cx - half, cy - half, cz - half};
    a.max = (phys_vec3_t){cx + half, cy + half, cz + half};
    return a;
}

/* ── Tests ────────────────────────────────────────────────────── */

/** @brief Ray hits a single leaf. */
static void test_single_leaf_hit(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 16384);

    /* One AABB centered at (0, 0, 5). */
    phys_aabb_t aabb = make_aabb(0, 0, 5, 1);
    uint32_t id = 42;

    phys_static_bvh_t bvh;
    phys_static_bvh_build(&bvh, &aabb, &id, 1, &arena);
    ASSERT_TRUE(bvh.node_count > 0);

    float origin[3] = {0, 0, 0};
    float dir[3]    = {0, 0, 1};
    uint32_t hits[4];
    uint32_t n = phys_static_bvh_raycast(&bvh, origin, dir, 100.0f, hits, 4);
    ASSERT_EQ(n, (uint32_t)1);
    ASSERT_EQ(hits[0], (uint32_t)42);

    phys_frame_arena_destroy(&arena);
    PASS();
}

/** @brief Ray misses all leaves. */
static void test_single_leaf_miss(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 16384);

    phys_aabb_t aabb = make_aabb(0, 0, 5, 1);
    uint32_t id = 1;

    phys_static_bvh_t bvh;
    phys_static_bvh_build(&bvh, &aabb, &id, 1, &arena);

    /* Ray along +X — misses the box at z=5. */
    float origin[3] = {0, 0, 0};
    float dir[3]    = {1, 0, 0};
    uint32_t hits[4];
    uint32_t n = phys_static_bvh_raycast(&bvh, origin, dir, 100.0f, hits, 4);
    ASSERT_EQ(n, (uint32_t)0);

    phys_frame_arena_destroy(&arena);
    PASS();
}

/** @brief Ray hits multiple leaves. */
static void test_multiple_leaves(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 65536);

    /* Three boxes along the Z axis. */
    phys_aabb_t aabbs[3] = {
        make_aabb(0, 0, 3, 1),
        make_aabb(0, 0, 7, 1),
        make_aabb(0, 0, 12, 1),
    };
    uint32_t ids[3] = {10, 20, 30};

    phys_static_bvh_t bvh;
    phys_static_bvh_build(&bvh, aabbs, ids, 3, &arena);

    float origin[3] = {0, 0, 0};
    float dir[3]    = {0, 0, 1};
    uint32_t hits[8];
    uint32_t n = phys_static_bvh_raycast(&bvh, origin, dir, 100.0f, hits, 8);
    ASSERT_EQ(n, (uint32_t)3);

    phys_frame_arena_destroy(&arena);
    PASS();
}

/** @brief Ray is too short to reach the leaf. */
static void test_max_distance_limit(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 16384);

    phys_aabb_t aabb = make_aabb(0, 0, 10, 1);
    uint32_t id = 1;

    phys_static_bvh_t bvh;
    phys_static_bvh_build(&bvh, &aabb, &id, 1, &arena);

    float origin[3] = {0, 0, 0};
    float dir[3]    = {0, 0, 1};
    uint32_t hits[4];

    /* max_distance = 5 — box starts at z=9, too far. */
    uint32_t n = phys_static_bvh_raycast(&bvh, origin, dir, 5.0f, hits, 4);
    ASSERT_EQ(n, (uint32_t)0);

    phys_frame_arena_destroy(&arena);
    PASS();
}

/** @brief NULL BVH returns 0. */
static void test_null_safety(void) {
    uint32_t hits[4];
    float origin[3] = {0, 0, 0};
    float dir[3]    = {0, 0, 1};

    ASSERT_EQ(phys_static_bvh_raycast(NULL, origin, dir, 10.0f, hits, 4), (uint32_t)0);

    phys_static_bvh_t empty = {0};
    ASSERT_EQ(phys_static_bvh_raycast(&empty, origin, dir, 10.0f, hits, 4), (uint32_t)0);

    PASS();
}

/** @brief Max results caps output. */
static void test_max_results_cap(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 65536);

    phys_aabb_t aabbs[4] = {
        make_aabb(0, 0, 2, 1),
        make_aabb(0, 0, 5, 1),
        make_aabb(0, 0, 8, 1),
        make_aabb(0, 0, 11, 1),
    };
    uint32_t ids[4] = {1, 2, 3, 4};

    phys_static_bvh_t bvh;
    phys_static_bvh_build(&bvh, aabbs, ids, 4, &arena);

    float origin[3] = {0, 0, 0};
    float dir[3]    = {0, 0, 1};
    uint32_t hits[2];

    /* Only 2 slots — should cap at 2. */
    uint32_t n = phys_static_bvh_raycast(&bvh, origin, dir, 100.0f, hits, 2);
    ASSERT_EQ(n, (uint32_t)2);

    phys_frame_arena_destroy(&arena);
    PASS();
}

/* ── Main ─────────────────────────────────────────────────────── */

int main(void) {
    printf("=== Static BVH Raycast Tests ===\n\n");

    RUN(test_single_leaf_hit);
    RUN(test_single_leaf_miss);
    RUN(test_multiple_leaves);
    RUN(test_max_distance_limit);
    RUN(test_null_safety);
    RUN(test_max_results_cap);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
