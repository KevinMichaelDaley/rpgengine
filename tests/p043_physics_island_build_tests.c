/**
 * @file p043_physics_island_build_tests.c
 * @brief Unit tests for Stage 10: Island Build.
 *
 * Tests cover: single island, two islands, static body breaking,
 * no constraints, chain topology, and NULL safety.
 */

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
        printf("  %-50s", #fn);                                                 \
        int _r = fn();                                                          \
        printf("%s\n", _r ? "FAIL" : "PASS");                                   \
        if (_r) fail_count++;                                                   \
        test_count++;                                                           \
    } while (0)

/* ── Helpers ────────────────────────────────────────────────────── */

/** Create a dynamic body with default state. */
static phys_body_t make_dynamic_body(void)
{
    phys_body_t b;
    phys_body_init(&b);
    b.inv_mass = 1.0f;
    b.flags = 0;
    return b;
}

/** Create a static body. */
static phys_body_t make_static_body(void)
{
    phys_body_t b;
    phys_body_init(&b);
    b.inv_mass = 0.0f;
    b.flags = PHYS_BODY_FLAG_STATIC;
    return b;
}

/** Create a minimal constraint connecting body_a to body_b. */
static phys_constraint_t make_constraint(uint32_t a, uint32_t b)
{
    phys_constraint_t c;
    memset(&c, 0, sizeof(c));
    c.body_a = a;
    c.body_b = b;
    c.row_count = 1;
    return c;
}

/**
 * Check if a specific body index appears in any island.
 * Returns the island index (0-based) or UINT32_MAX if not found.
 */
