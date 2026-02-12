/**
 * @file p101_physics_joint_island_tests.c
 * @brief Tests for phys-803: Joint-connected bodies share an island.
 *
 * Verifies that:
 * 1. Joint constraints cause connected bodies to merge into one island
 * 2. Mixed contact + joint constraints produce correct islands
 * 3. Joint constraints are included in island constraint lists
 * 4. Joint + static body does not merge static into island
 * 5. Capped splitting does not fragment joint-connected bodies
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

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
        printf("  %-55s", #fn);                                                 \
        int _r = fn();                                                          \
        printf("%s\n", _r ? "FAIL" : "PASS");                                   \
        if (_r) fail_count++;                                                   \
        test_count++;                                                           \
    } while (0)

/* ── Helpers ────────────────────────────────────────────────────── */

static phys_body_t make_dynamic_body(void)
{
    phys_body_t b;
    phys_body_init(&b);
    b.inv_mass = 1.0f;
    b.flags = 0;
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

/** Create a contact constraint (is_joint = 0). */
static phys_constraint_t make_contact(uint32_t a, uint32_t b)
{
    phys_constraint_t c;
    memset(&c, 0, sizeof(c));
    c.body_a = a;
    c.body_b = b;
    c.row_count = 1;
    c.is_joint = 0;
    return c;
}

/** Create a joint constraint (is_joint = 1). */
static phys_constraint_t make_joint_constraint(uint32_t a, uint32_t b,
                                                uint8_t rows)
{
    phys_constraint_t c;
    memset(&c, 0, sizeof(c));
    c.body_a = a;
    c.body_b = b;
    c.row_count = rows;
    c.is_joint = 1;
    c.manifold_idx = UINT32_MAX;
    return c;
}

/**
 * Find which island a body belongs to.
 * Returns island index or UINT32_MAX if not found.
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

/**
 * Check if a specific constraint index is in an island.
 */
static bool island_has_constraint(const phys_island_list_t *list,
                                  uint32_t island_idx,
                                  uint32_t constraint_idx)
{
    if (island_idx >= list->count) return false;
    const phys_island_t *island = &list->islands[island_idx];
    for (uint32_t j = 0; j < island->constraint_count; ++j) {
        if (island->constraint_indices[j] == constraint_idx) {
            return true;
        }
    }
    return false;
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * Two dynamic bodies connected by a single joint constraint.
 * They must end up in the same island.
 */
static int test_joint_merges_two_bodies(void)
{
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_body_t bodies[2] = {
        make_dynamic_body(),
        make_dynamic_body(),
    };

    phys_constraint_t constraints[1] = {
        make_joint_constraint(0, 1, 3), /* ball joint */
    };

    phys_island_list_t islands;
    memset(&islands, 0, sizeof(islands));

    phys_stage_island_build(&(phys_island_build_args_t){
        .constraints      = constraints,
        .constraint_count = 1,
        .bodies           = bodies,
        .body_count       = 2,
        .islands_out      = &islands,
        .arena            = &arena,
    });

    /* Both bodies in the same island. */
    uint32_t i0 = find_island_of_body(&islands, 0);
    uint32_t i1 = find_island_of_body(&islands, 1);
    ASSERT_TRUE(i0 != UINT32_MAX);
    ASSERT_TRUE(i0 == i1);
    ASSERT_UINT_EQ(1, islands.count);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Three bodies: 0-1 contact, 1-2 joint.
 * All three should be in one island.
 */
static int test_mixed_contact_and_joint(void)
{
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_body_t bodies[3] = {
        make_dynamic_body(),
        make_dynamic_body(),
        make_dynamic_body(),
    };

    phys_constraint_t constraints[2] = {
        make_contact(0, 1),
        make_joint_constraint(1, 2, 3),
    };

    phys_island_list_t islands;
    memset(&islands, 0, sizeof(islands));

    phys_stage_island_build(&(phys_island_build_args_t){
        .constraints      = constraints,
        .constraint_count = 2,
        .bodies           = bodies,
        .body_count       = 3,
        .islands_out      = &islands,
        .arena            = &arena,
    });

    uint32_t i0 = find_island_of_body(&islands, 0);
    uint32_t i1 = find_island_of_body(&islands, 1);
    uint32_t i2 = find_island_of_body(&islands, 2);
    ASSERT_TRUE(i0 != UINT32_MAX);
    ASSERT_TRUE(i0 == i1);
    ASSERT_TRUE(i1 == i2);
    ASSERT_UINT_EQ(1, islands.count);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Joint constraint references in island constraint list.
 * The joint constraint index must appear in the island.
 */
static int test_joint_constraint_in_island(void)
{
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_body_t bodies[2] = {
        make_dynamic_body(),
        make_dynamic_body(),
    };

    phys_constraint_t constraints[1] = {
        make_joint_constraint(0, 1, 1), /* distance joint */
    };

    phys_island_list_t islands;
    memset(&islands, 0, sizeof(islands));

    phys_stage_island_build(&(phys_island_build_args_t){
        .constraints      = constraints,
        .constraint_count = 1,
        .bodies           = bodies,
        .body_count       = 2,
        .islands_out      = &islands,
        .arena            = &arena,
    });

    ASSERT_UINT_EQ(1, islands.count);
    /* Constraint index 0 must be in the island. */
    ASSERT_TRUE(island_has_constraint(&islands, 0, 0));

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Joint between dynamic and static body.
 * Static body must NOT be pulled into the island.
 * The dynamic body should still be in an island with the constraint.
 */
static int test_joint_with_static_body(void)
{
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_body_t bodies[2] = {
        make_dynamic_body(),
        make_static_body(),
    };

    phys_constraint_t constraints[1] = {
        make_joint_constraint(0, 1, 3),
    };

    phys_island_list_t islands;
    memset(&islands, 0, sizeof(islands));

    phys_stage_island_build(&(phys_island_build_args_t){
        .constraints      = constraints,
        .constraint_count = 1,
        .bodies           = bodies,
        .body_count       = 2,
        .islands_out      = &islands,
        .arena            = &arena,
    });

    /* Dynamic body 0 is in an island. */
    uint32_t i0 = find_island_of_body(&islands, 0);
    ASSERT_TRUE(i0 != UINT32_MAX);

    /* Static body 1 is NOT in any island. */
    uint32_t i1 = find_island_of_body(&islands, 1);
    ASSERT_TRUE(i1 == UINT32_MAX);

    /* The joint constraint must still be in body 0's island. */
    ASSERT_TRUE(island_has_constraint(&islands, i0, 0));

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Two separate pairs: (0,1) joint, (2,3) contact.
 * Should produce two independent islands.
 */
static int test_two_independent_islands(void)
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
        make_joint_constraint(0, 1, 3),
        make_contact(2, 3),
    };

    phys_island_list_t islands;
    memset(&islands, 0, sizeof(islands));

    phys_stage_island_build(&(phys_island_build_args_t){
        .constraints      = constraints,
        .constraint_count = 2,
        .bodies           = bodies,
        .body_count       = 4,
        .islands_out      = &islands,
        .arena            = &arena,
    });

    ASSERT_UINT_EQ(2, islands.count);

    uint32_t i0 = find_island_of_body(&islands, 0);
    uint32_t i1 = find_island_of_body(&islands, 1);
    uint32_t i2 = find_island_of_body(&islands, 2);
    uint32_t i3 = find_island_of_body(&islands, 3);

    /* Joint pair in same island. */
    ASSERT_TRUE(i0 == i1);
    /* Contact pair in same island. */
    ASSERT_TRUE(i2 == i3);
    /* Two different islands. */
    ASSERT_TRUE(i0 != i2);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Hinge joint produces 2 constraints (3+2 rows).
 * Both must appear in the same island.
 */
static int test_hinge_two_constraints_same_island(void)
{
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_body_t bodies[2] = {
        make_dynamic_body(),
        make_dynamic_body(),
    };

    /* Simulate hinge: 2 joint constraints between same pair. */
    phys_constraint_t constraints[2] = {
        make_joint_constraint(0, 1, 3), /* positional rows */
        make_joint_constraint(0, 1, 2), /* angular rows */
    };

    phys_island_list_t islands;
    memset(&islands, 0, sizeof(islands));

    phys_stage_island_build(&(phys_island_build_args_t){
        .constraints      = constraints,
        .constraint_count = 2,
        .bodies           = bodies,
        .body_count       = 2,
        .islands_out      = &islands,
        .arena            = &arena,
    });

    ASSERT_UINT_EQ(1, islands.count);
    /* Both constraint indices in the island. */
    ASSERT_TRUE(island_has_constraint(&islands, 0, 0));
    ASSERT_TRUE(island_has_constraint(&islands, 0, 1));

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Joint chain: 0-1, 1-2, 2-3 all via joints.
 * All four bodies in one island.
 */
static int test_joint_chain(void)
{
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_body_t bodies[4] = {
        make_dynamic_body(),
        make_dynamic_body(),
        make_dynamic_body(),
        make_dynamic_body(),
    };

    phys_constraint_t constraints[3] = {
        make_joint_constraint(0, 1, 1),
        make_joint_constraint(1, 2, 1),
        make_joint_constraint(2, 3, 1),
    };

    phys_island_list_t islands;
    memset(&islands, 0, sizeof(islands));

    phys_stage_island_build(&(phys_island_build_args_t){
        .constraints      = constraints,
        .constraint_count = 3,
        .bodies           = bodies,
        .body_count       = 4,
        .islands_out      = &islands,
        .arena            = &arena,
    });

    ASSERT_UINT_EQ(1, islands.count);
    ASSERT_TRUE(find_island_of_body(&islands, 0) ==
                find_island_of_body(&islands, 3));

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Island splitting with cap: joint-connected resting bodies must NOT
 * be fragmented even when they exceed the cap.  Joints are structural
 * and must always be merged.
 *
 * Setup: 3 bodies all resting (speed=0), cap=2.
 * Contact: 0-1 (merged first in pass 2, sizes now 2).
 * Joint:  1-2 (merged size would be 3, exceeds cap of 2).
 * Without special handling, body 1 and 2 would be split by the cap.
 * Joint must force merge regardless.
 */
static int test_split_cap_respects_joints(void)
{
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_body_t bodies[3] = {
        make_dynamic_body(),
        make_dynamic_body(),
        make_dynamic_body(),
    };
    /* All bodies at rest. */
    for (int i = 0; i < 3; ++i) {
        bodies[i].linear_vel = (phys_vec3_t){0, 0, 0};
    }

    phys_constraint_t constraints[2] = {
        make_contact(0, 1),         /* contact merges 0+1 first */
        make_joint_constraint(1, 2, 3), /* joint must still merge 1+2 */
    };

    phys_island_list_t islands;
    memset(&islands, 0, sizeof(islands));

    phys_stage_island_build(&(phys_island_build_args_t){
        .constraints      = constraints,
        .constraint_count = 2,
        .bodies           = bodies,
        .body_count       = 3,
        .islands_out      = &islands,
        .arena            = &arena,
        .max_island_bodies = 2, /* tight cap: contact makes island of 2 */
    });

    /* Bodies 1 and 2 MUST be in the same island (joint). */
    uint32_t i1 = find_island_of_body(&islands, 1);
    uint32_t i2 = find_island_of_body(&islands, 2);
    ASSERT_TRUE(i1 != UINT32_MAX);
    ASSERT_TRUE(i2 != UINT32_MAX);
    ASSERT_TRUE(i1 == i2);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Joint-only world (no contacts).
 * Islands should still form correctly from joint constraints alone.
 */
static int test_joints_only_no_contacts(void)
{
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_body_t bodies[3] = {
        make_dynamic_body(),
        make_dynamic_body(),
        make_dynamic_body(),
    };

    phys_constraint_t constraints[2] = {
        make_joint_constraint(0, 1, 1),
        make_joint_constraint(1, 2, 3),
    };

    phys_island_list_t islands;
    memset(&islands, 0, sizeof(islands));

    phys_stage_island_build(&(phys_island_build_args_t){
        .constraints      = constraints,
        .constraint_count = 2,
        .bodies           = bodies,
        .body_count       = 3,
        .islands_out      = &islands,
        .arena            = &arena,
    });

    ASSERT_UINT_EQ(1, islands.count);
    ASSERT_UINT_EQ(3, islands.islands[0].body_count);
    ASSERT_UINT_EQ(2, islands.islands[0].constraint_count);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/* ── Main ──────────────────────────────────────────────────────── */

int main(void)
{
    int test_count = 0, fail_count = 0;

    printf("p101_physics_joint_island_tests\n");
    RUN_TEST(test_joint_merges_two_bodies);
    RUN_TEST(test_mixed_contact_and_joint);
    RUN_TEST(test_joint_constraint_in_island);
    RUN_TEST(test_joint_with_static_body);
    RUN_TEST(test_two_independent_islands);
    RUN_TEST(test_hinge_two_constraints_same_island);
    RUN_TEST(test_joint_chain);
    RUN_TEST(test_split_cap_respects_joints);
    RUN_TEST(test_joints_only_no_contacts);

    printf("\n%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count > 0 ? 1 : 0;
}
