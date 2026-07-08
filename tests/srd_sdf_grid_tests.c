/**
 * @file srd_sdf_grid_tests.c
 * @brief Tests for srd_sdf_grid_t: init/destroy, get/set, fill, box SDF
 *        stamping, CSG operations, and edge cases.
 *
 * TDD Phase 1 (RED): these tests define the API before implementation exists.
 */
#include "ferrum/procgen/srd/srd_sdf_grid.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

/* ── Test harness ──────────────────────────────────────────────── */

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

#define ASSERT_INT_EQ(exp, act) do { \
    int _e = (exp), _a = (act); \
    if (_e != _a) { \
        fprintf(stderr, "  FAIL %s:%d: expected %d, got %d\n", \
                __FILE__, __LINE__, _e, _a); \
        return 1; \
    } \
} while (0)

#define ASSERT_FLOAT_NEAR(exp, act, eps) do { \
    float _e = (exp), _a = (act); \
    if (fabsf(_e - _a) > (eps)) { \
        fprintf(stderr, "  FAIL %s:%d: expected %.6f, got %.6f (eps=%.6f)\n", \
                __FILE__, __LINE__, (double)_e, (double)_a, (double)(eps)); \
        return 1; \
    } \
} while (0)

#define ASSERT_FLOAT_GT(a, b) do { \
    float _a = (a), _b = (b); \
    if (!(_a > _b)) { \
        fprintf(stderr, "  FAIL %s:%d: expected %.6f > %.6f\n", \
                __FILE__, __LINE__, (double)_a, (double)_b); \
        return 1; \
    } \
} while (0)

#define ASSERT_FLOAT_LT(a, b) do { \
    float _a = (a), _b = (b); \
    if (!(_a < _b)) { \
        fprintf(stderr, "  FAIL %s:%d: expected %.6f < %.6f\n", \
                __FILE__, __LINE__, (double)_a, (double)_b); \
        return 1; \
    } \
} while (0)

/* ── Test: init and destroy ────────────────────────────────────── */

static int test_init_basic(void) {
    srd_sdf_grid_t grid;
    float origin[3] = {0.0f, 0.0f, 0.0f};
    int rc = srd_sdf_grid_init(&grid, 8, 4, 8, 0.125f, origin);
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(8, grid.nx);
    ASSERT_INT_EQ(4, grid.ny);
    ASSERT_INT_EQ(8, grid.nz);
    ASSERT_FLOAT_NEAR(0.125f, grid.voxel_size, 1e-6f);
    ASSERT_FLOAT_NEAR(0.0f, grid.origin[0], 1e-6f);
    ASSERT_FLOAT_NEAR(0.0f, grid.origin[1], 1e-6f);
    ASSERT_FLOAT_NEAR(0.0f, grid.origin[2], 1e-6f);
    ASSERT_TRUE(grid.values != NULL);
    srd_sdf_grid_destroy(&grid);
    return 0;
}

static int test_init_null(void) {
    float origin[3] = {0.0f, 0.0f, 0.0f};
    int rc = srd_sdf_grid_init(NULL, 8, 4, 8, 0.125f, origin);
    ASSERT_INT_EQ(-1, rc);
    return 0;
}

static int test_init_zero_dim(void) {
    srd_sdf_grid_t grid;
    float origin[3] = {0.0f, 0.0f, 0.0f};
    /* Zero dimension should fail */
    ASSERT_INT_EQ(-1, srd_sdf_grid_init(&grid, 0, 4, 8, 0.125f, origin));
    ASSERT_INT_EQ(-1, srd_sdf_grid_init(&grid, 8, 0, 8, 0.125f, origin));
    ASSERT_INT_EQ(-1, srd_sdf_grid_init(&grid, 8, 4, 0, 0.125f, origin));
    return 0;
}

static int test_init_values_positive(void) {
    /* After init, all voxels should be positive (outside = solid wall) */
    srd_sdf_grid_t grid;
    float origin[3] = {0.0f, 0.0f, 0.0f};
    srd_sdf_grid_init(&grid, 4, 4, 4, 0.25f, origin);
    for (int z = 0; z < 4; z++)
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++)
                ASSERT_FLOAT_GT(srd_sdf_grid_get(&grid, x, y, z), 0.0f);
    srd_sdf_grid_destroy(&grid);
    return 0;
}

