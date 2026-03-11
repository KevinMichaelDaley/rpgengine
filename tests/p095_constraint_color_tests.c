/**
 * @file p095_constraint_color_tests.c
 * @brief Tests for greedy constraint graph coloring.
 *
 * Validates that phys_constraint_color() produces valid colorings
 * where no two constraints sharing a body have the same color.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/constraint.h"
#include "ferrum/physics/constraint_color.h"

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

static void make_constraint_(phys_constraint_t *c, uint32_t a, uint32_t b) {
    memset(c, 0, sizeof(*c));
    c->body_a = a;
    c->body_b = b;
    c->row_count = 1;
}

/** Validate that no two constraints sharing a body have the same color. */
static int validate_coloring_(const phys_constraint_t *constraints,
                               uint32_t count,
                               const uint32_t *colors) {
    for (uint32_t i = 0; i < count; ++i) {
        for (uint32_t j = i + 1; j < count; ++j) {
            /* Check if constraints i and j share a body. */
            if (constraints[i].body_a == constraints[j].body_a ||
                constraints[i].body_a == constraints[j].body_b ||
                constraints[i].body_b == constraints[j].body_a ||
                constraints[i].body_b == constraints[j].body_b) {
                if (colors[i] == colors[j]) {
                    fprintf(stderr,
                            "  COLORING VIOLATION: c[%u] and c[%u] "
                            "share body, both color %u\n",
                            i, j, colors[i]);
                    return 0; /* invalid */
                }
            }
        }
    }
    return 1; /* valid */
}

/* Scratch buffer large enough for all tests (64 KB). */
static uint8_t g_scratch[64 * 1024];

/* ── Test: chain A-B, B-C, C-D → valid 2-coloring ─────────────── */

static int test_chain_coloring(void) {
    /* Chain: c0(0,1), c1(1,2), c2(2,3)
     * c0 and c1 share body 1 → different colors.
     * c1 and c2 share body 2 → different colors.
     * c0 and c2 do NOT share a body → may share color.
     * Optimal: 2 colors. */
    phys_constraint_t constraints[3];
    make_constraint_(&constraints[0], 0, 1);
    make_constraint_(&constraints[1], 1, 2);
    make_constraint_(&constraints[2], 2, 3);

    phys_color_result_t result;
    size_t need = phys_constraint_color_scratch_size(3, 4);
    ASSERT_TRUE(need <= sizeof(g_scratch));

    ASSERT_INT_EQ(0, phys_constraint_color(constraints, 3, 4,
                                            g_scratch, sizeof(g_scratch),
                                            &result));
    ASSERT_INT_EQ(3, (int)result.count);
    ASSERT_TRUE(result.num_colors <= 2);
    ASSERT_TRUE(validate_coloring_(constraints, 3, result.colors));

    return 0;
}

/* ── Test: star graph (all share central body) ─────────────────── */

static int test_star_coloring(void) {
    /* Star: c0(0,1), c1(0,2), c2(0,3), c3(0,4)
     * All share body 0 → all must have different colors.
     * Needs 4 colors. */
    phys_constraint_t constraints[4];
    make_constraint_(&constraints[0], 0, 1);
    make_constraint_(&constraints[1], 0, 2);
    make_constraint_(&constraints[2], 0, 3);
    make_constraint_(&constraints[3], 0, 4);

    phys_color_result_t result;

    ASSERT_INT_EQ(0, phys_constraint_color(constraints, 4, 5,
                                            g_scratch, sizeof(g_scratch),
                                            &result));
    ASSERT_INT_EQ(4, (int)result.count);
    ASSERT_INT_EQ(4, (int)result.num_colors);
    ASSERT_TRUE(validate_coloring_(constraints, 4, result.colors));

    return 0;
}

/* ── Test: fully disconnected constraints → all color 0 ────────── */

static int test_disconnected_all_color_0(void) {
    /* c0(0,1), c1(2,3), c2(4,5) — no shared bodies. */
    phys_constraint_t constraints[3];
    make_constraint_(&constraints[0], 0, 1);
    make_constraint_(&constraints[1], 2, 3);
    make_constraint_(&constraints[2], 4, 5);

    phys_color_result_t result;

    ASSERT_INT_EQ(0, phys_constraint_color(constraints, 3, 6,
                                            g_scratch, sizeof(g_scratch),
                                            &result));
    ASSERT_INT_EQ(3, (int)result.count);
    ASSERT_INT_EQ(1, (int)result.num_colors);
    for (uint32_t i = 0; i < 3; ++i) {
        ASSERT_INT_EQ(0, (int)result.colors[i]);
    }

    return 0;
}

