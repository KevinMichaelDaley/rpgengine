/**
 * @file srd_sdf_layout_tests.c
 * @brief Tests for srd_sdf_layout: init, from_grid, box ops, adjacency,
 *        SDF evaluation, and soft rasteriser.
 */
#include "ferrum/procgen/srd/srd_sdf_layout.h"
#include "ferrum/procgen/srd/srd_grammar.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT_TRUE(cond)                                                    \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n",              \
                    __FILE__, __LINE__, #cond);                              \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                              \
    do {                                                                     \
        if ((exp) != (act)) {                                                \
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d "      \
                    "got %d\n", __FILE__, __LINE__, (int)(exp), (int)(act));  \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, eps)                                     \
    do {                                                                     \
        float _e = (float)(exp);                                             \
        float _a = (float)(act);                                             \
        if (fabsf(_e - _a) > (eps)) {                                        \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: expected "     \
                    "%f got %f (eps=%f)\n", __FILE__, __LINE__,              \
                    (double)_e, (double)_a, (double)(eps));                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

/* ── Test helpers ───────────────────────────────────────────────── */

/**
 * @brief Build a simple 3-region ASCII grid for testing.
 *
 * Layout:
 *   R R B B
 *   R R B B
 *   G G G G
 *
 * Regions: R(0), B(1), G(2)
 * Edges: R-B, R-G, B-G
 */
static int build_test_grid(srd_grid_t *grid) {
    const char *lines[] = {
        "RRBB",
        "RRBB",
        "GGGG",
    };
    return srd_grid_parse(lines, 3, grid);
}

/* ── Init tests ─────────────────────────────────────────────────── */

static int test_init_zeroes(void) {
    srd_sdf_layout_t layout;
    srd_sdf_layout_init(&layout);
    ASSERT_INT_EQ(0, layout.n_boxes);
    /* Adjacency should be all zero */
    for (int i = 0; i < 16; i++)
        ASSERT_INT_EQ(0, layout.adj[i]);
    return 0;
}

static int test_from_grid_box_count(void) {
    srd_grid_t grid;
    ASSERT_INT_EQ(0, build_test_grid(&grid));

    srd_sdf_layout_t layout;
    srd_sdf_layout_init(&layout);
    ASSERT_INT_EQ(0, srd_sdf_layout_from_grid(&layout, &grid));
    ASSERT_INT_EQ(3, layout.n_boxes);

    free(grid.cells);
    free(grid.region_ids);
    free(grid.labels);
    return 0;
}

static int test_from_grid_centroid(void) {
    srd_grid_t grid;
    ASSERT_INT_EQ(0, build_test_grid(&grid));

    srd_sdf_layout_t layout;
    srd_sdf_layout_init(&layout);
    srd_sdf_layout_from_grid(&layout, &grid);

    /* Region 0 (R): cells at (0,0),(1,0),(0,1),(1,1)
     * Bounding box: xmin=0, xmax=1, zmin=0, zmax=1
     * cx = (0+1)*0.5 = 0.5, cz = (0+1)*0.5 = 0.5
     * hw = (1-0+1)*0.5 = 1.0, hd = (1-0+1)*0.5 = 1.0  */
    ASSERT_FLOAT_NEAR(0.5f, layout.boxes[0].cx, 0.01f);
    ASSERT_FLOAT_NEAR(0.5f, layout.boxes[0].cz, 0.01f);
    ASSERT_FLOAT_NEAR(1.0f, layout.boxes[0].hw, 0.01f);
    ASSERT_FLOAT_NEAR(1.0f, layout.boxes[0].hd, 0.01f);

    /* Region 2 (G): cells at (0,2),(1,2),(2,2),(3,2)
     * xmin=0, xmax=3, zmin=2, zmax=2
     * cx = 1.5, cz = 2.0, hw = 2.0, hd = 0.5 */
    ASSERT_FLOAT_NEAR(1.5f, layout.boxes[2].cx, 0.01f);
    ASSERT_FLOAT_NEAR(2.0f, layout.boxes[2].cz, 0.01f);
    ASSERT_FLOAT_NEAR(2.0f, layout.boxes[2].hw, 0.01f);
    ASSERT_FLOAT_NEAR(0.5f, layout.boxes[2].hd, 0.01f);

    free(grid.cells);
    free(grid.region_ids);
    free(grid.labels);
    return 0;
}

static int test_from_grid_types(void) {
    srd_grid_t grid;
    ASSERT_INT_EQ(0, build_test_grid(&grid));

    srd_sdf_layout_t layout;
    srd_sdf_layout_init(&layout);
    srd_sdf_layout_from_grid(&layout, &grid);

    ASSERT_INT_EQ(SRD_ROOM_GENERIC, layout.boxes[0].type);   /* R */
    ASSERT_INT_EQ(SRD_ROOM_BAR, layout.boxes[1].type);       /* B */
    ASSERT_INT_EQ(SRD_ROOM_ENTRANCE, layout.boxes[2].type);  /* G */

    free(grid.cells);
    free(grid.region_ids);
    free(grid.labels);
    return 0;
}

/* ── Adjacency tests ────────────────────────────────────────────── */

static int test_from_grid_adjacency(void) {
    srd_grid_t grid;
    ASSERT_INT_EQ(0, build_test_grid(&grid));

    srd_sdf_layout_t layout;
    srd_sdf_layout_init(&layout);
    srd_sdf_layout_from_grid(&layout, &grid);

    /* R(0) adjacent to B(1) and G(2) */
    ASSERT_TRUE(srd_sdf_layout_get_adj(&layout, 0, 1));
    ASSERT_TRUE(srd_sdf_layout_get_adj(&layout, 0, 2));
    /* B(1) adjacent to G(2) */
    ASSERT_TRUE(srd_sdf_layout_get_adj(&layout, 1, 2));
    /* Symmetric */
    ASSERT_TRUE(srd_sdf_layout_get_adj(&layout, 1, 0));
    ASSERT_TRUE(srd_sdf_layout_get_adj(&layout, 2, 0));
    ASSERT_TRUE(srd_sdf_layout_get_adj(&layout, 2, 1));
    /* Self-adjacency should be false */
    ASSERT_TRUE(!srd_sdf_layout_get_adj(&layout, 0, 0));

    free(grid.cells);
    free(grid.region_ids);
    free(grid.labels);
    return 0;
}

static int test_set_adj_symmetric(void) {
    srd_sdf_layout_t layout;
    srd_sdf_layout_init(&layout);

    srd_sdf_box_t a = {1.0f, 1.0f, 1.0f, 1.0f, SRD_ROOM_GENERIC, 0, {0}};
    srd_sdf_box_t b = {3.0f, 1.0f, 1.0f, 1.0f, SRD_ROOM_GENERIC, 0, {0}};
    srd_sdf_layout_add_box(&layout, &a);
    srd_sdf_layout_add_box(&layout, &b);

    srd_sdf_layout_set_adj(&layout, 0, 1, true);
    ASSERT_TRUE(srd_sdf_layout_get_adj(&layout, 0, 1));
    ASSERT_TRUE(srd_sdf_layout_get_adj(&layout, 1, 0));

    srd_sdf_layout_set_adj(&layout, 0, 1, false);
    ASSERT_TRUE(!srd_sdf_layout_get_adj(&layout, 0, 1));
    ASSERT_TRUE(!srd_sdf_layout_get_adj(&layout, 1, 0));
    return 0;
}

static int test_adj_count(void) {
    srd_grid_t grid;
    ASSERT_INT_EQ(0, build_test_grid(&grid));

    srd_sdf_layout_t layout;
    srd_sdf_layout_init(&layout);
    srd_sdf_layout_from_grid(&layout, &grid);

    /* R(0) -> B(1), G(2) = 2 neighbours */
    ASSERT_INT_EQ(2, srd_sdf_layout_adj_count(&layout, 0));
    ASSERT_INT_EQ(2, srd_sdf_layout_adj_count(&layout, 1));
    ASSERT_INT_EQ(2, srd_sdf_layout_adj_count(&layout, 2));

    free(grid.cells);
    free(grid.region_ids);
    free(grid.labels);
    return 0;
}

/* ── Box add/remove tests ───────────────────────────────────────── */

static int test_add_box(void) {
    srd_sdf_layout_t layout;
    srd_sdf_layout_init(&layout);

    srd_sdf_box_t box = {5.0f, 5.0f, 2.0f, 2.0f, SRD_ROOM_BOSS, 0, {0}};
    int idx = srd_sdf_layout_add_box(&layout, &box);
    ASSERT_INT_EQ(0, idx);
    ASSERT_INT_EQ(1, layout.n_boxes);
    ASSERT_FLOAT_NEAR(5.0f, layout.boxes[0].cx, 0.001f);
    ASSERT_INT_EQ(SRD_ROOM_BOSS, layout.boxes[0].type);
    return 0;
}

static int test_remove_box_shifts(void) {
    srd_sdf_layout_t layout;
    srd_sdf_layout_init(&layout);

    srd_sdf_box_t a = {1.0f, 0, 1.0f, 1.0f, SRD_ROOM_GENERIC, 0, {0}};
    srd_sdf_box_t b = {2.0f, 0, 1.0f, 1.0f, SRD_ROOM_BAR,     0, {0}};
    srd_sdf_box_t c = {3.0f, 0, 1.0f, 1.0f, SRD_ROOM_ENTRANCE, 0, {0}};
    srd_sdf_layout_add_box(&layout, &a);
    srd_sdf_layout_add_box(&layout, &b);
    srd_sdf_layout_add_box(&layout, &c);
    /* Adj: 0-1, 1-2 */
    srd_sdf_layout_set_adj(&layout, 0, 1, true);
    srd_sdf_layout_set_adj(&layout, 1, 2, true);

    /* Remove box 1 (B). Box 2 (C) becomes index 1. */
    ASSERT_INT_EQ(0, srd_sdf_layout_remove_box(&layout, 1));
    ASSERT_INT_EQ(2, layout.n_boxes);
    ASSERT_FLOAT_NEAR(1.0f, layout.boxes[0].cx, 0.001f);  /* A stays */
    ASSERT_FLOAT_NEAR(3.0f, layout.boxes[1].cx, 0.001f);  /* C shifted */
    ASSERT_INT_EQ(SRD_ROOM_ENTRANCE, layout.boxes[1].type);

    /* Adjacency: A-B was set, B removed, so A(0) should have no adj */
    ASSERT_TRUE(!srd_sdf_layout_get_adj(&layout, 0, 1));
    /* B-C was set, B removed, so new C(1) has no adj either */
    ASSERT_INT_EQ(0, srd_sdf_layout_adj_count(&layout, 0));
    ASSERT_INT_EQ(0, srd_sdf_layout_adj_count(&layout, 1));
    return 0;
}

static int test_remove_box_preserves_other_adj(void) {
    srd_sdf_layout_t layout;
    srd_sdf_layout_init(&layout);

    srd_sdf_box_t box = {0, 0, 1.0f, 1.0f, SRD_ROOM_GENERIC, 0, {0}};
    for (int i = 0; i < 4; i++) {
        box.cx = (float)i;
        srd_sdf_layout_add_box(&layout, &box);
    }
    /* Connect 0-1, 0-2, 0-3, 2-3 */
    srd_sdf_layout_set_adj(&layout, 0, 1, true);
    srd_sdf_layout_set_adj(&layout, 0, 2, true);
    srd_sdf_layout_set_adj(&layout, 0, 3, true);
    srd_sdf_layout_set_adj(&layout, 2, 3, true);

    /* Remove box 1. Boxes 2,3 shift to indices 1,2 */
    srd_sdf_layout_remove_box(&layout, 1);
    ASSERT_INT_EQ(3, layout.n_boxes);

    /* 0 was adj to 2 (now 1) and 3 (now 2) */
    ASSERT_TRUE(srd_sdf_layout_get_adj(&layout, 0, 1));
    ASSERT_TRUE(srd_sdf_layout_get_adj(&layout, 0, 2));
    /* Old 2-3 (now 1-2) */
    ASSERT_TRUE(srd_sdf_layout_get_adj(&layout, 1, 2));
    return 0;
}

/* ── Copy test ──────────────────────────────────────────────────── */

static int test_copy_independent(void) {
    srd_sdf_layout_t src;
    srd_sdf_layout_init(&src);

    srd_sdf_box_t box = {1.0f, 2.0f, 3.0f, 4.0f, SRD_ROOM_GENERIC, 0, {0}};
    srd_sdf_layout_add_box(&src, &box);
    box.cx = 5.0f;
    srd_sdf_layout_add_box(&src, &box);
    srd_sdf_layout_set_adj(&src, 0, 1, true);

    srd_sdf_layout_t dst;
    srd_sdf_layout_copy(&dst, &src);

    /* Should be equal */
    ASSERT_INT_EQ(src.n_boxes, dst.n_boxes);
    ASSERT_FLOAT_NEAR(src.boxes[0].cx, dst.boxes[0].cx, 0.001f);
    ASSERT_TRUE(srd_sdf_layout_get_adj(&dst, 0, 1));

    /* Modify dst — src should be unaffected */
    dst.boxes[0].cx = 99.0f;
    srd_sdf_layout_set_adj(&dst, 0, 1, false);
    ASSERT_FLOAT_NEAR(1.0f, src.boxes[0].cx, 0.001f);
    ASSERT_TRUE(srd_sdf_layout_get_adj(&src, 0, 1));
    return 0;
}

/* ── SDF evaluation tests ──────────────────────────────────────── */

static int test_sdf_box_inside(void) {
    srd_sdf_box_t box = {5.0f, 5.0f, 2.0f, 3.0f, SRD_ROOM_GENERIC, 0, {0}};
    /* Centre should be negative (inside) */
    float val = srd_sdf_box_eval(&box, 5.0f, 5.0f);
    ASSERT_TRUE(val < 0.0f);
    return 0;
}

static int test_sdf_box_outside(void) {
    srd_sdf_box_t box = {5.0f, 5.0f, 2.0f, 3.0f, SRD_ROOM_GENERIC, 0, {0}};
    /* Well outside */
    float val = srd_sdf_box_eval(&box, 10.0f, 10.0f);
    ASSERT_TRUE(val > 0.0f);
    return 0;
}

static int test_sdf_box_boundary(void) {
    srd_sdf_box_t box = {5.0f, 5.0f, 2.0f, 3.0f, SRD_ROOM_GENERIC, 0, {0}};
    /* On the X boundary: cx + hw = 7.0 */
    float val = srd_sdf_box_eval(&box, 7.0f, 5.0f);
    ASSERT_FLOAT_NEAR(0.0f, val, 0.001f);
    return 0;
}

/* ── Overlap / containment tests ───────────────────────────────── */

static int test_overlap_true(void) {
    srd_sdf_box_t a = {1.0f, 1.0f, 1.0f, 1.0f, SRD_ROOM_GENERIC, 0, {0}};
    srd_sdf_box_t b = {1.5f, 1.0f, 1.0f, 1.0f, SRD_ROOM_GENERIC, 0, {0}};
    ASSERT_TRUE(srd_sdf_box_overlap(&a, &b));
    return 0;
}

static int test_overlap_false(void) {
    srd_sdf_box_t a = {0.0f, 0.0f, 1.0f, 1.0f, SRD_ROOM_GENERIC, 0, {0}};
    srd_sdf_box_t b = {5.0f, 5.0f, 1.0f, 1.0f, SRD_ROOM_GENERIC, 0, {0}};
    ASSERT_TRUE(!srd_sdf_box_overlap(&a, &b));
    return 0;
}

static int test_contains_true(void) {
    srd_sdf_box_t outer = {5.0f, 5.0f, 5.0f, 5.0f, SRD_ROOM_GENERIC, 0, {0}};
    srd_sdf_box_t inner = {5.0f, 5.0f, 1.0f, 1.0f, SRD_ROOM_GENERIC, 0, {0}};
    ASSERT_TRUE(srd_sdf_box_contains(&outer, &inner));
    ASSERT_TRUE(!srd_sdf_box_contains(&inner, &outer));
    return 0;
}

/* ── Smooth union test ──────────────────────────────────────────── */

static int test_union_inside_box(void) {
    srd_sdf_layout_t layout;
    srd_sdf_layout_init(&layout);

    srd_sdf_box_t box = {5.0f, 5.0f, 2.0f, 2.0f, SRD_ROOM_GENERIC, 0, {0}};
    srd_sdf_layout_add_box(&layout, &box);

    /* Inside the only box — union SDF should be negative */
    float val = srd_sdf_layout_union(&layout, 5.0f, 5.0f, 0.1f);
    ASSERT_TRUE(val < 0.0f);

    /* Outside the only box — union SDF should be positive */
    val = srd_sdf_layout_union(&layout, 10.0f, 10.0f, 0.1f);
    ASSERT_TRUE(val > 0.0f);
    return 0;
}

/* ── Soft rasteriser test ───────────────────────────────────────── */

static int test_rasterize_inside_occupied(void) {
    srd_sdf_layout_t layout;
    srd_sdf_layout_init(&layout);

    /* Box at (5,5) with half-extent 2 — covers [3,7]x[3,7] */
    srd_sdf_box_t box = {5.0f, 5.0f, 2.0f, 2.0f, SRD_ROOM_GENERIC, 0, {0}};
    srd_sdf_layout_add_box(&layout, &box);

    float grid[100];  /* 10x10 */
    srd_sdf_layout_rasterize(&layout, 10, 10, 0.1f, grid);

    /* Cell (5,5) maps to world (5.5, 5.5) — inside the box */
    ASSERT_TRUE(grid[5 * 10 + 5] > 0.9f);

    /* Cell (0,0) maps to world (0.5, 0.5) — outside the box */
    ASSERT_TRUE(grid[0 * 10 + 0] < 0.1f);
    return 0;
}

/* ── Null/error handling tests ─────────────────────────────────── */

static int test_null_args(void) {
    ASSERT_INT_EQ(-1, srd_sdf_layout_from_grid(NULL, NULL));
    ASSERT_TRUE(!srd_sdf_layout_get_adj(NULL, 0, 1));
    ASSERT_INT_EQ(0, srd_sdf_layout_adj_count(NULL, 0));
    return 0;
}

/* ── Test runner ────────────────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"init_zeroes",                  test_init_zeroes},
    {"from_grid_box_count",          test_from_grid_box_count},
    {"from_grid_centroid",           test_from_grid_centroid},
    {"from_grid_types",              test_from_grid_types},
    {"from_grid_adjacency",          test_from_grid_adjacency},
    {"set_adj_symmetric",            test_set_adj_symmetric},
    {"adj_count",                    test_adj_count},
    {"add_box",                      test_add_box},
    {"remove_box_shifts",            test_remove_box_shifts},
    {"remove_preserves_other_adj",   test_remove_box_preserves_other_adj},
    {"copy_independent",             test_copy_independent},
    {"sdf_box_inside",               test_sdf_box_inside},
    {"sdf_box_outside",              test_sdf_box_outside},
    {"sdf_box_boundary",             test_sdf_box_boundary},
    {"overlap_true",                 test_overlap_true},
    {"overlap_false",                test_overlap_false},
    {"contains_true",                test_contains_true},
    {"union_inside_box",             test_union_inside_box},
    {"rasterize_inside_occupied",    test_rasterize_inside_occupied},
    {"null_args",                    test_null_args},
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0;
    for (size_t i = 0; i < total; i++) {
        struct test_case *tc = &TESTS[i];
        printf("RUN  %s\n", tc->name);
        int rc = tc->fn();
        if (rc == 0) {
            printf("  OK %s\n", tc->name);
            passed++;
        } else {
            printf("FAIL %s\n", tc->name);
        }
    }
    printf("\n%zu/%zu passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
