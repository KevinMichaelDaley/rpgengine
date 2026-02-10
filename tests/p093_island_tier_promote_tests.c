/**
 * @file p093_island_tier_promote_tests.c
 * @brief Tests for Stage 10b: island tier promotion.
 *
 * Verifies that all bodies in each island are promoted to the
 * highest-fidelity (lowest-numbered) tier found in the island,
 * and that constraint solver_mode fields are updated to match.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/island.h"
#include "ferrum/physics/island_tier_promote.h"
#include "ferrum/physics/step_plan.h"
#include "ferrum/physics/tier_list.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond)                                                \
    do {                                                                 \
        if (!(cond)) {                                                   \
            fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__,   \
                    #cond);                                              \
            return 1;                                                    \
        }                                                                \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                          \
    do {                                                                 \
        if ((int)(exp) != (int)(act)) {                                  \
            fprintf(stderr, "FAIL: %s:%d: expected %d got %d\n",       \
                    __FILE__, __LINE__, (int)(exp), (int)(act));          \
            return 1;                                                    \
        }                                                                \
    } while (0)

/* ── Helpers ────────────────────────────────────────────────────── */

/** Initialize a body with a given tier and mass. */
static void init_body_(phys_body_t *b, uint8_t tier, float mass) {
    phys_body_init(b);
    b->tier = tier;
    if (mass > 0.0f) {
        phys_body_set_mass(b, mass);
    } else {
        b->inv_mass = 0.0f;
    }
}

/** Build a minimal constraint linking two bodies. */
static void init_constraint_(phys_constraint_t *c, uint32_t a, uint32_t b,
                              uint8_t solver_mode) {
    memset(c, 0, sizeof(*c));
    c->body_a = a;
    c->body_b = b;
    c->solver_mode = solver_mode;
    c->row_count = 1;
}

/* ── Test: single island, all promoted to min tier ──────────────── */

static int test_single_island_promote(void) {
    /* 3 bodies: T0, T2, T3 — all in one island.
     * After promotion: all should be T0. */
    phys_body_t bodies[3];
    init_body_(&bodies[0], PHYS_TIER_0_DIRECT, 1.0f);
    init_body_(&bodies[1], PHYS_TIER_2_VISIBLE, 1.0f);
    init_body_(&bodies[2], PHYS_TIER_3_WORLD, 1.0f);

    /* Constraints: 0-1, 1-2 (chain). */
    phys_constraint_t constraints[2];
    init_constraint_(&constraints[0], 0, 1, PHYS_SOLVER_TGS);
    init_constraint_(&constraints[1], 1, 2, PHYS_SOLVER_XPBD);

    /* Build island manually. */
    uint32_t body_indices[3] = {0, 1, 2};
    uint32_t constraint_indices[2] = {0, 1};
    phys_island_t island = {
        .body_indices = body_indices,
        .body_count = 3,
        .constraint_indices = constraint_indices,
        .constraint_count = 2,
        .sleeping = false,
        .skip = false,
    };
    phys_island_list_t islands = {
        .islands = &island,
        .count = 1,
        .capacity = 1,
    };

    phys_stage_island_tier_promote(&(phys_island_tier_promote_args_t){
        .islands = &islands,
        .bodies = bodies,
        .body_count = 3,
        .constraints = constraints,
        .constraint_count = 2,
    });

    /* All bodies should be T0. */
    ASSERT_INT_EQ(PHYS_TIER_0_DIRECT, bodies[0].tier);
    ASSERT_INT_EQ(PHYS_TIER_0_DIRECT, bodies[1].tier);
    ASSERT_INT_EQ(PHYS_TIER_0_DIRECT, bodies[2].tier);

    /* All constraints should be TGS (since T0). */
    ASSERT_INT_EQ(PHYS_SOLVER_TGS, constraints[0].solver_mode);
    ASSERT_INT_EQ(PHYS_SOLVER_TGS, constraints[1].solver_mode);

    return 0;
}

