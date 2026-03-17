/**
 * @file collision_mesh_asset_tests.c
 * @brief Tests for collision mesh asset storage.
 *
 * Verifies the collision_mesh_store: init/destroy, set/get/remove/has,
 * disk I/O round-trip, and edge cases (NULL, out-of-range, double-set).
 */

#include "ferrum/asset/collision_mesh_asset.h"
#include "ferrum/editor/mesh/mesh_slot.h"
#include "ferrum/editor/mesh/mesh_vao_format.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ASSERT(expr) do { \
    if (!(expr)) { \
        printf("  FAIL: %s (line %d)\n", #expr, __LINE__); \
        return 0; \
    } \
} while (0)

#define RUN(fn) do { \
    printf("RUN  %s\n", #fn); \
    if (fn()) printf("OK   %s\n", #fn); \
    else { printf("FAIL %s\n", #fn); fails++; } \
    total++; \
} while (0)

/* ---- Helpers ---- */

/** Build a minimal FVMA blob for testing. */
static bool build_test_fvma_(uint8_t **out, size_t *out_size,
                              float x0, float y0, float z0) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 4, 12);

    float p0[3] = {x0, y0, z0};
    float p1[3] = {x0 + 1, y0, z0};
    float p2[3] = {x0, y0 + 1, z0};
    float n[3]  = {0, 0, 1};
    mesh_slot_add_vertex(&slot, p0, n);
    mesh_slot_add_vertex(&slot, p1, n);
    mesh_slot_add_vertex(&slot, p2, n);
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);

    uint32_t flags = MESH_VAO_FLAG_NORMALS;
    size_t size = mesh_vao_serialized_size(&slot, flags);
    uint8_t *buf = malloc(size);
    if (!buf) { mesh_slot_destroy(&slot); return false; }

    bool ok = mesh_vao_serialize(&slot, flags, buf, size);
    mesh_slot_destroy(&slot);
    if (!ok) { free(buf); return false; }

    *out = buf;
    *out_size = size;
    return true;
}

/** Recursively remove a directory tree (for test cleanup). */
static void rmdir_recursive_(const char *path) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", path);
    (void)system(cmd);
}

/* ---- Tests: init/destroy ---- */

static int test_init_destroy(void) {
    collision_mesh_store_t store;
    collision_mesh_store_init(&store, 16);

    ASSERT(store.capacity == 16);
    ASSERT(store.entries != NULL);

    /* All entries should be empty. */
    for (uint32_t i = 0; i < 16; i++) {
        ASSERT(!collision_mesh_store_has(&store, i));
        ASSERT(collision_mesh_store_get(&store, i) == NULL);
    }

    collision_mesh_store_destroy(&store);
    return 1;
}

static int test_init_zero_capacity(void) {
    collision_mesh_store_t store;
    collision_mesh_store_init(&store, 0);
    ASSERT(store.capacity == 0);
    ASSERT(store.entries == NULL);
    collision_mesh_store_destroy(&store);
    return 1;
}

/* ---- Tests: set/get/has/remove ---- */

static int test_set_get(void) {
    collision_mesh_store_t store;
    collision_mesh_store_init(&store, 8);

    uint8_t *fvma = NULL;
    size_t fvma_size = 0;
    ASSERT(build_test_fvma_(&fvma, &fvma_size, 0, 0, 0));

    /* Set collision mesh for entity 3. */
    ASSERT(collision_mesh_store_set(&store, 3, fvma, fvma_size));
    ASSERT(collision_mesh_store_has(&store, 3));

    /* Get it back. */
    size_t got_size = 0;
    const uint8_t *got = collision_mesh_store_get(&store, 3);
    ASSERT(got != NULL);
    got_size = collision_mesh_store_get_size(&store, 3);
    ASSERT(got_size == fvma_size);

    /* Data should match. */
    ASSERT(memcmp(got, fvma, fvma_size) == 0);

    free(fvma);
    collision_mesh_store_destroy(&store);
    return 1;
}

static int test_set_overwrites(void) {
    collision_mesh_store_t store;
    collision_mesh_store_init(&store, 8);

    uint8_t *fvma_a = NULL, *fvma_b = NULL;
    size_t size_a = 0, size_b = 0;
    ASSERT(build_test_fvma_(&fvma_a, &size_a, 0, 0, 0));
    ASSERT(build_test_fvma_(&fvma_b, &size_b, 5, 5, 5));

    ASSERT(collision_mesh_store_set(&store, 0, fvma_a, size_a));
    ASSERT(collision_mesh_store_set(&store, 0, fvma_b, size_b));

    /* Should have the second mesh. */
    const uint8_t *got = collision_mesh_store_get(&store, 0);
    size_t got_size = collision_mesh_store_get_size(&store, 0);
    ASSERT(got != NULL);
    ASSERT(got_size == size_b);
    ASSERT(memcmp(got, fvma_b, size_b) == 0);

    free(fvma_a);
    free(fvma_b);
    collision_mesh_store_destroy(&store);
    return 1;
}

