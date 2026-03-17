/**
 * @file snap_mesh_cache_tests.c
 * @brief Tests for snap mesh cache (CPU-side geometry retention).
 */

#include "ferrum/editor/viewport/snap/snap_mesh_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define ASSERT(expr) do { \
    if (!(expr)) { \
        printf("  FAIL: %s (line %d)\n", #expr, __LINE__); \
        return 0; \
    } \
} while (0)

#define ASSERT_NEAR(a, b, eps) ASSERT(fabsf((a) - (b)) < (eps))

#define RUN(fn) do { \
    printf("RUN  %s\n", #fn); \
    if (fn()) printf("OK   %s\n", #fn); \
    else { printf("FAIL %s\n", #fn); fails++; } \
    total++; \
} while (0)

/* ---- Test helpers ---- */

/** Build a minimal 1-triangle mesh for testing. */
static void make_triangle_data(float *positions, float *normals,
                                uint32_t *indices) {
    /* Triangle at Y=0 plane. */
    positions[0] = 0.0f; positions[1] = 0.0f; positions[2] = 0.0f;
    positions[3] = 1.0f; positions[4] = 0.0f; positions[5] = 0.0f;
    positions[6] = 0.0f; positions[7] = 0.0f; positions[8] = 1.0f;

    normals[0] = 0.0f; normals[1] = 1.0f; normals[2] = 0.0f;
    normals[3] = 0.0f; normals[4] = 1.0f; normals[5] = 0.0f;
    normals[6] = 0.0f; normals[7] = 1.0f; normals[8] = 0.0f;

    indices[0] = 0; indices[1] = 1; indices[2] = 2;
}

/* ---- Tests ---- */

static int test_init_destroy(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 16);
    ASSERT(cache.capacity == 16);
    ASSERT(cache.meshes != NULL);
    snap_mesh_cache_destroy(&cache);
    ASSERT(cache.meshes == NULL);
    ASSERT(cache.capacity == 0);
    return 1;
}

static int test_insert_and_get(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 16);

    float positions[9], normals[9];
    uint32_t indices[3];
    make_triangle_data(positions, normals, indices);

    snap_mesh_cache_insert(&cache, 3, positions, normals, indices, 3, 3);

    ASSERT(snap_mesh_cache_has(&cache, 3));
    const snap_mesh_t *mesh = snap_mesh_cache_get(&cache, 3);
    ASSERT(mesh != NULL);
    ASSERT(mesh->vertex_count == 3);
    ASSERT(mesh->index_count == 3);
    ASSERT_NEAR(mesh->positions[3], 1.0f, 1e-6f); /* v1.x */
    ASSERT_NEAR(mesh->normals[1], 1.0f, 1e-6f);   /* v0.ny */
    ASSERT(mesh->indices[2] == 2);

    snap_mesh_cache_destroy(&cache);
    return 1;
}

static int test_get_empty_returns_null(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 8);

    ASSERT(!snap_mesh_cache_has(&cache, 0));
    ASSERT(snap_mesh_cache_get(&cache, 0) == NULL);
    ASSERT(snap_mesh_cache_get(&cache, 7) == NULL);

    snap_mesh_cache_destroy(&cache);
    return 1;
}

static int test_out_of_bounds_returns_null(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 4);

    ASSERT(snap_mesh_cache_get(&cache, 4) == NULL);
    ASSERT(snap_mesh_cache_get(&cache, 100) == NULL);
    ASSERT(!snap_mesh_cache_has(&cache, 4));

    snap_mesh_cache_destroy(&cache);
    return 1;
}

static int test_remove(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 16);

    float positions[9], normals[9];
    uint32_t indices[3];
    make_triangle_data(positions, normals, indices);

    snap_mesh_cache_insert(&cache, 5, positions, normals, indices, 3, 3);
    ASSERT(snap_mesh_cache_has(&cache, 5));

    snap_mesh_cache_remove(&cache, 5);
    ASSERT(!snap_mesh_cache_has(&cache, 5));
    ASSERT(snap_mesh_cache_get(&cache, 5) == NULL);

    snap_mesh_cache_destroy(&cache);
    return 1;
}

