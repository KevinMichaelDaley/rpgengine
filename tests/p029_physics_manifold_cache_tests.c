/**
 * @file p029_physics_manifold_cache_tests.c
 * @brief Unit tests for persistent manifold cache (phys-009).
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/manifold_cache.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond)                                                           \
    do {                                                                            \
        if (!(cond)) {                                                              \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n",                     \
                    __FILE__, __LINE__, #cond);                                     \
            return 1;                                                               \
        }                                                                           \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                                     \
    do {                                                                            \
        if ((exp) != (act)) {                                                       \
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d got %d\n",   \
                    __FILE__, __LINE__, (int)(exp), (int)(act));                     \
            return 1;                                                               \
        }                                                                           \
    } while (0)

#define ASSERT_PTR_NULL(ptr)                                                        \
    do {                                                                            \
        if ((ptr) != NULL) {                                                        \
            fprintf(stderr, "ASSERT_PTR_NULL failed: %s:%d\n",                     \
                    __FILE__, __LINE__);                                             \
            return 1;                                                               \
        }                                                                           \
    } while (0)

#define ASSERT_PTR_NOT_NULL(ptr)                                                    \
    do {                                                                            \
        if ((ptr) == NULL) {                                                        \
            fprintf(stderr, "ASSERT_PTR_NOT_NULL failed: %s:%d\n",                 \
                    __FILE__, __LINE__);                                             \
            return 1;                                                               \
        }                                                                           \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, eps)                                            \
    do {                                                                            \
        float _e = (float)(exp);                                                    \
        float _a = (float)(act);                                                    \
        if ((_e - _a) > (eps) || (_a - _e) > (eps)) {                              \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: "                    \
                    "expected %f got %f\n",                                          \
                    __FILE__, __LINE__, (double)_e, (double)_a);                     \
            return 1;                                                               \
        }                                                                           \
    } while (0)

/* ── Tests ──────────────────────────────────────────────────────── */

static int test_cache_init(void) {
    phys_manifold_cache_t cache;
    ASSERT_INT_EQ(0, phys_manifold_cache_init(&cache, 64));
    ASSERT_INT_EQ(0, (int)phys_manifold_cache_count(&cache));
    phys_manifold_cache_destroy(&cache);
    return 0;
}

static int test_cache_get_or_create(void) {
    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, 64);

    phys_manifold_t *m = phys_manifold_cache_get_or_create(&cache, 1, 2, 0);
    ASSERT_PTR_NOT_NULL(m);
    ASSERT_INT_EQ(1, (int)phys_manifold_cache_count(&cache));

    phys_manifold_cache_destroy(&cache);
    return 0;
}

static int test_cache_find_existing(void) {
    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, 64);

    phys_manifold_t *created = phys_manifold_cache_get_or_create(&cache, 5, 10, 0);
    phys_manifold_t *found   = phys_manifold_cache_find(&cache, 5, 10);
    ASSERT_TRUE(created == found);

    phys_manifold_cache_destroy(&cache);
    return 0;
}

static int test_cache_find_reverse_order(void) {
    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, 64);

    phys_manifold_t *created = phys_manifold_cache_get_or_create(&cache, 5, 10, 0);
    /* Lookup with reversed body order should return the same entry. */
    phys_manifold_t *found   = phys_manifold_cache_find(&cache, 10, 5);
    ASSERT_TRUE(created == found);

    phys_manifold_cache_destroy(&cache);
    return 0;
}

static int test_cache_find_not_found(void) {
    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, 64);

    ASSERT_PTR_NULL(phys_manifold_cache_find(&cache, 99, 100));

    phys_manifold_cache_destroy(&cache);
    return 0;
}

static int test_cache_warmstart_persists(void) {
    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, 64);

    phys_manifold_t *m = phys_manifold_cache_get_or_create(&cache, 1, 2, 0);
    ASSERT_PTR_NOT_NULL(m);

    /* Simulate solver writing warmstart data. */
    m->normal_impulse[0] = 42.0f;
    m->tangent_impulse[0][0] = 7.5f;

    /* Re-find and verify the data persists. */
    phys_manifold_t *m2 = phys_manifold_cache_find(&cache, 1, 2);
    ASSERT_PTR_NOT_NULL(m2);
    ASSERT_FLOAT_NEAR(42.0f, m2->normal_impulse[0], 1e-6f);
    ASSERT_FLOAT_NEAR(7.5f, m2->tangent_impulse[0][0], 1e-6f);

    phys_manifold_cache_destroy(&cache);
    return 0;
}

static int test_cache_multiple_pairs(void) {
    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, 64);

    /* Insert 10 distinct body pairs. */
    for (uint32_t i = 0; i < 10; i++) {
        phys_manifold_t *m = phys_manifold_cache_get_or_create(
            &cache, i, i + 100, 0);
        ASSERT_PTR_NOT_NULL(m);
    }
    ASSERT_INT_EQ(10, (int)phys_manifold_cache_count(&cache));

    /* Verify all are findable. */
    for (uint32_t i = 0; i < 10; i++) {
        phys_manifold_t *m = phys_manifold_cache_find(&cache, i, i + 100);
        ASSERT_PTR_NOT_NULL(m);
    }

    phys_manifold_cache_destroy(&cache);
    return 0;
}