static int test_remove(void) {
    collision_mesh_store_t store;
    collision_mesh_store_init(&store, 8);

    uint8_t *fvma = NULL;
    size_t size = 0;
    ASSERT(build_test_fvma_(&fvma, &size, 0, 0, 0));

    ASSERT(collision_mesh_store_set(&store, 2, fvma, size));
    ASSERT(collision_mesh_store_has(&store, 2));

    collision_mesh_store_remove(&store, 2);
    ASSERT(!collision_mesh_store_has(&store, 2));
    ASSERT(collision_mesh_store_get(&store, 2) == NULL);

    free(fvma);
    collision_mesh_store_destroy(&store);
    return 1;
}

/* ---- Tests: edge cases ---- */

static int test_out_of_range(void) {
    collision_mesh_store_t store;
    collision_mesh_store_init(&store, 4);

    uint8_t dummy[4] = {1, 2, 3, 4};

    /* Out of range set should fail. */
    ASSERT(!collision_mesh_store_set(&store, 4, dummy, 4));
    ASSERT(!collision_mesh_store_set(&store, 999, dummy, 4));

    /* Out of range get/has should return NULL/false. */
    ASSERT(collision_mesh_store_get(&store, 4) == NULL);
    ASSERT(!collision_mesh_store_has(&store, 999));

    /* Out of range remove should not crash. */
    collision_mesh_store_remove(&store, 999);

    collision_mesh_store_destroy(&store);
    return 1;
}

static int test_null_data(void) {
    collision_mesh_store_t store;
    collision_mesh_store_init(&store, 4);

    /* NULL data or zero size should fail. */
    ASSERT(!collision_mesh_store_set(&store, 0, NULL, 10));
    ASSERT(!collision_mesh_store_set(&store, 0, (uint8_t *)"x", 0));

    collision_mesh_store_destroy(&store);
    return 1;
}

static int test_remove_nonexistent(void) {
    collision_mesh_store_t store;
    collision_mesh_store_init(&store, 4);

    /* Should not crash. */
    collision_mesh_store_remove(&store, 0);
    collision_mesh_store_remove(&store, 3);
    ASSERT(!collision_mesh_store_has(&store, 0));

    collision_mesh_store_destroy(&store);
    return 1;
}

static int test_entity_zero(void) {
    collision_mesh_store_t store;
    collision_mesh_store_init(&store, 4);

    uint8_t *fvma = NULL;
    size_t size = 0;
    ASSERT(build_test_fvma_(&fvma, &size, 0, 0, 0));

    /* Entity ID 0 should work fine. */
    ASSERT(collision_mesh_store_set(&store, 0, fvma, size));
    ASSERT(collision_mesh_store_has(&store, 0));
    ASSERT(collision_mesh_store_get(&store, 0) != NULL);

    free(fvma);
    collision_mesh_store_destroy(&store);
    return 1;
}

/* ---- Tests: clear ---- */

static int test_clear(void) {
    collision_mesh_store_t store;
    collision_mesh_store_init(&store, 8);

    uint8_t *fvma = NULL;
    size_t size = 0;
    ASSERT(build_test_fvma_(&fvma, &size, 0, 0, 0));

    ASSERT(collision_mesh_store_set(&store, 0, fvma, size));
    ASSERT(collision_mesh_store_set(&store, 3, fvma, size));
    ASSERT(collision_mesh_store_set(&store, 7, fvma, size));

    collision_mesh_store_clear(&store);

    for (uint32_t i = 0; i < 8; i++) {
        ASSERT(!collision_mesh_store_has(&store, i));
    }

    free(fvma);
    collision_mesh_store_destroy(&store);
    return 1;
}

/* ---- Tests: disk I/O ---- */

static int test_save_load_entry(void) {
    const char *dir = "/tmp/collision_mesh_test_io";
    rmdir_recursive_(dir);
    mkdir(dir, 0755);

    collision_mesh_store_t store;
    collision_mesh_store_init(&store, 8);

    uint8_t *fvma = NULL;
    size_t size = 0;
    ASSERT(build_test_fvma_(&fvma, &size, 3, 3, 3));

    ASSERT(collision_mesh_store_set(&store, 2, fvma, size));

    /* Save entity 2's collision mesh to disk. */
    ASSERT(collision_mesh_store_save_entry(&store, 2, dir));

    /* Load into a fresh store. */
    collision_mesh_store_t store2;
    collision_mesh_store_init(&store2, 8);

    ASSERT(collision_mesh_store_load_entry(&store2, 2, dir));
    ASSERT(collision_mesh_store_has(&store2, 2));

    const uint8_t *loaded = collision_mesh_store_get(&store2, 2);
    size_t loaded_size = collision_mesh_store_get_size(&store2, 2);
    ASSERT(loaded != NULL);
    ASSERT(loaded_size == size);
    ASSERT(memcmp(loaded, fvma, size) == 0);

    free(fvma);
    collision_mesh_store_destroy(&store);
    collision_mesh_store_destroy(&store2);
    rmdir_recursive_(dir);
    return 1;
}