static int test_remove_nonexistent_safe(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 8);

    /* Should not crash. */
    snap_mesh_cache_remove(&cache, 0);
    snap_mesh_cache_remove(&cache, 7);
    snap_mesh_cache_remove(&cache, 100);

    snap_mesh_cache_destroy(&cache);
    return 1;
}

static int test_double_insert_replaces(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 16);

    float positions[9], normals[9];
    uint32_t indices[3];
    make_triangle_data(positions, normals, indices);

    snap_mesh_cache_insert(&cache, 2, positions, normals, indices, 3, 3);

    /* Insert different data at same slot. */
    float positions2[6] = {0, 0, 0, 1, 1, 1};
    float normals2[6] = {0, 1, 0, 0, 1, 0};
    uint32_t indices2[3] = {0, 1, 0};

    snap_mesh_cache_insert(&cache, 2, positions2, normals2, indices2, 2, 3);

    const snap_mesh_t *mesh = snap_mesh_cache_get(&cache, 2);
    ASSERT(mesh != NULL);
    ASSERT(mesh->vertex_count == 2);
    ASSERT_NEAR(mesh->positions[3], 1.0f, 1e-6f);

    snap_mesh_cache_destroy(&cache);
    return 1;
}

static int test_multiple_entities(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 16);

    float positions[9], normals[9];
    uint32_t indices[3];
    make_triangle_data(positions, normals, indices);

    snap_mesh_cache_insert(&cache, 0, positions, normals, indices, 3, 3);
    snap_mesh_cache_insert(&cache, 5, positions, normals, indices, 3, 3);
    snap_mesh_cache_insert(&cache, 15, positions, normals, indices, 3, 3);

    ASSERT(snap_mesh_cache_has(&cache, 0));
    ASSERT(snap_mesh_cache_has(&cache, 5));
    ASSERT(snap_mesh_cache_has(&cache, 15));
    ASSERT(!snap_mesh_cache_has(&cache, 1));

    snap_mesh_cache_remove(&cache, 5);
    ASSERT(snap_mesh_cache_has(&cache, 0));
    ASSERT(!snap_mesh_cache_has(&cache, 5));
    ASSERT(snap_mesh_cache_has(&cache, 15));

    snap_mesh_cache_destroy(&cache);
    return 1;
}

static int test_data_is_copied(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 16);

    float positions[9], normals[9];
    uint32_t indices[3];
    make_triangle_data(positions, normals, indices);

    snap_mesh_cache_insert(&cache, 0, positions, normals, indices, 3, 3);

    /* Modify original data — cache should be independent. */
    positions[0] = 999.0f;
    normals[1] = -1.0f;
    indices[0] = 99;

    const snap_mesh_t *mesh = snap_mesh_cache_get(&cache, 0);
    ASSERT_NEAR(mesh->positions[0], 0.0f, 1e-6f);
    ASSERT_NEAR(mesh->normals[1], 1.0f, 1e-6f);
    ASSERT(mesh->indices[0] == 0);

    snap_mesh_cache_destroy(&cache);
    return 1;
}

/* ---- Main ---- */

int main(void) {
    int total = 0, fails = 0;
    printf("=== snap_mesh_cache tests ===\n");

    RUN(test_init_destroy);
    RUN(test_insert_and_get);
    RUN(test_get_empty_returns_null);
    RUN(test_out_of_bounds_returns_null);
    RUN(test_remove);
    RUN(test_remove_nonexistent_safe);
    RUN(test_double_insert_replaces);
    RUN(test_multiple_entities);
    RUN(test_data_is_copied);

    printf("\n%d passed, %d failed\n", total - fails, fails);
    return fails > 0 ? 1 : 0;
}
