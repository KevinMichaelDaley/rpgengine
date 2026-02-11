/**
 * @file p098_island_split_tests.c
 * @brief Tests for max-island-size splitting in the island build stage.
 *
 * Validates that when max_island_bodies is set, oversized islands are
 * split at weak edges (both bodies near-resting) while tightly-coupled
 * groups (at least one body with significant velocity) remain merged.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/island.h"
#include "ferrum/physics/island_build.h"
#include "ferrum/physics/phys_pool.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n",                 \
                    __FILE__, __LINE__, #cond);                                 \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define ASSERT_UINT_EQ(exp, act)                                               \
    do {                                                                        \
        unsigned _e = (unsigned)(exp), _a = (unsigned)(act);                    \
        if (_e != _a) {                                                         \
            fprintf(stderr, "ASSERT_UINT_EQ failed: %s:%d: "                   \
                    "expected %u got %u\n",                                     \
                    __FILE__, __LINE__, _e, _a);                                \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define RUN_TEST(fn)                                                           \
    do {                                                                        \
        printf("  %-60s", #fn);                                                 \
        int _r = fn();                                                          \
        printf("%s\n", _r ? "FAIL" : "PASS");                                   \
        if (_r) fail_count++;                                                   \
        test_count++;                                                           \
    } while (0)

/* ── Helpers ────────────────────────────────────────────────────── */

static phys_body_t make_dynamic_body(float speed)
{
    phys_body_t b;
    phys_body_init(&b);
    b.inv_mass = 1.0f;
    b.flags = 0;
    b.linear_vel = (phys_vec3_t){0.0f, speed, 0.0f};
    return b;
}

static phys_body_t make_static_body(void)
{
    phys_body_t b;
    phys_body_init(&b);
    b.inv_mass = 0.0f;
    b.flags = PHYS_BODY_FLAG_STATIC;
    return b;
}

static phys_constraint_t make_constraint(uint32_t a, uint32_t b)
{
    phys_constraint_t c;
    memset(&c, 0, sizeof(c));
    c.body_a = a;
    c.body_b = b;
    c.row_count = 1;
    return c;
}

/** Find which island a body belongs to (UINT32_MAX if not found). */
static uint32_t find_island_of(const phys_island_list_t *list, uint32_t body)
{
    for (uint32_t i = 0; i < list->count; ++i) {
        for (uint32_t j = 0; j < list->islands[i].body_count; ++j) {
            if (list->islands[i].body_indices[j] == body) return i;
        }
    }
    return UINT32_MAX;
}