/* ── Test: single constraint gets color 0 ──────────────────────── */

static int test_single_constraint(void) {
    phys_constraint_t constraints[1];
    make_constraint_(&constraints[0], 0, 1);

    phys_color_result_t result;

    ASSERT_INT_EQ(0, phys_constraint_color(constraints, 1, 2,
                                            g_scratch, sizeof(g_scratch),
                                            &result));
    ASSERT_INT_EQ(1, (int)result.count);
    ASSERT_INT_EQ(1, (int)result.num_colors);
    ASSERT_INT_EQ(0, (int)result.colors[0]);

    return 0;
}

/* ── Test: null/empty input ────────────────────────────────────── */

static int test_null_args(void) {
    ASSERT_INT_EQ(-1, phys_constraint_color(NULL, 0, 0,
                                             NULL, 0, NULL));

    phys_color_result_t result;

    /* Zero constraints → success, 0 colors. */
    ASSERT_INT_EQ(0, phys_constraint_color(NULL, 0, 0,
                                            g_scratch, sizeof(g_scratch),
                                            &result));
    ASSERT_INT_EQ(0, (int)result.count);
    ASSERT_INT_EQ(0, (int)result.num_colors);

    return 0;
}

/* ── Test: triangle (3-clique) needs 3 colors ──────────────────── */

static int test_triangle(void) {
    /* c0(0,1), c1(1,2), c2(0,2)
     * Every pair shares a body → 3 colors needed. */
    phys_constraint_t constraints[3];
    make_constraint_(&constraints[0], 0, 1);
    make_constraint_(&constraints[1], 1, 2);
    make_constraint_(&constraints[2], 0, 2);

    phys_color_result_t result;

    ASSERT_INT_EQ(0, phys_constraint_color(constraints, 3, 3,
                                            g_scratch, sizeof(g_scratch),
                                            &result));
    ASSERT_INT_EQ(3, (int)result.num_colors);
    ASSERT_TRUE(validate_coloring_(constraints, 3, result.colors));

    return 0;
}

/* ── Test: dense mesh (many contacts on same bodies) ───────────── */

static int test_dense_mesh(void) {
    /* 8 constraints forming a dense cluster.
     * Validate correctness, not optimality. */
    phys_constraint_t constraints[8];
    make_constraint_(&constraints[0], 0, 1);
    make_constraint_(&constraints[1], 1, 2);
    make_constraint_(&constraints[2], 2, 3);
    make_constraint_(&constraints[3], 3, 0);
    make_constraint_(&constraints[4], 0, 2);
    make_constraint_(&constraints[5], 1, 3);
    make_constraint_(&constraints[6], 0, 4);
    make_constraint_(&constraints[7], 4, 2);

    phys_color_result_t result;

    ASSERT_INT_EQ(0, phys_constraint_color(constraints, 8, 5,
                                            g_scratch, sizeof(g_scratch),
                                            &result));
    ASSERT_INT_EQ(8, (int)result.count);
    ASSERT_TRUE(result.num_colors >= 1);
    ASSERT_TRUE(validate_coloring_(constraints, 8, result.colors));

    return 0;
}

/* ── Test: insufficient scratch returns error ──────────────────── */

static int test_insufficient_scratch(void) {
    phys_constraint_t constraints[3];
    make_constraint_(&constraints[0], 0, 1);
    make_constraint_(&constraints[1], 1, 2);
    make_constraint_(&constraints[2], 2, 3);

    phys_color_result_t result;
    uint8_t tiny[4]; /* way too small */

    ASSERT_INT_EQ(-1, phys_constraint_color(constraints, 3, 4,
                                             tiny, sizeof(tiny),
                                             &result));
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

    printf("p095_constraint_color_tests:\n");

    RUN_TEST(test_chain_coloring);
    RUN_TEST(test_star_coloring);
    RUN_TEST(test_disconnected_all_color_0);
    RUN_TEST(test_single_constraint);
    RUN_TEST(test_null_args);
    RUN_TEST(test_triangle);
    RUN_TEST(test_dense_mesh);
    RUN_TEST(test_insufficient_scratch);

    printf("%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
