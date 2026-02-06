#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/spatial_grid.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond)                                                                                \
    do {                                                                                                 \
        if (!(cond)) {                                                                                   \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                                                          \
    do {                                                                                                 \
        if ((exp) != (act)) {                                                                            \
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d got %d\n", __FILE__, __LINE__,     \
                    (int)(exp), (int)(act));                                                              \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_PTR_NOT_NULL(ptr)                                                                         \
    do {                                                                                                 \
        if ((ptr) == NULL) {                                                                             \
            fprintf(stderr, "ASSERT_PTR_NOT_NULL failed: %s:%d: %s\n", __FILE__, __LINE__, #ptr);        \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

/* ── Helper: check if a value is in an array ────────────────────── */

static bool results_contain(const uint32_t *arr, uint32_t count, uint32_t val) {
    for (uint32_t i = 0; i < count; ++i) {
        if (arr[i] == val) {
            return true;
        }
    }
    return false;
}

/* ── Tests ──────────────────────────────────────────────────────── */

static int test_grid_init(void) {
    phys_frame_arena_t arena;
    int rc = phys_frame_arena_init(&arena, 4 * 1024 * 1024);
    ASSERT_INT_EQ(0, rc);

    phys_spatial_grid_t grid;
    phys_spatial_grid_init(&grid, 1024, 10.0f, &arena);

    ASSERT_PTR_NOT_NULL(grid.cells);
    ASSERT_INT_EQ(1024, (int)grid.cell_count);
    ASSERT_INT_EQ(1023, (int)grid.cell_mask);

    /* All cells should start with count = 0. */
    for (uint32_t i = 0; i < grid.cell_count; ++i) {
        ASSERT_INT_EQ(0, (int)grid.cells[i].count);
    }

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_grid_insert_single(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 4 * 1024 * 1024);

    phys_spatial_grid_t grid;
    phys_spatial_grid_init(&grid, 1024, 10.0f, &arena);

    /* Insert a small AABB that fits in one cell. */
    phys_aabb_t aabb = {.min = {.x = 5, .y = 5, .z = 5},
                        .max = {.x = 6, .y = 6, .z = 6}};
    phys_spatial_grid_insert(&grid, 42, &aabb);

    uint32_t results[100];
    uint32_t count = phys_spatial_grid_query(&grid, &aabb, results, 100);
    ASSERT_INT_EQ(1, (int)count);
    ASSERT_INT_EQ(42, (int)results[0]);

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_grid_insert_same_cell(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 4 * 1024 * 1024);

    phys_spatial_grid_t grid;
    phys_spatial_grid_init(&grid, 1024, 10.0f, &arena);

    /* Two small AABBs in the same 10m cell. */
    phys_aabb_t a1 = {.min = {.x = 1, .y = 1, .z = 1},
                      .max = {.x = 2, .y = 2, .z = 2}};
    phys_aabb_t a2 = {.min = {.x = 3, .y = 3, .z = 3},
                      .max = {.x = 4, .y = 4, .z = 4}};
    phys_spatial_grid_insert(&grid, 10, &a1);
    phys_spatial_grid_insert(&grid, 20, &a2);

    /* Query the whole cell. */
    phys_aabb_t query = {.min = {.x = 0, .y = 0, .z = 0},
                         .max = {.x = 9, .y = 9, .z = 9}};
    uint32_t results[100];
    uint32_t count = phys_spatial_grid_query(&grid, &query, results, 100);
    ASSERT_INT_EQ(2, (int)count);
    ASSERT_TRUE(results_contain(results, count, 10));
    ASSERT_TRUE(results_contain(results, count, 20));

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_grid_insert_different_cells(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 4 * 1024 * 1024);

    phys_spatial_grid_t grid;
    phys_spatial_grid_init(&grid, 1024, 10.0f, &arena);

    /* Two bodies in clearly different cells (cell_size = 10). */
    phys_aabb_t a1 = {.min = {.x = 5, .y = 5, .z = 5},
                      .max = {.x = 6, .y = 6, .z = 6}};
    phys_aabb_t a2 = {.min = {.x = 55, .y = 55, .z = 55},
                      .max = {.x = 56, .y = 56, .z = 56}};
    phys_spatial_grid_insert(&grid, 1, &a1);
    phys_spatial_grid_insert(&grid, 2, &a2);

    /* Query near body 1 only. */
    uint32_t results[100];
    uint32_t count = phys_spatial_grid_query(&grid, &a1, results, 100);
    ASSERT_INT_EQ(1, (int)count);
    ASSERT_INT_EQ(1, (int)results[0]);

    /* Query near body 2 only. */
    count = phys_spatial_grid_query(&grid, &a2, results, 100);
    ASSERT_INT_EQ(1, (int)count);
    ASSERT_INT_EQ(2, (int)results[0]);

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_grid_large_aabb_spans_cells(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 4 * 1024 * 1024);

    phys_spatial_grid_t grid;
    phys_spatial_grid_init(&grid, 1024, 10.0f, &arena);

    /* AABB spanning 3x3x3 cells (0..25 across 10m cells → cells 0,1,2). */
    phys_aabb_t large = {.min = {.x = 0, .y = 0, .z = 0},
                         .max = {.x = 25, .y = 25, .z = 25}};
    phys_spatial_grid_insert(&grid, 7, &large);

    /* Query a small region inside each cell should find body 7. */
    phys_aabb_t q1 = {.min = {.x = 1, .y = 1, .z = 1},
                      .max = {.x = 2, .y = 2, .z = 2}};
    phys_aabb_t q2 = {.min = {.x = 11, .y = 11, .z = 11},
                      .max = {.x = 12, .y = 12, .z = 12}};
    phys_aabb_t q3 = {.min = {.x = 21, .y = 21, .z = 21},
                      .max = {.x = 22, .y = 22, .z = 22}};

    uint32_t results[100];
    uint32_t c1 = phys_spatial_grid_query(&grid, &q1, results, 100);
    ASSERT_TRUE(c1 >= 1);
    ASSERT_TRUE(results_contain(results, c1, 7));

    uint32_t c2 = phys_spatial_grid_query(&grid, &q2, results, 100);
    ASSERT_TRUE(c2 >= 1);
    ASSERT_TRUE(results_contain(results, c2, 7));

    uint32_t c3 = phys_spatial_grid_query(&grid, &q3, results, 100);
    ASSERT_TRUE(c3 >= 1);
    ASSERT_TRUE(results_contain(results, c3, 7));

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_grid_query_no_results(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 4 * 1024 * 1024);

    phys_spatial_grid_t grid;
    phys_spatial_grid_init(&grid, 1024, 10.0f, &arena);

    /* Insert a body near the origin. */
    phys_aabb_t aabb = {.min = {.x = 1, .y = 1, .z = 1},
                        .max = {.x = 2, .y = 2, .z = 2}};
    phys_spatial_grid_insert(&grid, 99, &aabb);

    /* Query far away — should find nothing. */
    phys_aabb_t far_away = {.min = {.x = 1000, .y = 1000, .z = 1000},
                            .max = {.x = 1001, .y = 1001, .z = 1001}};
    uint32_t results[100];
    uint32_t count = phys_spatial_grid_query(&grid, &far_away, results, 100);
    ASSERT_INT_EQ(0, (int)count);

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_grid_query_dedup(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 4 * 1024 * 1024);

    phys_spatial_grid_t grid;
    phys_spatial_grid_init(&grid, 1024, 10.0f, &arena);

    /* Large AABB spanning multiple cells — body 5 inserted in many cells. */
    phys_aabb_t large = {.min = {.x = 0, .y = 0, .z = 0},
                         .max = {.x = 25, .y = 25, .z = 25}};
    phys_spatial_grid_insert(&grid, 5, &large);

    /* Query overlapping multiple cells. Body 5 must appear only once. */
    uint32_t results[100];
    uint32_t count = phys_spatial_grid_query(&grid, &large, results, 100);
    ASSERT_INT_EQ(1, (int)count);
    ASSERT_INT_EQ(5, (int)results[0]);

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_grid_clear(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 4 * 1024 * 1024);

    phys_spatial_grid_t grid;
    phys_spatial_grid_init(&grid, 1024, 10.0f, &arena);

    phys_aabb_t aabb = {.min = {.x = 5, .y = 5, .z = 5},
                        .max = {.x = 6, .y = 6, .z = 6}};
    phys_spatial_grid_insert(&grid, 42, &aabb);

    phys_spatial_grid_clear(&grid);

    uint32_t results[100];
    uint32_t count = phys_spatial_grid_query(&grid, &aabb, results, 100);
    ASSERT_INT_EQ(0, (int)count);

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_grid_many_bodies(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 4 * 1024 * 1024);

    phys_spatial_grid_t grid;
    phys_spatial_grid_init(&grid, 1024, 10.0f, &arena);

    /* Insert 100 bodies, each in a distinct cell region. */
    for (uint32_t i = 0; i < 100; ++i) {
        float base = (float)(i * 20); /* 20m apart, well beyond cell_size=10 */
        phys_aabb_t aabb = {.min = {.x = base, .y = 0, .z = 0},
                            .max = {.x = base + 1, .y = 1, .z = 1}};
        phys_spatial_grid_insert(&grid, i, &aabb);
    }

    /* Query each body individually — should find exactly that body. */
    for (uint32_t i = 0; i < 100; ++i) {
        float base = (float)(i * 20);
        phys_aabb_t aabb = {.min = {.x = base, .y = 0, .z = 0},
                            .max = {.x = base + 1, .y = 1, .z = 1}};
        uint32_t results[200];
        uint32_t count = phys_spatial_grid_query(&grid, &aabb, results, 200);
        /* Must find at least this body (hash collisions may add others). */
        ASSERT_TRUE(count >= 1);
        ASSERT_TRUE(results_contain(results, count, i));
    }

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_grid_null_safe(void) {
    /* NULL grid pointer — should not crash. */
    phys_spatial_grid_init(NULL, 1024, 10.0f, NULL);
    phys_spatial_grid_clear(NULL);
    phys_spatial_grid_insert(NULL, 0, NULL);

    uint32_t results[10];
    uint32_t count = phys_spatial_grid_query(NULL, NULL, results, 10);
    ASSERT_INT_EQ(0, (int)count);

    /* Valid grid but NULL AABB / out_indices. */
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 4 * 1024 * 1024);

    phys_spatial_grid_t grid;
    phys_spatial_grid_init(&grid, 1024, 10.0f, &arena);

    phys_spatial_grid_insert(&grid, 0, NULL);

    phys_aabb_t aabb = {.min = {.x = 0, .y = 0, .z = 0},
                        .max = {.x = 1, .y = 1, .z = 1}};
    count = phys_spatial_grid_query(&grid, &aabb, NULL, 10);
    ASSERT_INT_EQ(0, (int)count);

    count = phys_spatial_grid_query(&grid, &aabb, results, 0);
    ASSERT_INT_EQ(0, (int)count);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/* ── Test runner ─────────────────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct test_case TESTS[] = {
    {"grid_init",                    test_grid_init},
    {"grid_insert_single",           test_grid_insert_single},
    {"grid_insert_same_cell",        test_grid_insert_same_cell},
    {"grid_insert_different_cells",  test_grid_insert_different_cells},
    {"grid_large_aabb_spans_cells",  test_grid_large_aabb_spans_cells},
    {"grid_query_no_results",        test_grid_query_no_results},
    {"grid_query_dedup",             test_grid_query_dedup},
    {"grid_clear",                   test_grid_clear},
    {"grid_many_bodies",             test_grid_many_bodies},
    {"grid_null_safe",               test_grid_null_safe},
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0;
    for (size_t i = 0; i < total; ++i) {
        struct test_case *tc = &TESTS[i];
        printf("RUN %s\n", tc->name);
        int rc = tc->fn();
        if (rc == 0) {
            printf("OK %s\n", tc->name);
            passed++;
        } else {
            fprintf(stderr, "FAIL %s (rc=%d)\n", tc->name, rc);
            break;
        }
    }
    if (passed == total) {
        printf("All %zu tests passed\n", passed);
        return 0;
    }
    return 1;
}
