/**
 * @file edit_selection_tests.c
 * @brief Unit tests for the entity selection system.
 */

#include <stdio.h>
#include "ferrum/editor/edit_selection.h"

/* ----------------------------------------------------------------------- */
/* Test harness                                                             */
/* ----------------------------------------------------------------------- */

#define ASSERT_TRUE(expr)                                                    \
    do {                                                                     \
        if (!(expr)) {                                                       \
            fprintf(stderr, "  ASSERT_TRUE failed: %s (%s:%d)\n",            \
                    #expr, __FILE__, __LINE__);                               \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

#define ASSERT_UINT_EQ(a, b)                                                 \
    do {                                                                     \
        unsigned _a = (unsigned)(a), _b = (unsigned)(b);                     \
        if (_a != _b) {                                                      \
            fprintf(stderr, "  ASSERT_UINT_EQ failed: %u != %u (%s:%d)\n",   \
                    _a, _b, __FILE__, __LINE__);                              \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* ----------------------------------------------------------------------- */
/* Tests                                                                     */
/* ----------------------------------------------------------------------- */

static int test_init_destroy(void) {
    edit_selection_t sel;
    ASSERT_TRUE(edit_selection_init(&sel));
    ASSERT_UINT_EQ(edit_selection_count(&sel), 0);
    edit_selection_destroy(&sel);
    return 0;
}

static int test_add_single(void) {
    edit_selection_t sel;
    edit_selection_init(&sel);

    ASSERT_TRUE(edit_selection_add(&sel, 42));
    ASSERT_UINT_EQ(edit_selection_count(&sel), 1);
    ASSERT_TRUE(edit_selection_contains(&sel, 42));
    ASSERT_FALSE(edit_selection_contains(&sel, 43));

    edit_selection_destroy(&sel);
    return 0;
}

static int test_add_duplicate(void) {
    edit_selection_t sel;
    edit_selection_init(&sel);

    ASSERT_TRUE(edit_selection_add(&sel, 10));
    ASSERT_FALSE(edit_selection_add(&sel, 10)); /* Duplicate. */
    ASSERT_UINT_EQ(edit_selection_count(&sel), 1);

    edit_selection_destroy(&sel);
    return 0;
}

static int test_remove(void) {
    edit_selection_t sel;
    edit_selection_init(&sel);

    edit_selection_add(&sel, 1);
    edit_selection_add(&sel, 2);
    edit_selection_add(&sel, 3);
    ASSERT_UINT_EQ(edit_selection_count(&sel), 3);

    ASSERT_TRUE(edit_selection_remove(&sel, 2));
    ASSERT_UINT_EQ(edit_selection_count(&sel), 2);
    ASSERT_FALSE(edit_selection_contains(&sel, 2));
    ASSERT_TRUE(edit_selection_contains(&sel, 1));
    ASSERT_TRUE(edit_selection_contains(&sel, 3));

    /* Remove non-existent. */
    ASSERT_FALSE(edit_selection_remove(&sel, 99));

    edit_selection_destroy(&sel);
    return 0;
}

static int test_toggle(void) {
    edit_selection_t sel;
    edit_selection_init(&sel);

    /* Toggle on. */
    ASSERT_TRUE(edit_selection_toggle(&sel, 5));
    ASSERT_TRUE(edit_selection_contains(&sel, 5));

    /* Toggle off. */
    ASSERT_FALSE(edit_selection_toggle(&sel, 5));
    ASSERT_FALSE(edit_selection_contains(&sel, 5));
    ASSERT_UINT_EQ(edit_selection_count(&sel), 0);

    edit_selection_destroy(&sel);
    return 0;
}

static int test_clear(void) {
    edit_selection_t sel;
    edit_selection_init(&sel);

    for (uint32_t i = 0; i < 50; ++i) {
        edit_selection_add(&sel, i);
    }
    ASSERT_UINT_EQ(edit_selection_count(&sel), 50);

    edit_selection_clear(&sel);
    ASSERT_UINT_EQ(edit_selection_count(&sel), 0);

    edit_selection_destroy(&sel);
    return 0;
}

static int test_sorted_order(void) {
    edit_selection_t sel;
    edit_selection_init(&sel);

    /* Add in reverse order. */
    edit_selection_add(&sel, 100);
    edit_selection_add(&sel, 50);
    edit_selection_add(&sel, 200);
    edit_selection_add(&sel, 1);
    edit_selection_add(&sel, 75);

    const uint32_t *ids = edit_selection_ids(&sel);
    ASSERT_UINT_EQ(ids[0], 1);
    ASSERT_UINT_EQ(ids[1], 50);
    ASSERT_UINT_EQ(ids[2], 75);
    ASSERT_UINT_EQ(ids[3], 100);
    ASSERT_UINT_EQ(ids[4], 200);

    edit_selection_destroy(&sel);
    return 0;
}

static int test_many_entities(void) {
    edit_selection_t sel;
    edit_selection_init(&sel);

    /* Add 1000 entities. */
    for (uint32_t i = 0; i < 1000; ++i) {
        ASSERT_TRUE(edit_selection_add(&sel, i * 3)); /* Sparse IDs. */
    }
    ASSERT_UINT_EQ(edit_selection_count(&sel), 1000);

    /* Verify all present. */
    for (uint32_t i = 0; i < 1000; ++i) {
        ASSERT_TRUE(edit_selection_contains(&sel, i * 3));
    }

    /* Remove every other one. */
    for (uint32_t i = 0; i < 1000; i += 2) {
        ASSERT_TRUE(edit_selection_remove(&sel, i * 3));
    }
    ASSERT_UINT_EQ(edit_selection_count(&sel), 500);

    edit_selection_destroy(&sel);
    return 0;
}

static int test_version_tracking(void) {
    edit_selection_t sel;
    edit_selection_init(&sel);

    uint32_t v0 = sel.version;
    edit_selection_add(&sel, 1);
    uint32_t v1 = sel.version;
    ASSERT_TRUE(v1 > v0);

    edit_selection_remove(&sel, 1);
    uint32_t v2 = sel.version;
    ASSERT_TRUE(v2 > v1);

    /* No-op add (already removed) should not bump version. */
    uint32_t v3_pre = sel.version;
    edit_selection_remove(&sel, 1); /* Not present. */
    ASSERT_UINT_EQ(sel.version, v3_pre);

    edit_selection_destroy(&sel);
    return 0;
}

static int test_null_params(void) {
    ASSERT_FALSE(edit_selection_init(NULL));
    ASSERT_UINT_EQ(edit_selection_count(NULL), 0);
    ASSERT_FALSE(edit_selection_contains(NULL, 1));
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test runner                                                              */
/* ----------------------------------------------------------------------- */

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"init_destroy",     test_init_destroy},
    {"add_single",       test_add_single},
    {"add_duplicate",    test_add_duplicate},
    {"remove",           test_remove},
    {"toggle",           test_toggle},
    {"clear",            test_clear},
    {"sorted_order",     test_sorted_order},
    {"many_entities",    test_many_entities},
    {"version_tracking", test_version_tracking},
    {"null_params",      test_null_params},
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0;
    for (size_t i = 0; i < total; ++i) {
        struct test_case *tc = &TESTS[i];
        printf("RUN  %s\n", tc->name);
        int rc = tc->fn();
        if (rc == 0) {
            printf("OK   %s\n", tc->name);
            passed++;
        } else {
            fprintf(stderr, "FAIL %s (rc=%d)\n", tc->name, rc);
            break;
        }
    }
    printf("\n%zu / %zu tests passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