static int test_cache_expire_old(void) {
    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, 64);

    /* Create at tick 1. */
    phys_manifold_cache_get_or_create(&cache, 1, 2, 1);
    ASSERT_INT_EQ(1, (int)phys_manifold_cache_count(&cache));

    /* Expire at tick 10 with max_age 3 → age = 9 > 3 → removed. */
    phys_manifold_cache_expire(&cache, 10, 3);
    ASSERT_INT_EQ(0, (int)phys_manifold_cache_count(&cache));
    ASSERT_PTR_NULL(phys_manifold_cache_find(&cache, 1, 2));

    phys_manifold_cache_destroy(&cache);
    return 0;
}

static int test_cache_expire_keeps_recent(void) {
    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, 64);

    /* Create at tick 5. */
    phys_manifold_cache_get_or_create(&cache, 1, 2, 5);

    /* Expire at tick 10 with max_age 6 → age = 5, not > 6 → kept. */
    phys_manifold_cache_expire(&cache, 10, 6);
    ASSERT_INT_EQ(1, (int)phys_manifold_cache_count(&cache));

    phys_manifold_cache_destroy(&cache);
    return 0;
}

static int test_cache_touch_prevents_expire(void) {
    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, 64);

    /* Create at tick 1. */
    phys_manifold_cache_get_or_create(&cache, 1, 2, 1);

    /* Touch at tick 8. */
    phys_manifold_cache_touch(&cache, 1, 2, 8);

    /* Expire at tick 10 with max_age 3 → age from touch = 2, not > 3 → kept. */
    phys_manifold_cache_expire(&cache, 10, 3);
    ASSERT_INT_EQ(1, (int)phys_manifold_cache_count(&cache));

    phys_manifold_cache_destroy(&cache);
    return 0;
}

static int test_cache_hash_collision(void) {
    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, 256);

    /* Insert 100 pairs — all must be findable despite hash collisions. */
    for (uint32_t i = 0; i < 100; i++) {
        phys_manifold_t *m = phys_manifold_cache_get_or_create(
            &cache, i, i + 1000, 0);
        ASSERT_PTR_NOT_NULL(m);
    }
    ASSERT_INT_EQ(100, (int)phys_manifold_cache_count(&cache));

    for (uint32_t i = 0; i < 100; i++) {
        phys_manifold_t *m = phys_manifold_cache_find(&cache, i, i + 1000);
        ASSERT_PTR_NOT_NULL(m);
    }

    phys_manifold_cache_destroy(&cache);
    return 0;
}

static int test_cache_capacity_full(void) {
    uint32_t cap = 16;
    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, cap);

    /* Fill to capacity. */
    for (uint32_t i = 0; i < cap; i++) {
        phys_manifold_t *m = phys_manifold_cache_get_or_create(
            &cache, i, i + 500, 0);
        ASSERT_PTR_NOT_NULL(m);
    }
    ASSERT_INT_EQ((int)cap, (int)phys_manifold_cache_count(&cache));

    /* Next insert should return NULL. */
    phys_manifold_t *overflow = phys_manifold_cache_get_or_create(
        &cache, 999, 1000, 0);
    ASSERT_PTR_NULL(overflow);

    /* All original entries must still be findable. */
    for (uint32_t i = 0; i < cap; i++) {
        ASSERT_PTR_NOT_NULL(phys_manifold_cache_find(&cache, i, i + 500));
    }

    phys_manifold_cache_destroy(&cache);
    return 0;
}

static int test_cache_null_safe(void) {
    /* All functions must handle NULL cache without crashing. */
    ASSERT_INT_EQ(-1, phys_manifold_cache_init(NULL, 64));
    phys_manifold_cache_destroy(NULL);
    ASSERT_PTR_NULL(phys_manifold_cache_find(NULL, 1, 2));
    ASSERT_PTR_NULL(phys_manifold_cache_get_or_create(NULL, 1, 2, 0));
    phys_manifold_cache_expire(NULL, 10, 3);
    phys_manifold_cache_touch(NULL, 1, 2, 5);
    ASSERT_INT_EQ(0, (int)phys_manifold_cache_count(NULL));
    return 0;
}

static int test_cache_count(void) {
    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, 64);

    ASSERT_INT_EQ(0, (int)phys_manifold_cache_count(&cache));

    phys_manifold_cache_get_or_create(&cache, 1, 2, 0);
    ASSERT_INT_EQ(1, (int)phys_manifold_cache_count(&cache));

    phys_manifold_cache_get_or_create(&cache, 3, 4, 0);
    ASSERT_INT_EQ(2, (int)phys_manifold_cache_count(&cache));

    /* Re-accessing an existing pair should not change the count. */
    phys_manifold_cache_get_or_create(&cache, 1, 2, 1);
    ASSERT_INT_EQ(2, (int)phys_manifold_cache_count(&cache));

    phys_manifold_cache_destroy(&cache);
    return 0;
}

/* ── Test runner ────────────────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct test_case TESTS[] = {
    {"cache_init",                  test_cache_init},
    {"cache_get_or_create",         test_cache_get_or_create},
    {"cache_find_existing",         test_cache_find_existing},
    {"cache_find_reverse_order",    test_cache_find_reverse_order},
    {"cache_find_not_found",        test_cache_find_not_found},
    {"cache_warmstart_persists",    test_cache_warmstart_persists},
    {"cache_multiple_pairs",        test_cache_multiple_pairs},
    {"cache_expire_old",            test_cache_expire_old},
    {"cache_expire_keeps_recent",   test_cache_expire_keeps_recent},
    {"cache_touch_prevents_expire", test_cache_touch_prevents_expire},
    {"cache_hash_collision",        test_cache_hash_collision},
    {"cache_capacity_full",         test_cache_capacity_full},
    {"cache_null_safe",             test_cache_null_safe},
    {"cache_count",                 test_cache_count},
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
