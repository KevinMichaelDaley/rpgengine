/**
 * @file mesh_snapshot_tests.c
 * @brief Tests for mesh snapshot cache: serialize to cached blob,
 *        hash computation, slot lookup, notification generation.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ferrum/editor/mesh/mesh_snapshot.h"
#include "ferrum/editor/mesh/mesh_primitives.h"

static int g_pass = 0;
static int g_fail = 0;

#define ASSERT(cond, msg)                                              \
    do {                                                               \
        if (!(cond)) {                                                 \
            fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
            g_fail++;                                                  \
            return;                                                    \
        }                                                              \
    } while (0)

/* ------------------------------------------------------------------ */
/* Init / Destroy                                                      */
/* ------------------------------------------------------------------ */

static void test_init(void) {
    mesh_snapshot_cache_t cache;
    bool ok = mesh_snapshot_cache_init(&cache);
    ASSERT(ok, "init succeeds");
    ASSERT(cache.entries != NULL, "entries allocated");

    for (uint32_t i = 0; i < MESH_MAX_EDITABLE; i++) {
        ASSERT(cache.entries[i].data == NULL, "slot empty");
        ASSERT(cache.entries[i].size == 0, "size 0");
        ASSERT(cache.entries[i].hash == 0, "hash 0");
    }

    mesh_snapshot_cache_destroy(&cache);
    g_pass++;
}

static void test_init_null(void) {
    bool ok = mesh_snapshot_cache_init(NULL);
    ASSERT(!ok, "NULL init fails");
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Update slot                                                         */
/* ------------------------------------------------------------------ */

static void test_update_slot(void) {
    mesh_snapshot_cache_t cache;
    mesh_snapshot_cache_init(&cache);

    mesh_slot_t slot;
    mesh_slot_init(&slot, 8, 24);
    float p[3] = {0,0,0}, n[3] = {0,1,0};
    mesh_slot_add_vertex(&slot, p, n);
    mesh_slot_add_vertex(&slot, (float[3]){1,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){0,0,1}, n);
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);

    uint32_t flags = MESH_VAO_FLAG_NORMALS;
    bool ok = mesh_snapshot_cache_update(&cache, 0, &slot, flags);
    ASSERT(ok, "update succeeds");
    ASSERT(cache.entries[0].data != NULL, "data allocated");
    ASSERT(cache.entries[0].size > 0, "size nonzero");
    ASSERT(cache.entries[0].hash != 0, "hash nonzero");

    mesh_slot_destroy(&slot);
    mesh_snapshot_cache_destroy(&cache);
    g_pass++;
}

static void test_update_replaces(void) {
    mesh_snapshot_cache_t cache;
    mesh_snapshot_cache_init(&cache);

    mesh_slot_t slot;
    mesh_slot_init(&slot, 8, 24);
    float p[3] = {0,0,0}, n[3] = {0,1,0};
    mesh_slot_add_vertex(&slot, p, n);
    mesh_slot_add_vertex(&slot, (float[3]){1,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){0,0,1}, n);
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);

    mesh_snapshot_cache_update(&cache, 0, &slot, MESH_VAO_FLAG_NORMALS);
    uint64_t hash1 = cache.entries[0].hash;

    /* Modify mesh and update again */
    mesh_slot_add_vertex(&slot, (float[3]){2,0,0}, n);
    mesh_slot_add_triangle(&slot, 0, 2, 3, 0);

    mesh_snapshot_cache_update(&cache, 0, &slot, MESH_VAO_FLAG_NORMALS);
    uint64_t hash2 = cache.entries[0].hash;

    ASSERT(hash1 != hash2, "hash changes with different data");

    mesh_slot_destroy(&slot);
    mesh_snapshot_cache_destroy(&cache);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Get slot                                                            */
/* ------------------------------------------------------------------ */