static int test_destroy_null(void) {
    /* Should not crash */
    srd_sdf_grid_destroy(NULL);
    return 0;
}

/* ── Test: get and set ─────────────────────────────────────────── */

static int test_get_set_basic(void) {
    srd_sdf_grid_t grid;
    float origin[3] = {0.0f, 0.0f, 0.0f};
    srd_sdf_grid_init(&grid, 8, 4, 8, 0.125f, origin);

    srd_sdf_grid_set(&grid, 3, 2, 5, -1.5f);
    ASSERT_FLOAT_NEAR(-1.5f, srd_sdf_grid_get(&grid, 3, 2, 5), 1e-6f);

    srd_sdf_grid_set(&grid, 0, 0, 0, 0.0f);
    ASSERT_FLOAT_NEAR(0.0f, srd_sdf_grid_get(&grid, 0, 0, 0), 1e-6f);

    srd_sdf_grid_set(&grid, 7, 3, 7, 42.0f);
    ASSERT_FLOAT_NEAR(42.0f, srd_sdf_grid_get(&grid, 7, 3, 7), 1e-6f);

    srd_sdf_grid_destroy(&grid);
    return 0;
}

static int test_get_out_of_bounds(void) {
    srd_sdf_grid_t grid;
    float origin[3] = {0.0f, 0.0f, 0.0f};
    srd_sdf_grid_init(&grid, 4, 4, 4, 0.25f, origin);

    /* Out-of-bounds should return a large positive value (outside) */
    float oob = srd_sdf_grid_get(&grid, -1, 0, 0);
    ASSERT_FLOAT_GT(oob, 0.0f);

    oob = srd_sdf_grid_get(&grid, 4, 0, 0);
    ASSERT_FLOAT_GT(oob, 0.0f);

    oob = srd_sdf_grid_get(&grid, 0, 4, 0);
    ASSERT_FLOAT_GT(oob, 0.0f);

    oob = srd_sdf_grid_get(&grid, 0, 0, 4);
    ASSERT_FLOAT_GT(oob, 0.0f);

    srd_sdf_grid_destroy(&grid);
    return 0;
}

static int test_get_null(void) {
    /* Get on null grid should return large positive */
    float v = srd_sdf_grid_get(NULL, 0, 0, 0);
    ASSERT_FLOAT_GT(v, 0.0f);
    return 0;
}

static int test_set_out_of_bounds(void) {
    /* Set out-of-bounds should be a no-op (no crash) */
    srd_sdf_grid_t grid;
    float origin[3] = {0.0f, 0.0f, 0.0f};
    srd_sdf_grid_init(&grid, 4, 4, 4, 0.25f, origin);

    srd_sdf_grid_set(&grid, -1, 0, 0, -5.0f);
    srd_sdf_grid_set(&grid, 4, 0, 0, -5.0f);
    srd_sdf_grid_set(NULL, 0, 0, 0, -5.0f);

    /* Verify no corruption: all values still default */
    for (int z = 0; z < 4; z++)
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++)
                ASSERT_FLOAT_GT(srd_sdf_grid_get(&grid, x, y, z), 0.0f);

    srd_sdf_grid_destroy(&grid);
    return 0;
}

/* ── Test: fill ────────────────────────────────────────────────── */

static int test_fill(void) {
    srd_sdf_grid_t grid;
    float origin[3] = {0.0f, 0.0f, 0.0f};
    srd_sdf_grid_init(&grid, 4, 4, 4, 0.25f, origin);

    srd_sdf_grid_fill(&grid, -2.0f);

    for (int z = 0; z < 4; z++)
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++)
                ASSERT_FLOAT_NEAR(-2.0f, srd_sdf_grid_get(&grid, x, y, z), 1e-6f);

    srd_sdf_grid_destroy(&grid);
    return 0;
}

/* ── Test: stamp_box (CSG union — carve a room) ────────────────── */

