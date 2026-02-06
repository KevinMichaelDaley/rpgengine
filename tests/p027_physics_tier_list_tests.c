#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/tier_list.h"

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

/* ── Tests ──────────────────────────────────────────────────────── */

static int test_tier_enum_values(void) {
    ASSERT_INT_EQ(0, PHYS_TIER_0_DIRECT);
    ASSERT_INT_EQ(1, PHYS_TIER_1_NEAR);
    ASSERT_INT_EQ(2, PHYS_TIER_2_VISIBLE);
    ASSERT_INT_EQ(3, PHYS_TIER_3_WORLD);
    ASSERT_INT_EQ(4, PHYS_TIER_4_BACKGROUND);
    ASSERT_INT_EQ(5, PHYS_TIER_5_SLEEPING);
    ASSERT_INT_EQ(6, PHYS_TIER_COUNT);
    return 0;
}

static int test_tier_lists_init(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024 * 1024);

    phys_tier_lists_t lists;
    phys_tier_lists_init(&lists, &arena, 100);

    for (int t = 0; t < PHYS_TIER_COUNT; ++t) {
        ASSERT_PTR_NOT_NULL(lists.tiers[t].indices);
        ASSERT_INT_EQ(0, (int)lists.tiers[t].count);
        ASSERT_INT_EQ(100, (int)lists.tiers[t].capacity);
    }

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_tier_list_add_single(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024 * 1024);

    phys_tier_lists_t lists;
    phys_tier_lists_init(&lists, &arena, 100);

    phys_tier_list_add(&lists.tiers[PHYS_TIER_0_DIRECT], 42);
    ASSERT_INT_EQ(1, (int)lists.tiers[PHYS_TIER_0_DIRECT].count);
    ASSERT_INT_EQ(42, (int)lists.tiers[PHYS_TIER_0_DIRECT].indices[0]);

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_tier_list_add_multiple(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024 * 1024);

    phys_tier_lists_t lists;
    phys_tier_lists_init(&lists, &arena, 100);

    for (uint32_t i = 0; i < 10; ++i) {
        phys_tier_list_add(&lists.tiers[PHYS_TIER_1_NEAR], i * 3);
    }
    ASSERT_INT_EQ(10, (int)lists.tiers[PHYS_TIER_1_NEAR].count);

    for (uint32_t i = 0; i < 10; ++i) {
        ASSERT_INT_EQ((int)(i * 3), (int)lists.tiers[PHYS_TIER_1_NEAR].indices[i]);
    }

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_tier_list_clear(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024 * 1024);

    phys_tier_lists_t lists;
    phys_tier_lists_init(&lists, &arena, 100);

    phys_tier_list_add(&lists.tiers[PHYS_TIER_2_VISIBLE], 7);
    phys_tier_list_add(&lists.tiers[PHYS_TIER_2_VISIBLE], 8);
    ASSERT_INT_EQ(2, (int)lists.tiers[PHYS_TIER_2_VISIBLE].count);

    phys_tier_list_clear(&lists.tiers[PHYS_TIER_2_VISIBLE]);
    ASSERT_INT_EQ(0, (int)lists.tiers[PHYS_TIER_2_VISIBLE].count);

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_tier_lists_clear_all(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024 * 1024);

    phys_tier_lists_t lists;
    phys_tier_lists_init(&lists, &arena, 100);

    phys_tier_list_add(&lists.tiers[PHYS_TIER_0_DIRECT], 1);
    phys_tier_list_add(&lists.tiers[PHYS_TIER_3_WORLD], 2);
    phys_tier_list_add(&lists.tiers[PHYS_TIER_5_SLEEPING], 3);

    phys_tier_lists_clear_all(&lists);

    for (int t = 0; t < PHYS_TIER_COUNT; ++t) {
        ASSERT_INT_EQ(0, (int)lists.tiers[t].count);
    }

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_tier_list_at_capacity(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024 * 1024);

    uint32_t max_bodies = 8;
    phys_tier_lists_t lists;
    phys_tier_lists_init(&lists, &arena, max_bodies);

    /* Fill to capacity. */
    for (uint32_t i = 0; i < max_bodies; ++i) {
        phys_tier_list_add(&lists.tiers[PHYS_TIER_0_DIRECT], i);
    }
    ASSERT_INT_EQ((int)max_bodies, (int)lists.tiers[PHYS_TIER_0_DIRECT].count);

    /* Next add should be silently ignored. */
    phys_tier_list_add(&lists.tiers[PHYS_TIER_0_DIRECT], 999);
    ASSERT_INT_EQ((int)max_bodies, (int)lists.tiers[PHYS_TIER_0_DIRECT].count);

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_total_active_excludes_sleeping(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024 * 1024);

    phys_tier_lists_t lists;
    phys_tier_lists_init(&lists, &arena, 100);

    /* Add bodies to each active tier. */
    phys_tier_list_add(&lists.tiers[PHYS_TIER_0_DIRECT], 1);
    phys_tier_list_add(&lists.tiers[PHYS_TIER_1_NEAR], 2);
    phys_tier_list_add(&lists.tiers[PHYS_TIER_2_VISIBLE], 3);
    phys_tier_list_add(&lists.tiers[PHYS_TIER_3_WORLD], 4);
    phys_tier_list_add(&lists.tiers[PHYS_TIER_4_BACKGROUND], 5);

    /* Add sleeping bodies — should not be counted. */
    phys_tier_list_add(&lists.tiers[PHYS_TIER_5_SLEEPING], 10);
    phys_tier_list_add(&lists.tiers[PHYS_TIER_5_SLEEPING], 11);
    phys_tier_list_add(&lists.tiers[PHYS_TIER_5_SLEEPING], 12);

    ASSERT_INT_EQ(5, (int)phys_tier_lists_total_active(&lists));

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_total_active_empty(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024 * 1024);

    phys_tier_lists_t lists;
    phys_tier_lists_init(&lists, &arena, 100);

    ASSERT_INT_EQ(0, (int)phys_tier_lists_total_active(&lists));

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_tier_list_null_safe(void) {
    /* NULL pointers must not crash. */
    phys_tier_list_add(NULL, 0);
    phys_tier_list_clear(NULL);
    phys_tier_lists_clear_all(NULL);
    ASSERT_INT_EQ(0, (int)phys_tier_lists_total_active(NULL));
    phys_tier_lists_init(NULL, NULL, 0);
    return 0;
}

/* ── Test runner ─────────────────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct test_case TESTS[] = {
    {"tier_enum_values",             test_tier_enum_values},
    {"tier_lists_init",              test_tier_lists_init},
    {"tier_list_add_single",         test_tier_list_add_single},
    {"tier_list_add_multiple",       test_tier_list_add_multiple},
    {"tier_list_clear",              test_tier_list_clear},
    {"tier_lists_clear_all",         test_tier_lists_clear_all},
    {"tier_list_at_capacity",        test_tier_list_at_capacity},
    {"total_active_excludes_sleeping", test_total_active_excludes_sleeping},
    {"total_active_empty",           test_total_active_empty},
    {"tier_list_null_safe",          test_tier_list_null_safe},
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
