/**
 * @file snap_decompose_cache_tests.c
 * @brief Tests for the convex decomposition result cache.
 */

#include "ferrum/editor/viewport/snap/snap_decompose_cache.h"
#include "ferrum/physics/convex_decompose.h"

#include <stdio.h>
#include <string.h>

static int g_pass, g_fail;

#define ASSERT(cond) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; \
           fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

/** @brief Build a dummy decompose result with a given hull count. */
static void build_dummy_result(phys_decompose_result_t *r, uint32_t hull_count) {
    memset(r, 0, sizeof(*r));
    r->hull_count = hull_count;
    for (uint32_t i = 0; i < hull_count && i < PHYS_DECOMPOSE_MAX_HULLS; i++) {
        r->hulls[i].vertex_count = 4 + i;
        r->hulls[i].face_count = 4;
    }
}

static void test_init_destroy(void) {
    snap_decompose_cache_t cache;
    snap_decompose_cache_init(&cache, 32);
    ASSERT(cache.capacity == 32);
    snap_decompose_cache_destroy(&cache);
}

static void test_set_and_get(void) {
    snap_decompose_cache_t cache;
    snap_decompose_cache_init(&cache, 16);

    phys_decompose_result_t r;
    build_dummy_result(&r, 3);

    snap_decompose_cache_set(&cache, 5, &r);
    ASSERT(snap_decompose_cache_has(&cache, 5));

    const phys_decompose_result_t *got = snap_decompose_cache_get(&cache, 5);
    ASSERT(got != NULL);
    if (got) {
        ASSERT(got->hull_count == 3);
        ASSERT(got->hulls[0].vertex_count == 4);
        ASSERT(got->hulls[1].vertex_count == 5);
        ASSERT(got->hulls[2].vertex_count == 6);
    }

    snap_decompose_cache_destroy(&cache);
}

static void test_get_empty_returns_null(void) {
    snap_decompose_cache_t cache;
    snap_decompose_cache_init(&cache, 16);

    ASSERT(!snap_decompose_cache_has(&cache, 0));
    ASSERT(snap_decompose_cache_get(&cache, 0) == NULL);
    ASSERT(snap_decompose_cache_get(&cache, 15) == NULL);

    snap_decompose_cache_destroy(&cache);
}

static void test_out_of_bounds(void) {
    snap_decompose_cache_t cache;
    snap_decompose_cache_init(&cache, 8);

    ASSERT(!snap_decompose_cache_has(&cache, 100));
    ASSERT(snap_decompose_cache_get(&cache, 100) == NULL);

    /* Set out of bounds should be a no-op. */
    phys_decompose_result_t r;
    build_dummy_result(&r, 1);
    snap_decompose_cache_set(&cache, 100, &r);
    ASSERT(!snap_decompose_cache_has(&cache, 100));

    snap_decompose_cache_destroy(&cache);
}

static void test_remove(void) {
    snap_decompose_cache_t cache;
    snap_decompose_cache_init(&cache, 16);

    phys_decompose_result_t r;
    build_dummy_result(&r, 2);
    snap_decompose_cache_set(&cache, 3, &r);
    ASSERT(snap_decompose_cache_has(&cache, 3));

    snap_decompose_cache_remove(&cache, 3);
    ASSERT(!snap_decompose_cache_has(&cache, 3));
    ASSERT(snap_decompose_cache_get(&cache, 3) == NULL);

    snap_decompose_cache_destroy(&cache);
}

static void test_overwrite(void) {
    snap_decompose_cache_t cache;
    snap_decompose_cache_init(&cache, 16);

    phys_decompose_result_t r1, r2;
    build_dummy_result(&r1, 2);
    build_dummy_result(&r2, 5);

    snap_decompose_cache_set(&cache, 7, &r1);
    ASSERT(snap_decompose_cache_get(&cache, 7)->hull_count == 2);

    snap_decompose_cache_set(&cache, 7, &r2);
    ASSERT(snap_decompose_cache_get(&cache, 7)->hull_count == 5);

    snap_decompose_cache_destroy(&cache);
}

static void test_remove_nonexistent_safe(void) {
    snap_decompose_cache_t cache;
    snap_decompose_cache_init(&cache, 16);

    /* Should not crash. */
    snap_decompose_cache_remove(&cache, 0);
    snap_decompose_cache_remove(&cache, 15);
    snap_decompose_cache_remove(&cache, 100); /* out of bounds */

    snap_decompose_cache_destroy(&cache);
}

static void test_data_is_copied(void) {
    snap_decompose_cache_t cache;
    snap_decompose_cache_init(&cache, 16);

    phys_decompose_result_t r;
    build_dummy_result(&r, 1);
    snap_decompose_cache_set(&cache, 0, &r);

    /* Modify the source — cached copy should be unaffected. */
    r.hull_count = 99;
    const phys_decompose_result_t *got = snap_decompose_cache_get(&cache, 0);
    ASSERT(got != NULL);
    if (got) {
        ASSERT(got->hull_count == 1);
    }

    snap_decompose_cache_destroy(&cache);
}

int main(void) {
    printf("snap_decompose_cache_tests:\n");

    test_init_destroy();
    test_set_and_get();
    test_get_empty_returns_null();
    test_out_of_bounds();
    test_remove();
    test_overwrite();
    test_remove_nonexistent_safe();
    test_data_is_copied();

    printf("snap_decompose_cache_tests: %d passed, %d failed\n",
           g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