static int test_stamp_box_center(void) {
    /* 16x8x16 grid at 0.25m voxels = 4m x 2m x 4m world space.
     * Stamp a 2x1x2 meter box centered at (2, 1, 2).
     * Half-extents: (1, 0.5, 1).
     * Expected: voxels inside box are negative, outside are positive. */
    srd_sdf_grid_t grid;
    float origin[3] = {0.0f, 0.0f, 0.0f};
    srd_sdf_grid_init(&grid, 16, 8, 16, 0.25f, origin);

    srd_sdf_grid_stamp_box(&grid, 2.0f, 1.0f, 2.0f, 1.0f, 0.5f, 1.0f);

    /* Center voxel (8, 4, 8) maps to world (2.0, 1.0, 2.0) — deep inside */
    float center_val = srd_sdf_grid_get(&grid, 8, 4, 8);
    ASSERT_FLOAT_LT(center_val, 0.0f);

    /* Corner voxel (0, 0, 0) maps to world (0, 0, 0) — well outside */
    float corner_val = srd_sdf_grid_get(&grid, 0, 0, 0);
    ASSERT_FLOAT_GT(corner_val, 0.0f);

    /* Voxel just inside the box edge: world (1.125, 1.0, 2.0) = voxel (4.5, 4, 8)
     * → voxel (4, 4, 8), world = (1.0, 1.0, 2.0) — on boundary
     * → voxel (5, 4, 8), world = (1.25, 1.0, 2.0) — inside (0.25m from wall) */
    float inside_edge = srd_sdf_grid_get(&grid, 5, 4, 8);
    ASSERT_FLOAT_LT(inside_edge, 0.0f);

    /* Voxel just outside: voxel (3, 4, 8), world = (0.75, 1.0, 2.0) — outside */
    float outside_edge = srd_sdf_grid_get(&grid, 3, 4, 8);
    ASSERT_FLOAT_GT(outside_edge, 0.0f);

    srd_sdf_grid_destroy(&grid);
    return 0;
}

static int test_stamp_box_union(void) {
    /* Stamp two overlapping boxes; both interiors should be negative (union) */
    srd_sdf_grid_t grid;
    float origin[3] = {0.0f, 0.0f, 0.0f};
    srd_sdf_grid_init(&grid, 32, 8, 32, 0.25f, origin);

    /* Box A: center (2,1,4), half (1, 0.5, 1) → spans X=[1,3] */
    srd_sdf_grid_stamp_box(&grid, 2.0f, 1.0f, 4.0f, 1.0f, 0.5f, 1.0f);
    /* Box B: center (3.5,1,4), half (1, 0.5, 1) → spans X=[2.5,4.5], overlaps A */
    srd_sdf_grid_stamp_box(&grid, 3.5f, 1.0f, 4.0f, 1.0f, 0.5f, 1.0f);

    /* Center of A: voxel (8, 4, 16) → world (2, 1, 4) — inside */
    ASSERT_FLOAT_LT(srd_sdf_grid_get(&grid, 8, 4, 16), 0.0f);
    /* Center of B: voxel (14, 4, 16) → world (3.5, 1, 4) — inside */
    ASSERT_FLOAT_LT(srd_sdf_grid_get(&grid, 14, 4, 16), 0.0f);
    /* Overlap region: voxel (11, 4, 16) → world (2.75, 1, 4) — inside both */
    ASSERT_FLOAT_LT(srd_sdf_grid_get(&grid, 11, 4, 16), 0.0f);
    /* Outside both: voxel (0, 0, 0) → world (0, 0, 0) */
    ASSERT_FLOAT_GT(srd_sdf_grid_get(&grid, 0, 0, 0), 0.0f);

    srd_sdf_grid_destroy(&grid);
    return 0;
}