static int test_load_nonexistent(void) {
    const char *dir = "/tmp/collision_mesh_test_nofile";
    rmdir_recursive_(dir);
    mkdir(dir, 0755);

    collision_mesh_store_t store;
    collision_mesh_store_init(&store, 8);

    /* Should fail gracefully. */
    ASSERT(!collision_mesh_store_load_entry(&store, 5, dir));
    ASSERT(!collision_mesh_store_has(&store, 5));

    collision_mesh_store_destroy(&store);
    rmdir_recursive_(dir);
    return 1;
}

static int test_save_load_all(void) {
    const char *dir = "/tmp/collision_mesh_test_all";
    rmdir_recursive_(dir);
    mkdir(dir, 0755);

    collision_mesh_store_t store;
    collision_mesh_store_init(&store, 8);

    uint8_t *fvma_a = NULL, *fvma_b = NULL;
    size_t size_a = 0, size_b = 0;
    ASSERT(build_test_fvma_(&fvma_a, &size_a, 0, 0, 0));
    ASSERT(build_test_fvma_(&fvma_b, &size_b, 7, 7, 7));

    ASSERT(collision_mesh_store_set(&store, 1, fvma_a, size_a));
    ASSERT(collision_mesh_store_set(&store, 5, fvma_b, size_b));

    /* Save all to disk. */
    uint32_t saved = collision_mesh_store_save_all(&store, dir);
    ASSERT(saved == 2);

    /* Load into a fresh store. */
    collision_mesh_store_t store2;
    collision_mesh_store_init(&store2, 8);

    uint32_t loaded = collision_mesh_store_load_all(&store2, dir);
    ASSERT(loaded == 2);
    ASSERT(collision_mesh_store_has(&store2, 1));
    ASSERT(collision_mesh_store_has(&store2, 5));

    /* Verify data integrity. */
    const uint8_t *got_a = collision_mesh_store_get(&store2, 1);
    size_t got_size_a = collision_mesh_store_get_size(&store2, 1);
    ASSERT(memcmp(got_a, fvma_a, size_a) == 0);
    ASSERT(got_size_a == size_a);

    const uint8_t *got_b = collision_mesh_store_get(&store2, 5);
    size_t got_size_b = collision_mesh_store_get_size(&store2, 5);
    ASSERT(memcmp(got_b, fvma_b, size_b) == 0);
    ASSERT(got_size_b == size_b);

    free(fvma_a);
    free(fvma_b);
    collision_mesh_store_destroy(&store);
    collision_mesh_store_destroy(&store2);
    rmdir_recursive_(dir);
    return 1;
}

/* ---- Tests: multiple entities ---- */

static int test_multiple_entities(void) {
    collision_mesh_store_t store;
    collision_mesh_store_init(&store, 16);

    /* Set collision meshes for several entities. */
    uint8_t *blobs[4];
    size_t sizes[4];
    for (int i = 0; i < 4; i++) {
        float off = (float)i * 10.0f;
        ASSERT(build_test_fvma_(&blobs[i], &sizes[i], off, off, off));
        ASSERT(collision_mesh_store_set(&store, (uint32_t)i * 3, blobs[i], sizes[i]));
    }

    /* Verify each is independent. */
    for (int i = 0; i < 4; i++) {
        uint32_t eid = (uint32_t)i * 3;
        ASSERT(collision_mesh_store_has(&store, eid));
        const uint8_t *got = collision_mesh_store_get(&store, eid);
        ASSERT(got != NULL);
        ASSERT(memcmp(got, blobs[i], sizes[i]) == 0);
    }

    /* Remove one; others unaffected. */
    collision_mesh_store_remove(&store, 3);
    ASSERT(!collision_mesh_store_has(&store, 3));
    ASSERT(collision_mesh_store_has(&store, 0));
    ASSERT(collision_mesh_store_has(&store, 6));
    ASSERT(collision_mesh_store_has(&store, 9));

    for (int i = 0; i < 4; i++) free(blobs[i]);
    collision_mesh_store_destroy(&store);
    return 1;
}

/* ---- Main ---- */

int main(void) {
    int fails = 0, total = 0;

    /* Init/destroy */
    RUN(test_init_destroy);
    RUN(test_init_zero_capacity);

    /* Set/get/has/remove */
    RUN(test_set_get);
    RUN(test_set_overwrites);
    RUN(test_remove);

    /* Edge cases */
    RUN(test_out_of_range);
    RUN(test_null_data);
    RUN(test_remove_nonexistent);
    RUN(test_entity_zero);

    /* Clear */
    RUN(test_clear);

    /* Disk I/O */
    RUN(test_save_load_entry);
    RUN(test_load_nonexistent);
    RUN(test_save_load_all);

    /* Multiple entities */
    RUN(test_multiple_entities);

    printf("\n%d / %d passed\n", total - fails, total);
    return fails ? 1 : 0;
}