/* ── Test: two islands, independent promotion ───────────────────── */

static int test_multi_island_independent(void) {
    /* Island A: bodies 0(T1), 1(T3) → promoted to T1.
     * Island B: bodies 2(T2), 3(T4) → promoted to T2. */
    phys_body_t bodies[4];
    init_body_(&bodies[0], PHYS_TIER_1_NEAR, 1.0f);
    init_body_(&bodies[1], PHYS_TIER_3_WORLD, 1.0f);
    init_body_(&bodies[2], PHYS_TIER_2_VISIBLE, 1.0f);
    init_body_(&bodies[3], PHYS_TIER_4_BACKGROUND, 1.0f);

    phys_constraint_t constraints[2];
    init_constraint_(&constraints[0], 0, 1, PHYS_SOLVER_TGS);
    init_constraint_(&constraints[1], 2, 3, PHYS_SOLVER_XPBD);

    uint32_t bi_a[2] = {0, 1};
    uint32_t ci_a[1] = {0};
    uint32_t bi_b[2] = {2, 3};
    uint32_t ci_b[1] = {1};

    phys_island_t isle[2] = {
        {.body_indices = bi_a, .body_count = 2,
         .constraint_indices = ci_a, .constraint_count = 1},
        {.body_indices = bi_b, .body_count = 2,
         .constraint_indices = ci_b, .constraint_count = 1},
    };
    phys_island_list_t islands = {.islands = isle, .count = 2, .capacity = 2};

    phys_stage_island_tier_promote(&(phys_island_tier_promote_args_t){
        .islands = &islands,
        .bodies = bodies,
        .body_count = 4,
        .constraints = constraints,
        .constraint_count = 2,
    });

    /* Island A: both T1, solver TGS. */
    ASSERT_INT_EQ(PHYS_TIER_1_NEAR, bodies[0].tier);
    ASSERT_INT_EQ(PHYS_TIER_1_NEAR, bodies[1].tier);
    ASSERT_INT_EQ(PHYS_SOLVER_TGS, constraints[0].solver_mode);

    /* Island B: both T2, solver XPBD. */
    ASSERT_INT_EQ(PHYS_TIER_2_VISIBLE, bodies[2].tier);
    ASSERT_INT_EQ(PHYS_TIER_2_VISIBLE, bodies[3].tier);
    ASSERT_INT_EQ(PHYS_SOLVER_XPBD, constraints[1].solver_mode);

    return 0;
}

/* ── Test: static body excluded from min-tier computation ──────── */

static int test_static_body_excluded(void) {
    /* Body 0: static (inv_mass=0), T0.
     * Body 1: dynamic, T3.
     * Body 2: dynamic, T4.
     * Static body should not drag the island to T0 — the min among
     * dynamic bodies is T3. */
    phys_body_t bodies[3];
    init_body_(&bodies[0], PHYS_TIER_0_DIRECT, 0.0f); /* static */
    init_body_(&bodies[1], PHYS_TIER_3_WORLD, 1.0f);
    init_body_(&bodies[2], PHYS_TIER_4_BACKGROUND, 1.0f);

    phys_constraint_t constraints[2];
    init_constraint_(&constraints[0], 0, 1, PHYS_SOLVER_XPBD);
    init_constraint_(&constraints[1], 1, 2, PHYS_SOLVER_XPBD);

    uint32_t bi[3] = {0, 1, 2};
    uint32_t ci[2] = {0, 1};
    phys_island_t island = {
        .body_indices = bi, .body_count = 3,
        .constraint_indices = ci, .constraint_count = 2,
    };
    phys_island_list_t islands = {.islands = &island, .count = 1, .capacity = 1};

    phys_stage_island_tier_promote(&(phys_island_tier_promote_args_t){
        .islands = &islands,
        .bodies = bodies,
        .body_count = 3,
        .constraints = constraints,
        .constraint_count = 2,
    });

    /* Static body tier unchanged. */
    ASSERT_INT_EQ(PHYS_TIER_0_DIRECT, bodies[0].tier);
    /* Dynamic bodies promoted to T3 (min of T3, T4). */
    ASSERT_INT_EQ(PHYS_TIER_3_WORLD, bodies[1].tier);
    ASSERT_INT_EQ(PHYS_TIER_3_WORLD, bodies[2].tier);

    /* Constraints solver_mode: T3 → XPBD. */
    ASSERT_INT_EQ(PHYS_SOLVER_XPBD, constraints[0].solver_mode);
    ASSERT_INT_EQ(PHYS_SOLVER_XPBD, constraints[1].solver_mode);

    return 0;
}