static int test_stamp_box_clipped(void) {
    /* Stamp a box that extends beyond grid bounds — should not crash,
     * and voxels inside the grid that overlap should still be carved. */
    srd_sdf_grid_t grid;
    float origin[3] = {0.0f, 0.0f, 0.0f};
    srd_sdf_grid_init(&grid, 8, 8, 8, 0.5f, origin);
    /* Grid spans world [0,4] x [0,4] x [0,4].
     * Stamp box centered at (0, 2, 2), half (2, 1, 1).
     * Box spans [-2,2] x [1,3] x [1,3] — extends past grid edge at x<0. */
    srd_sdf_grid_stamp_box(&grid, 0.0f, 2.0f, 2.0f, 2.0f, 1.0f, 1.0f);

    /* Voxel (1, 4, 4) → world (0.5, 2, 2) — inside the box */
    ASSERT_FLOAT_LT(srd_sdf_grid_get(&grid, 1, 4, 4), 0.0f);
    /* Voxel (0, 4, 4) → world (0, 2, 2) — inside the box */
    ASSERT_FLOAT_LT(srd_sdf_grid_get(&grid, 0, 4, 4), 0.0f);

    srd_sdf_grid_destroy(&grid);
    return 0;
}

static int test_stamp_box_null(void) {
    /* Should not crash */
    srd_sdf_grid_stamp_box(NULL, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    return 0;
}

/* ── Test: CSG subtract (fill in / add wall) ───────────────────── */

static int test_subtract_box(void) {
    /* Carve a room, then subtract (fill in) part of it.
     * The subtracted region should become positive (outside). */
    srd_sdf_grid_t grid;
    float origin[3] = {0.0f, 0.0f, 0.0f};
    srd_sdf_grid_init(&grid, 16, 8, 16, 0.25f, origin);

    /* Carve a 2x1x2m room centered at (2, 1, 2) */
    srd_sdf_grid_stamp_box(&grid, 2.0f, 1.0f, 2.0f, 1.0f, 0.5f, 1.0f);

    /* Verify center is inside */
    ASSERT_FLOAT_LT(srd_sdf_grid_get(&grid, 8, 4, 8), 0.0f);

    /* Subtract (fill in) a 1x1x1m box centered at (2, 1, 2) */
    srd_sdf_grid_subtract_box(&grid, 2.0f, 1.0f, 2.0f, 0.5f, 0.5f, 0.5f);

    /* Center should now be positive (filled back in) */
    ASSERT_FLOAT_GT(srd_sdf_grid_get(&grid, 8, 4, 8), 0.0f);

    /* Edge of original room still inside: voxel (5, 4, 8) → world (1.25, 1, 2)
     * This is inside the original room but outside the subtracted box */
    ASSERT_FLOAT_LT(srd_sdf_grid_get(&grid, 5, 4, 8), 0.0f);

    srd_sdf_grid_destroy(&grid);
    return 0;
}

/* ── Test: world_to_voxel coordinate conversion ────────────────── */

static int test_world_to_voxel(void) {
    srd_sdf_grid_t grid;
    float origin[3] = {10.0f, 5.0f, 20.0f};
    srd_sdf_grid_init(&grid, 16, 8, 16, 0.25f, origin);

    int vx, vy, vz;
    /* World (10, 5, 20) → voxel (0, 0, 0) */
    srd_sdf_grid_world_to_voxel(&grid, 10.0f, 5.0f, 20.0f, &vx, &vy, &vz);
    ASSERT_INT_EQ(0, vx);
    ASSERT_INT_EQ(0, vy);
    ASSERT_INT_EQ(0, vz);

    /* World (11.0, 5.5, 21.0) → voxel (4, 2, 4) */
    srd_sdf_grid_world_to_voxel(&grid, 11.0f, 5.5f, 21.0f, &vx, &vy, &vz);
    ASSERT_INT_EQ(4, vx);
    ASSERT_INT_EQ(2, vy);
    ASSERT_INT_EQ(4, vz);

    srd_sdf_grid_destroy(&grid);
    return 0;
}

static int test_voxel_to_world(void) {
    srd_sdf_grid_t grid;
    float origin[3] = {10.0f, 5.0f, 20.0f};
    srd_sdf_grid_init(&grid, 16, 8, 16, 0.25f, origin);

    float wx, wy, wz;
    srd_sdf_grid_voxel_to_world(&grid, 0, 0, 0, &wx, &wy, &wz);
    ASSERT_FLOAT_NEAR(10.0f, wx, 1e-6f);
    ASSERT_FLOAT_NEAR(5.0f, wy, 1e-6f);
    ASSERT_FLOAT_NEAR(20.0f, wz, 1e-6f);

    srd_sdf_grid_voxel_to_world(&grid, 4, 2, 4, &wx, &wy, &wz);
    ASSERT_FLOAT_NEAR(11.0f, wx, 1e-6f);
    ASSERT_FLOAT_NEAR(5.5f, wy, 1e-6f);
    ASSERT_FLOAT_NEAR(21.0f, wz, 1e-6f);

    srd_sdf_grid_destroy(&grid);
    return 0;
}

/* ── Test: stamp box with non-zero origin ──────────────────────── */

static int test_stamp_box_offset_origin(void) {
    /* Grid origin at (10, 0, 10), voxel_size = 0.5, dims = 16x8x16.
     * Grid spans world [10,18] x [0,4] x [10,18].
     * Stamp a 2x2x2m room centered at (14, 2, 14). */
    srd_sdf_grid_t grid;
    float origin[3] = {10.0f, 0.0f, 10.0f};
    srd_sdf_grid_init(&grid, 16, 8, 16, 0.5f, origin);

    srd_sdf_grid_stamp_box(&grid, 14.0f, 2.0f, 14.0f, 1.0f, 1.0f, 1.0f);

    /* Center: world (14, 2, 14) → voxel (8, 4, 8) */
    ASSERT_FLOAT_LT(srd_sdf_grid_get(&grid, 8, 4, 8), 0.0f);
    /* Far corner: voxel (0, 0, 0) → world (10, 0, 10) — outside */
    ASSERT_FLOAT_GT(srd_sdf_grid_get(&grid, 0, 0, 0), 0.0f);

    srd_sdf_grid_destroy(&grid);
    return 0;
}

/* ── Test: stamp_sphere (for pillars) ──────────────────────────── */

static int test_stamp_sphere(void) {
    srd_sdf_grid_t grid;
    float origin[3] = {0.0f, 0.0f, 0.0f};
    srd_sdf_grid_init(&grid, 16, 16, 16, 0.25f, origin);

    /* Fill grid negative first (all inside), then subtract a sphere.
     * This simulates carving a pillar-shaped hole. */
    srd_sdf_grid_fill(&grid, -1.0f);

    /* Stamp sphere centered at (2, 2, 2), radius 0.5m.
     * Sphere SDF: length(p - center) - radius.
     * CSG union: min(grid, sphere_sdf). */
    srd_sdf_grid_stamp_sphere(&grid, 2.0f, 2.0f, 2.0f, 0.5f);

    /* Center of sphere: voxel (8, 8, 8) → world (2, 2, 2).
     * Sphere SDF at center = 0 - 0.5 = -0.5.
     * Grid was -1.0. min(-1.0, -0.5) = -1.0. Still inside. */
    ASSERT_FLOAT_LT(srd_sdf_grid_get(&grid, 8, 8, 8), 0.0f);

    /* Now test subtract_sphere: carves the sphere OUT (makes positive).
     * This is the actual "add pillar" operation. */
    srd_sdf_grid_subtract_sphere(&grid, 2.0f, 2.0f, 2.0f, 0.5f);

    /* Center should now be positive (pillar = solid) */
    ASSERT_FLOAT_GT(srd_sdf_grid_get(&grid, 8, 8, 8), 0.0f);

    /* Point outside sphere: voxel (0, 8, 8) → world (0, 2, 2).
     * Distance to center = 2.0, sphere SDF = 2.0 - 0.5 = 1.5.
     * Subtract: max(grid, -sphere_sdf) = max(-1.0, -1.5) = -1.0. Still inside. */
    ASSERT_FLOAT_LT(srd_sdf_grid_get(&grid, 0, 8, 8), 0.0f);

    srd_sdf_grid_destroy(&grid);
    return 0;
}

/* ── Test: count_negative (volume measurement) ─────────────────── */

static int test_count_negative(void) {
    srd_sdf_grid_t grid;
    float origin[3] = {0.0f, 0.0f, 0.0f};
    srd_sdf_grid_init(&grid, 8, 8, 8, 0.5f, origin);

    /* Empty grid: no negative voxels */
    ASSERT_INT_EQ(0, srd_sdf_grid_count_negative(&grid));

    /* Fill entirely: all 512 voxels negative */
    srd_sdf_grid_fill(&grid, -1.0f);
    ASSERT_INT_EQ(512, srd_sdf_grid_count_negative(&grid));

    /* Set one voxel positive */
    srd_sdf_grid_set(&grid, 0, 0, 0, 1.0f);
    ASSERT_INT_EQ(511, srd_sdf_grid_count_negative(&grid));

    srd_sdf_grid_destroy(&grid);
    return 0;
}

/* ── Test: copy ────────────────────────────────────────────────── */

static int test_copy(void) {
    srd_sdf_grid_t src, dst;
    float origin[3] = {1.0f, 2.0f, 3.0f};
    srd_sdf_grid_init(&src, 4, 4, 4, 0.5f, origin);
    srd_sdf_grid_set(&src, 1, 1, 1, -3.0f);
    srd_sdf_grid_set(&src, 2, 2, 2, -7.0f);

    int rc = srd_sdf_grid_copy(&dst, &src);
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(4, dst.nx);
    ASSERT_INT_EQ(4, dst.ny);
    ASSERT_INT_EQ(4, dst.nz);
    ASSERT_FLOAT_NEAR(0.5f, dst.voxel_size, 1e-6f);
    ASSERT_FLOAT_NEAR(1.0f, dst.origin[0], 1e-6f);
    ASSERT_FLOAT_NEAR(-3.0f, srd_sdf_grid_get(&dst, 1, 1, 1), 1e-6f);
    ASSERT_FLOAT_NEAR(-7.0f, srd_sdf_grid_get(&dst, 2, 2, 2), 1e-6f);

    /* Modify src, dst should be independent */
    srd_sdf_grid_set(&src, 1, 1, 1, 99.0f);
    ASSERT_FLOAT_NEAR(-3.0f, srd_sdf_grid_get(&dst, 1, 1, 1), 1e-6f);

    srd_sdf_grid_destroy(&src);
    srd_sdf_grid_destroy(&dst);
    return 0;
}

/* ── Test runner ───────────────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    /* init/destroy */
    {"init_basic",           test_init_basic},
    {"init_null",            test_init_null},
    {"init_zero_dim",        test_init_zero_dim},
    {"init_values_positive", test_init_values_positive},
    {"destroy_null",         test_destroy_null},
    /* get/set */
    {"get_set_basic",        test_get_set_basic},
    {"get_out_of_bounds",    test_get_out_of_bounds},
    {"get_null",             test_get_null},
    {"set_out_of_bounds",    test_set_out_of_bounds},
    /* fill */
    {"fill",                 test_fill},
    /* stamp_box */
    {"stamp_box_center",     test_stamp_box_center},
    {"stamp_box_union",      test_stamp_box_union},
    {"stamp_box_clipped",    test_stamp_box_clipped},
    {"stamp_box_null",       test_stamp_box_null},
    {"stamp_box_offset_origin", test_stamp_box_offset_origin},
    /* subtract_box */
    {"subtract_box",         test_subtract_box},
    /* coordinate conversion */
    {"world_to_voxel",       test_world_to_voxel},
    {"voxel_to_world",       test_voxel_to_world},
    /* sphere */
    {"stamp_sphere",         test_stamp_sphere},
    /* count */
    {"count_negative",       test_count_negative},
    /* copy */
    {"copy",                 test_copy},
};

int main(void) {
    size_t total = sizeof(TESTS) / sizeof(TESTS[0]);
    size_t passed = 0;

    fprintf(stderr, "srd_sdf_grid_tests: %zu tests\n", total);
    for (size_t i = 0; i < total; i++) {
        struct test_case *tc = &TESTS[i];
        fprintf(stderr, "  RUN  %s\n", tc->name);
        int rc = tc->fn();
        if (rc == 0) {
            fprintf(stderr, "  OK   %s\n", tc->name);
            passed++;
        } else {
            fprintf(stderr, "  FAIL %s\n", tc->name);
        }
    }
    fprintf(stderr, "\n%zu/%zu passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