static uint32_t find_island_of_body(const phys_island_list_t *list,
                                    uint32_t body_idx)
{
    for (uint32_t i = 0; i < list->count; ++i) {
        const phys_island_t *island = &list->islands[i];
        for (uint32_t j = 0; j < island->body_count; ++j) {
            if (island->body_indices[j] == body_idx) {
                return i;
            }
        }
    }
    return UINT32_MAX;
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * 3 dynamic bodies connected by 2 constraints (0-1, 1-2).
 * All three should land in a single island.
 */
static int test_island_single_island(void)
{
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_body_t bodies[3] = {
        make_dynamic_body(),
        make_dynamic_body(),
        make_dynamic_body(),
    };

    phys_constraint_t constraints[2] = {
        make_constraint(0, 1),
        make_constraint(1, 2),
    };

    phys_island_list_t islands;
    memset(&islands, 0, sizeof(islands));

    phys_island_build_args_t args = {
        .constraints      = constraints,
        .constraint_count = 2,
        .bodies           = bodies,
        .body_count       = 3,
        .islands_out      = &islands,
        .arena            = &arena,
    };

    phys_stage_island_build(&args);

    ASSERT_UINT_EQ(1, islands.count);
    ASSERT_UINT_EQ(3, islands.islands[0].body_count);

    /* All bodies should be in island 0. */
    ASSERT_UINT_EQ(0, find_island_of_body(&islands, 0));
    ASSERT_UINT_EQ(0, find_island_of_body(&islands, 1));
    ASSERT_UINT_EQ(0, find_island_of_body(&islands, 2));

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * 4 dynamic bodies, constraints 0-1 and 2-3.
 * Should produce 2 islands of 2 bodies each.
 */
static int test_island_two_islands(void)
{
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_body_t bodies[4] = {
        make_dynamic_body(),
        make_dynamic_body(),
        make_dynamic_body(),
        make_dynamic_body(),
    };

    phys_constraint_t constraints[2] = {
        make_constraint(0, 1),
        make_constraint(2, 3),
    };

    phys_island_list_t islands;
    memset(&islands, 0, sizeof(islands));

    phys_island_build_args_t args = {
        .constraints      = constraints,
        .constraint_count = 2,
        .bodies           = bodies,
        .body_count       = 4,
        .islands_out      = &islands,
        .arena            = &arena,
    };

    phys_stage_island_build(&args);

    ASSERT_UINT_EQ(2, islands.count);

    /* Bodies 0 and 1 share one island; 2 and 3 share another. */
    uint32_t island_01 = find_island_of_body(&islands, 0);
    uint32_t island_23 = find_island_of_body(&islands, 2);
    ASSERT_TRUE(island_01 != UINT32_MAX);
    ASSERT_TRUE(island_23 != UINT32_MAX);
    ASSERT_TRUE(island_01 != island_23);
    ASSERT_UINT_EQ(island_01, find_island_of_body(&islands, 1));
    ASSERT_UINT_EQ(island_23, find_island_of_body(&islands, 3));

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Body 0 is static; constraints 0-1 and 0-2.
 * Static bodies do NOT merge islands, so bodies 1 and 2 end up in
 * separate islands.  Each island contains one dynamic body and its
 * constraint against the static body (so the solver can resolve
 * static-dynamic contacts like ground collisions).
 */
static int test_island_static_breaks(void)
{
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_body_t bodies[3] = {
        make_static_body(),
        make_dynamic_body(),
        make_dynamic_body(),
    };

    phys_constraint_t constraints[2] = {
        make_constraint(0, 1),
        make_constraint(0, 2),
    };

    phys_island_list_t islands;
    memset(&islands, 0, sizeof(islands));

    phys_island_build_args_t args = {
        .constraints      = constraints,
        .constraint_count = 2,
        .bodies           = bodies,
        .body_count       = 3,
        .islands_out      = &islands,
        .arena            = &arena,
    };

    phys_stage_island_build(&args);

    /* Two separate islands: one for body 1, one for body 2.
     * Each has 1 constraint against the static body. */
    ASSERT_UINT_EQ(2, islands.count);

    /* Bodies 1 and 2 must be in different islands. */
    uint32_t i1 = find_island_of_body(&islands, 1);
    uint32_t i2 = find_island_of_body(&islands, 2);
    ASSERT_TRUE(i1 != UINT32_MAX && i2 != UINT32_MAX && i1 != i2);

    /* Each island should have 1 constraint. */
    ASSERT_UINT_EQ(1, islands.islands[i1].constraint_count);
    ASSERT_UINT_EQ(1, islands.islands[i2].constraint_count);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * 3 dynamic bodies, 0 constraints → 0 islands
 * (phys_island_list_build only tracks bodies that appear in constraints).
 */
static int test_island_no_constraints(void)
{
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_body_t bodies[3] = {
        make_dynamic_body(),
        make_dynamic_body(),
        make_dynamic_body(),
    };

    phys_island_list_t islands;
    memset(&islands, 0, sizeof(islands));

    phys_island_build_args_t args = {
        .constraints      = NULL,
        .constraint_count = 0,
        .bodies           = bodies,
        .body_count       = 3,
        .islands_out      = &islands,
        .arena            = &arena,
    };

    phys_stage_island_build(&args);

    /* No constraints → no islands. */
    ASSERT_UINT_EQ(0, islands.count);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * 5 dynamic bodies chained: 0-1, 1-2, 2-3, 3-4 → 1 island with 5 bodies.
 */
static int test_island_chain(void)
{
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_body_t bodies[5] = {
        make_dynamic_body(),
        make_dynamic_body(),
        make_dynamic_body(),
        make_dynamic_body(),
        make_dynamic_body(),
    };

    phys_constraint_t constraints[4] = {
        make_constraint(0, 1),
        make_constraint(1, 2),
        make_constraint(2, 3),
        make_constraint(3, 4),
    };

    phys_island_list_t islands;
    memset(&islands, 0, sizeof(islands));

    phys_island_build_args_t args = {
        .constraints      = constraints,
        .constraint_count = 4,
        .bodies           = bodies,
        .body_count       = 5,
        .islands_out      = &islands,
        .arena            = &arena,
    };

    phys_stage_island_build(&args);

    ASSERT_UINT_EQ(1, islands.count);
    ASSERT_UINT_EQ(5, islands.islands[0].body_count);

    /* All bodies in the same island. */
    uint32_t island_0 = find_island_of_body(&islands, 0);
    ASSERT_TRUE(island_0 != UINT32_MAX);
    for (uint32_t i = 1; i < 5; ++i) {
        ASSERT_UINT_EQ(island_0, find_island_of_body(&islands, i));
    }

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * NULL args must not crash.
 */
static int test_island_null_safe(void)
{
    /* NULL args pointer. */
    phys_stage_island_build(NULL);

    /* NULL fields in args. */
    phys_island_build_args_t args;
    memset(&args, 0, sizeof(args));
    phys_stage_island_build(&args);

    /* NULL bodies. */
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 4096);
    phys_island_list_t islands;
    memset(&islands, 0, sizeof(islands));

    args.arena = &arena;
    args.islands_out = &islands;
    args.bodies = NULL;
    args.body_count = 3;
    phys_stage_island_build(&args);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void)
{
    int test_count = 0;
    int fail_count = 0;

    printf("p043_physics_island_build_tests\n");
    RUN_TEST(test_island_single_island);
    RUN_TEST(test_island_two_islands);
    RUN_TEST(test_island_static_breaks);
    RUN_TEST(test_island_no_constraints);
    RUN_TEST(test_island_chain);
    RUN_TEST(test_island_null_safe);

    printf("\n%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