static void test_get_slot(void) {
    mesh_snapshot_cache_t cache;
    mesh_snapshot_cache_init(&cache);

    mesh_slot_t slot;
    mesh_slot_init(&slot, 8, 24);
    float p[3] = {0,0,0}, n[3] = {0,1,0};
    mesh_slot_add_vertex(&slot, p, n);
    mesh_slot_add_vertex(&slot, (float[3]){1,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){0,0,1}, n);
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);

    mesh_snapshot_cache_update(&cache, 3, &slot, MESH_VAO_FLAG_NORMALS);

    const uint8_t *data = NULL;
    size_t size = 0;
    uint64_t hash = 0;
    bool ok = mesh_snapshot_cache_get(&cache, 3, &data, &size, &hash);
    ASSERT(ok, "get succeeds");
    ASSERT(data != NULL, "data returned");
    ASSERT(size > 0, "size returned");
    ASSERT(hash != 0, "hash returned");

    /* Empty slot should return false */
    ok = mesh_snapshot_cache_get(&cache, 0, &data, &size, &hash);
    ASSERT(!ok, "get empty slot fails");

    mesh_slot_destroy(&slot);
    mesh_snapshot_cache_destroy(&cache);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* OOB slot                                                            */
/* ------------------------------------------------------------------ */

static void test_oob_slot(void) {
    mesh_snapshot_cache_t cache;
    mesh_snapshot_cache_init(&cache);

    mesh_slot_t slot;
    mesh_slot_init(&slot, 4, 12);

    bool ok = mesh_snapshot_cache_update(&cache, MESH_MAX_EDITABLE, &slot, 0);
    ASSERT(!ok, "OOB update fails");

    const uint8_t *data;
    size_t size;
    uint64_t hash;
    ok = mesh_snapshot_cache_get(&cache, MESH_MAX_EDITABLE, &data, &size, &hash);
    ASSERT(!ok, "OOB get fails");

    mesh_slot_destroy(&slot);
    mesh_snapshot_cache_destroy(&cache);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Multiple slots                                                      */
/* ------------------------------------------------------------------ */

static void test_multiple_slots(void) {
    mesh_snapshot_cache_t cache;
    mesh_snapshot_cache_init(&cache);

    /* Create box in slot 0, plane in slot 1 */
    mesh_slot_t box, plane;
    mesh_slot_init(&box, 32, 48);
    mesh_slot_init(&plane, 8, 12);

    mesh_prim_box(&box, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});
    mesh_prim_plane(&plane, (float[2]){2,2}, (uint32_t[2]){1,1}, 1, (float[3]){0,0,0});

    mesh_snapshot_cache_update(&cache, 0, &box, MESH_VAO_FLAG_NORMALS);
    mesh_snapshot_cache_update(&cache, 1, &plane, MESH_VAO_FLAG_NORMALS);

    const uint8_t *data0, *data1;
    size_t s0, s1;
    uint64_t h0, h1;

    ASSERT(mesh_snapshot_cache_get(&cache, 0, &data0, &s0, &h0), "slot 0");
    ASSERT(mesh_snapshot_cache_get(&cache, 1, &data1, &s1, &h1), "slot 1");
    ASSERT(s0 != s1, "different sizes (box vs plane)");
    ASSERT(h0 != h1, "different hashes");

    mesh_slot_destroy(&box);
    mesh_slot_destroy(&plane);
    mesh_snapshot_cache_destroy(&cache);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Empty mesh                                                          */
/* ------------------------------------------------------------------ */

static void test_empty_mesh(void) {
    mesh_snapshot_cache_t cache;
    mesh_snapshot_cache_init(&cache);

    mesh_slot_t slot;
    mesh_slot_init(&slot, 0, 0);

    bool ok = mesh_snapshot_cache_update(&cache, 0, &slot, 0);
    ASSERT(ok, "empty mesh update succeeds");
    ASSERT(cache.entries[0].size == MESH_VAO_HEADER_SIZE, "empty = header only");

    mesh_slot_destroy(&slot);
    mesh_snapshot_cache_destroy(&cache);
    g_pass++;
}

/* ================================================================== */
/* Main                                                                */
/* ================================================================== */

int main(void) {
    printf("mesh_snapshot_tests:\n");

    test_init();
    test_init_null();
    test_update_slot();
    test_update_replaces();
    test_get_slot();
    test_oob_slot();
    test_multiple_slots();
    test_empty_mesh();

    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