/** Check that no island exceeds the max body count. */
static int all_islands_within_cap(const phys_island_list_t *list,
                                   uint32_t cap)
{
    for (uint32_t i = 0; i < list->count; ++i) {
        if (list->islands[i].body_count > cap) return 0;
    }
    return 1;
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * max_island_bodies=0 (disabled): a chain of 8 resting bodies
 * should still merge into 1 island (backward compatible).
 */
static int test_split_disabled_preserves_single_island(void)
{
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 128 * 1024);

    enum { N = 8 };
    phys_body_t bodies[N];
    for (int i = 0; i < N; ++i) bodies[i] = make_dynamic_body(0.0f);

    phys_constraint_t cons[N - 1];
    for (int i = 0; i < N - 1; ++i) cons[i] = make_constraint(i, i + 1);

    phys_island_list_t islands;
    memset(&islands, 0, sizeof(islands));

    phys_stage_island_build(&(phys_island_build_args_t){
        .constraints      = cons,
        .constraint_count = N - 1,
        .bodies           = bodies,
        .body_count       = N,
        .islands_out      = &islands,
        .arena            = &arena,
        .max_island_bodies = 0, /* disabled */
    });

    ASSERT_UINT_EQ(1, islands.count);
    ASSERT_UINT_EQ(N, islands.islands[0].body_count);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Chain of 8 resting bodies (speed=0), max_island_bodies=4.
 * All contacts are weak → should split into islands of ≤4 bodies.
 */
static int test_split_chain_resting_bodies(void)
{
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 128 * 1024);

    enum { N = 8 };
    phys_body_t bodies[N];
    for (int i = 0; i < N; ++i) bodies[i] = make_dynamic_body(0.0f);

    phys_constraint_t cons[N - 1];
    for (int i = 0; i < N - 1; ++i) cons[i] = make_constraint(i, i + 1);

    phys_island_list_t islands;
    memset(&islands, 0, sizeof(islands));

    phys_stage_island_build(&(phys_island_build_args_t){
        .constraints      = cons,
        .constraint_count = N - 1,
        .bodies           = bodies,
        .body_count       = N,
        .islands_out      = &islands,
        .arena            = &arena,
        .max_island_bodies = 4,
    });

    ASSERT_TRUE(islands.count >= 2);
    ASSERT_TRUE(all_islands_within_cap(&islands, 4));

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Chain of 8 bodies, ALL moving fast.  max_island_bodies=4.
 * All contacts are strong (at least one body has high velocity).
 * Strong edges are merged first, so we get 1 large island because
 * every edge is strong.  The second pass enforces the cap by
 * refusing further merges, so the result should have islands ≤ 4.
 */
static int test_split_chain_all_active(void)
{
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 128 * 1024);

    enum { N = 8 };
    phys_body_t bodies[N];
    for (int i = 0; i < N; ++i) bodies[i] = make_dynamic_body(5.0f);

    phys_constraint_t cons[N - 1];
    for (int i = 0; i < N - 1; ++i) cons[i] = make_constraint(i, i + 1);

    phys_island_list_t islands;
    memset(&islands, 0, sizeof(islands));

    phys_stage_island_build(&(phys_island_build_args_t){
        .constraints      = cons,
        .constraint_count = N - 1,
        .bodies           = bodies,
        .body_count       = N,
        .islands_out      = &islands,
        .arena            = &arena,
        .max_island_bodies = 4,
    });

    /* Even with all strong edges, the cap should be enforced. */
    ASSERT_TRUE(all_islands_within_cap(&islands, 4));

    /* Total body count across islands must still equal N. */
    uint32_t total = 0;
    for (uint32_t i = 0; i < islands.count; ++i) {
        total += islands.islands[i].body_count;
    }
    ASSERT_UINT_EQ(N, total);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Mixed scenario: 4 resting bodies (0-3) chained, then body 4 is
 * fast and connected to body 3.  max_island_bodies=3.
 *
 * Strong edge: 3-4 (body 4 is fast).
 * Weak edges: 0-1, 1-2, 2-3 (all resting).
 *
 * Expected: body 3 and 4 must share an island (strong coupling).
 * The resting chain should break so no island exceeds 3 bodies.
 */
static int test_split_mixed_active_resting(void)
{
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 128 * 1024);

    phys_body_t bodies[5] = {
        make_dynamic_body(0.0f),  /* 0: resting */
        make_dynamic_body(0.0f),  /* 1: resting */
        make_dynamic_body(0.0f),  /* 2: resting */
        make_dynamic_body(0.0f),  /* 3: resting */
        make_dynamic_body(5.0f),  /* 4: active  */
    };

    phys_constraint_t cons[4] = {
        make_constraint(0, 1),
        make_constraint(1, 2),
        make_constraint(2, 3),
        make_constraint(3, 4),
    };

    phys_island_list_t islands;
    memset(&islands, 0, sizeof(islands));

    phys_stage_island_build(&(phys_island_build_args_t){
        .constraints      = cons,
        .constraint_count = 4,
        .bodies           = bodies,
        .body_count       = 5,
        .islands_out      = &islands,
        .arena            = &arena,
        .max_island_bodies = 3,
    });

    ASSERT_TRUE(all_islands_within_cap(&islands, 3));

    /* Bodies 3 and 4 must be in the same island (strong edge). */
    uint32_t i3 = find_island_of(&islands, 3);
    uint32_t i4 = find_island_of(&islands, 4);
    ASSERT_TRUE(i3 != UINT32_MAX);
    ASSERT_UINT_EQ(i3, i4);

    /* Every body must appear in exactly one island. */
    for (uint32_t b = 0; b < 5; ++b) {
        ASSERT_TRUE(find_island_of(&islands, b) != UINT32_MAX);
    }

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Large pile: 20 resting bodies in a grid of constraints (each body
 * connected to its neighbors).  max_island_bodies=8.
 *
 * All bodies are resting → all edges are weak.
 * Result: multiple islands, each ≤ 8 bodies.
 * All constraints must be assigned to some island.
 */
static int test_split_large_resting_pile(void)
{
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 256 * 1024);

    enum { N = 20 };
    phys_body_t bodies[N];
    for (int i = 0; i < N; ++i) bodies[i] = make_dynamic_body(0.0f);

    /* Chain plus cross-links: i-(i+1) and i-(i+2) where valid. */
    enum { MAX_CONS = 2 * N };
    phys_constraint_t cons[MAX_CONS];
    uint32_t nc = 0;
    for (int i = 0; i < N - 1; ++i) {
        cons[nc++] = make_constraint(i, i + 1);
    }
    for (int i = 0; i < N - 2; ++i) {
        cons[nc++] = make_constraint(i, i + 2);
    }

    phys_island_list_t islands;
    memset(&islands, 0, sizeof(islands));

    phys_stage_island_build(&(phys_island_build_args_t){
        .constraints      = cons,
        .constraint_count = nc,
        .bodies           = bodies,
        .body_count       = N,
        .islands_out      = &islands,
        .arena            = &arena,
        .max_island_bodies = 8,
    });

    ASSERT_TRUE(islands.count >= 3);  /* 20 bodies / 8 max ≥ 3 islands */
    ASSERT_TRUE(all_islands_within_cap(&islands, 8));

    /* Every body must appear in exactly one island. */
    for (uint32_t b = 0; b < N; ++b) {
        ASSERT_TRUE(find_island_of(&islands, b) != UINT32_MAX);
    }

    /* Total constraint count across islands must equal original count. */
    uint32_t total_cons = 0;
    for (uint32_t i = 0; i < islands.count; ++i) {
        total_cons += islands.islands[i].constraint_count;
    }
    ASSERT_UINT_EQ(nc, total_cons);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Already-small island: 3 bodies, max_island_bodies=4.
 * Should produce exactly 1 island (no unnecessary splitting).
 */
static int test_split_small_island_unchanged(void)
{
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_body_t bodies[3] = {
        make_dynamic_body(0.0f),
        make_dynamic_body(0.0f),
        make_dynamic_body(0.0f),
    };

    phys_constraint_t cons[2] = {
        make_constraint(0, 1),
        make_constraint(1, 2),
    };

    phys_island_list_t islands;
    memset(&islands, 0, sizeof(islands));

    phys_stage_island_build(&(phys_island_build_args_t){
        .constraints      = cons,
        .constraint_count = 2,
        .bodies           = bodies,
        .body_count       = 3,
        .islands_out      = &islands,
        .arena            = &arena,
        .max_island_bodies = 4,
    });

    ASSERT_UINT_EQ(1, islands.count);
    ASSERT_UINT_EQ(3, islands.islands[0].body_count);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Static bodies should still break islands regardless of splitting.
 * 2 dynamic bodies connected through a static body, max_island_bodies=4.
 */
static int test_split_static_still_breaks(void)
{
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_body_t bodies[3] = {
        make_static_body(),
        make_dynamic_body(5.0f),
        make_dynamic_body(5.0f),
    };

    phys_constraint_t cons[2] = {
        make_constraint(0, 1),
        make_constraint(0, 2),
    };

    phys_island_list_t islands;
    memset(&islands, 0, sizeof(islands));

    phys_stage_island_build(&(phys_island_build_args_t){
        .constraints      = cons,
        .constraint_count = 2,
        .bodies           = bodies,
        .body_count       = 3,
        .islands_out      = &islands,
        .arena            = &arena,
        .max_island_bodies = 4,
    });

    ASSERT_UINT_EQ(2, islands.count);
    uint32_t i1 = find_island_of(&islands, 1);
    uint32_t i2 = find_island_of(&islands, 2);
    ASSERT_TRUE(i1 != i2);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void)
{
    int test_count = 0;
    int fail_count = 0;

    printf("p098_island_split_tests\n");
    RUN_TEST(test_split_disabled_preserves_single_island);
    RUN_TEST(test_split_chain_resting_bodies);
    RUN_TEST(test_split_chain_all_active);
    RUN_TEST(test_split_mixed_active_resting);
    RUN_TEST(test_split_large_resting_pile);
    RUN_TEST(test_split_small_island_unchanged);
    RUN_TEST(test_split_static_still_breaks);

    printf("\n%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
