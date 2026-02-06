/**
 * @file p030_physics_island_tests.c
 * @brief Unit tests for island structures and union-find (phys-011).
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "ferrum/physics/island.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/phys_pool.h"

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

#define ASSERT_UINT_EQ(exp, act)                                                                         \
    do {                                                                                                 \
        if ((exp) != (act)) {                                                                            \
            fprintf(stderr, "ASSERT_UINT_EQ failed: %s:%d: expected %u got %u\n", __FILE__, __LINE__,    \
                    (unsigned)(exp), (unsigned)(act));                                                    \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

/* ── Helper: create a simple constraint with only body indices ── */

static phys_constraint_t make_constraint(uint32_t a, uint32_t b) {
    phys_constraint_t c;
    memset(&c, 0, sizeof(c));
    c.body_a = a;
    c.body_b = b;
    return c;
}

/* ── Tests ──────────────────────────────────────────────────────── */

static int test_island_list_init(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_island_list_t list;
    phys_island_list_init(&list, &arena, 16, 8);

    ASSERT_UINT_EQ(0, list.count);
    ASSERT_UINT_EQ(8, list.capacity);
    ASSERT_UINT_EQ(16, list.uf_size);
    ASSERT_TRUE(list.islands != NULL);
    ASSERT_TRUE(list.parent != NULL);
    ASSERT_TRUE(list.rank != NULL);

    /* parent[i] == i after init */
    for (uint32_t i = 0; i < 16; i++) {
        ASSERT_UINT_EQ(i, list.parent[i]);
        ASSERT_UINT_EQ(0, list.rank[i]);
    }

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_uf_find_self(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_island_list_t list;
    phys_island_list_init(&list, &arena, 10, 4);

    /* Each element is its own root initially. */
    for (uint32_t i = 0; i < 10; i++) {
        ASSERT_UINT_EQ(i, phys_uf_find(&list, i));
    }

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_uf_union_and_find(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_island_list_t list;
    phys_island_list_init(&list, &arena, 10, 4);

    phys_uf_union(&list, 0, 1);
    ASSERT_UINT_EQ(phys_uf_find(&list, 0), phys_uf_find(&list, 1));

    /* 2 and 3 should still be separate. */
    ASSERT_TRUE(phys_uf_find(&list, 0) != phys_uf_find(&list, 2));
    ASSERT_TRUE(phys_uf_find(&list, 2) != phys_uf_find(&list, 3));

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_uf_path_compression(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_island_list_t list;
    phys_island_list_init(&list, &arena, 10, 4);

    /* Build a chain: 0-1, 1-2, 2-3, 3-4 */
    phys_uf_union(&list, 0, 1);
    phys_uf_union(&list, 1, 2);
    phys_uf_union(&list, 2, 3);
    phys_uf_union(&list, 3, 4);

    /* All should share the same root. */
    uint32_t root = phys_uf_find(&list, 4);
    for (uint32_t i = 0; i < 5; i++) {
        ASSERT_UINT_EQ(root, phys_uf_find(&list, i));
    }

    /* 5 should be separate. */
    ASSERT_TRUE(phys_uf_find(&list, 5) != root);

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_build_single_island(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    /* 4 bodies in a chain: 0-1, 1-2, 2-3 */
    phys_constraint_t constraints[3];
    constraints[0] = make_constraint(0, 1);
    constraints[1] = make_constraint(1, 2);
    constraints[2] = make_constraint(2, 3);

    phys_island_list_t list;
    phys_island_list_init(&list, &arena, 8, 8);
    phys_island_list_build(&list, constraints, 3, 4, &arena);

    ASSERT_UINT_EQ(1, list.count);
    ASSERT_UINT_EQ(4, list.islands[0].body_count);
    ASSERT_UINT_EQ(3, list.islands[0].constraint_count);
    ASSERT_TRUE(list.islands[0].sleeping == false);

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_build_two_islands(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    /* Island A: 0-1-2, Island B: 3-4 */
    phys_constraint_t constraints[3];
    constraints[0] = make_constraint(0, 1);
    constraints[1] = make_constraint(1, 2);
    constraints[2] = make_constraint(3, 4);

    phys_island_list_t list;
    phys_island_list_init(&list, &arena, 8, 8);
    phys_island_list_build(&list, constraints, 3, 5, &arena);

    ASSERT_UINT_EQ(2, list.count);

    /* Find which island has 3 bodies and which has 2. */
    uint32_t big_idx = (list.islands[0].body_count == 3) ? 0 : 1;
    uint32_t small_idx = 1 - big_idx;

    ASSERT_UINT_EQ(3, list.islands[big_idx].body_count);
    ASSERT_UINT_EQ(2, list.islands[big_idx].constraint_count);
    ASSERT_UINT_EQ(2, list.islands[small_idx].body_count);
    ASSERT_UINT_EQ(1, list.islands[small_idx].constraint_count);

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_build_no_constraints(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_island_list_t list;
    phys_island_list_init(&list, &arena, 8, 8);
    phys_island_list_build(&list, NULL, 0, 3, &arena);

    /* No constraints → no islands (isolated bodies are not grouped). */
    ASSERT_UINT_EQ(0, list.count);

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_build_constraints_assigned(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    /* Two islands: {0,1,2} with constraints 0,1 and {3,4} with constraint 2 */
    phys_constraint_t constraints[3];
    constraints[0] = make_constraint(0, 1);
    constraints[1] = make_constraint(1, 2);
    constraints[2] = make_constraint(3, 4);

    phys_island_list_t list;
    phys_island_list_init(&list, &arena, 8, 8);
    phys_island_list_build(&list, constraints, 3, 5, &arena);

    ASSERT_UINT_EQ(2, list.count);

    /* Total constraints across all islands must equal input constraint count. */
    uint32_t total_constraints = 0;
    for (uint32_t i = 0; i < list.count; i++) {
        total_constraints += list.islands[i].constraint_count;
        ASSERT_TRUE(list.islands[i].constraint_indices != NULL);
    }
    ASSERT_UINT_EQ(3, total_constraints);

    /* Each constraint index must be valid (< 3). */
    for (uint32_t i = 0; i < list.count; i++) {
        for (uint32_t j = 0; j < list.islands[i].constraint_count; j++) {
            ASSERT_TRUE(list.islands[i].constraint_indices[j] < 3);
        }
    }

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_island_clear(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_constraint_t constraints[2];
    constraints[0] = make_constraint(0, 1);
    constraints[1] = make_constraint(2, 3);

    phys_island_list_t list;
    phys_island_list_init(&list, &arena, 8, 8);
    phys_island_list_build(&list, constraints, 2, 4, &arena);

    ASSERT_TRUE(list.count > 0);

    phys_island_list_clear(&list);
    ASSERT_UINT_EQ(0, list.count);

    /* Union-find should be reset. */
    for (uint32_t i = 0; i < list.uf_size; i++) {
        ASSERT_UINT_EQ(i, list.parent[i]);
        ASSERT_UINT_EQ(0, list.rank[i]);
    }

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_large_graph(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 256 * 1024);

    /* 50 bodies, 5 chains of 10 bodies each: 0-9, 10-19, 20-29, 30-39, 40-49 */
    phys_constraint_t constraints[45]; /* 9 constraints per chain * 5 chains */
    uint32_t ci = 0;
    for (uint32_t chain = 0; chain < 5; chain++) {
        uint32_t base = chain * 10;
        for (uint32_t j = 0; j < 9; j++) {
            constraints[ci++] = make_constraint(base + j, base + j + 1);
        }
    }
    ASSERT_UINT_EQ(45, ci);

    phys_island_list_t list;
    phys_island_list_init(&list, &arena, 64, 16);
    phys_island_list_build(&list, constraints, 45, 50, &arena);

    ASSERT_UINT_EQ(5, list.count);

    /* Each island should have 10 bodies and 9 constraints. */
    for (uint32_t i = 0; i < 5; i++) {
        ASSERT_UINT_EQ(10, list.islands[i].body_count);
        ASSERT_UINT_EQ(9, list.islands[i].constraint_count);
    }

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_null_safe(void) {
    /* All functions with NULL should not crash. */
    phys_island_list_init(NULL, NULL, 0, 0);
    phys_island_list_clear(NULL);
    phys_island_list_build(NULL, NULL, 0, 0, NULL);

    /* uf_find with NULL returns x. */
    ASSERT_UINT_EQ(5, phys_uf_find(NULL, 5));

    /* uf_union with NULL should not crash. */
    phys_uf_union(NULL, 0, 1);

    /* Out-of-bounds find returns x. */
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 8 * 1024);

    phys_island_list_t list;
    phys_island_list_init(&list, &arena, 4, 2);
    ASSERT_UINT_EQ(99, phys_uf_find(&list, 99));

    phys_frame_arena_destroy(&arena);
    return 0;
}

/* ── Test runner ────────────────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct test_case TESTS[] = {
    {"island_list_init",          test_island_list_init},
    {"uf_find_self",              test_uf_find_self},
    {"uf_union_and_find",         test_uf_union_and_find},
    {"uf_path_compression",       test_uf_path_compression},
    {"build_single_island",       test_build_single_island},
    {"build_two_islands",         test_build_two_islands},
    {"build_no_constraints",      test_build_no_constraints},
    {"build_constraints_assigned", test_build_constraints_assigned},
    {"island_clear",              test_island_clear},
    {"large_graph",               test_large_graph},
    {"null_safe",                 test_null_safe},
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