/* ── Test: sleeping island is skipped ──────────────────────────── */

static int test_sleeping_island_skipped(void) {
    phys_body_t bodies[2];
    init_body_(&bodies[0], PHYS_TIER_1_NEAR, 1.0f);
    init_body_(&bodies[1], PHYS_TIER_3_WORLD, 1.0f);

    phys_constraint_t constraints[1];
    init_constraint_(&constraints[0], 0, 1, PHYS_SOLVER_XPBD);

    uint32_t bi[2] = {0, 1};
    uint32_t ci[1] = {0};
    phys_island_t island = {
        .body_indices = bi, .body_count = 2,
        .constraint_indices = ci, .constraint_count = 1,
        .sleeping = true,
    };
    phys_island_list_t islands = {.islands = &island, .count = 1, .capacity = 1};

    phys_stage_island_tier_promote(&(phys_island_tier_promote_args_t){
        .islands = &islands,
        .bodies = bodies,
        .body_count = 2,
        .constraints = constraints,
        .constraint_count = 1,
    });

    /* Tiers should be unchanged (island is sleeping). */
    ASSERT_INT_EQ(PHYS_TIER_1_NEAR, bodies[0].tier);
    ASSERT_INT_EQ(PHYS_TIER_3_WORLD, bodies[1].tier);
    ASSERT_INT_EQ(PHYS_SOLVER_XPBD, constraints[0].solver_mode);

    return 0;
}

/* ── Test: single-body island is no-op ─────────────────────────── */

static int test_single_body_island_noop(void) {
    phys_body_t bodies[1];
    init_body_(&bodies[0], PHYS_TIER_2_VISIBLE, 1.0f);

    uint32_t bi[1] = {0};
    phys_island_t island = {
        .body_indices = bi, .body_count = 1,
        .constraint_indices = NULL, .constraint_count = 0,
    };
    phys_island_list_t islands = {.islands = &island, .count = 1, .capacity = 1};

    phys_stage_island_tier_promote(&(phys_island_tier_promote_args_t){
        .islands = &islands,
        .bodies = bodies,
        .body_count = 1,
        .constraints = NULL,
        .constraint_count = 0,
    });

    /* Tier unchanged — single body, nothing to promote. */
    ASSERT_INT_EQ(PHYS_TIER_2_VISIBLE, bodies[0].tier);

    return 0;
}

/* ── Test: NULL args returns safely ─────────────────────────────── */

static int test_null_args(void) {
    /* Should not crash. */
    phys_stage_island_tier_promote(NULL);
    return 0;
}

/* ── Runner ─────────────────────────────────────────────────────── */

#define RUN_TEST(fn)                                                     \
    do {                                                                 \
        printf("  %-60s", #fn);                                          \
        int _r = fn();                                                   \
        printf("%s\n", _r ? "FAIL" : "PASS");                         \
        if (_r) fail_count++;                                            \
        test_count++;                                                    \
    } while (0)

int main(void) {
    int fail_count = 0;
    int test_count = 0;

    printf("p093_island_tier_promote_tests:\n");

    RUN_TEST(test_single_island_promote);
    RUN_TEST(test_multi_island_independent);
    RUN_TEST(test_static_body_excluded);
    RUN_TEST(test_sleeping_island_skipped);
    RUN_TEST(test_single_body_island_noop);
    RUN_TEST(test_null_args);

    printf("%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
