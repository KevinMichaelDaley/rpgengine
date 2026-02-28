/**
 * @file client_asset_cache_tests.c
 * @brief Tests for client-side asset cache.
 */

#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

#include "ferrum/editor/client/client_asset_cache.h"

/* ----------------------------------------------------------------------- */
/* Test macros                                                               */
/* ----------------------------------------------------------------------- */

static int g_pass, g_fail;

#define RUN(fn) do { \
    printf("RUN  %s\n", #fn); \
    if (fn()) { printf("OK   %s\n", #fn); g_pass++; } \
    else       { printf("FAIL %s\n", #fn); g_fail++; } \
} while (0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  ASSERT FAILED: %s (line %d)\n", #cond, __LINE__); \
        return false; \
    } \
} while (0)

/* ----------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ----------------------------------------------------------------------- */

static char g_tmpdir[256];

static void make_tmpdir_(void) {
    snprintf(g_tmpdir, sizeof(g_tmpdir), "/tmp/asset_cache_test_XXXXXX");
    char *r = mkdtemp(g_tmpdir);
    (void)r;
}

static void rm_tmpdir_(void) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", g_tmpdir);
    int r = system(cmd);
    (void)r;
}

/* ----------------------------------------------------------------------- */
/* Tests                                                                     */
/* ----------------------------------------------------------------------- */

/** Basic put and get. */
static bool test_put_get(void) {
    make_tmpdir_();
    asset_cache_t cache;
    ASSERT(asset_cache_init(&cache, g_tmpdir, 0));

    const char *data = "HELLO_WORLD_DATA";
    ASSERT(asset_cache_put(&cache, "meshes/box.glb", 0xABCD, data, 16));
    ASSERT(asset_cache_count(&cache) == 1);
    ASSERT(asset_cache_has(&cache, "meshes/box.glb", 0xABCD));

    void *out = NULL;
    uint32_t out_sz = 0;
    ASSERT(asset_cache_get(&cache, "meshes/box.glb", &out, &out_sz));
    ASSERT(out_sz == 16);
    ASSERT(memcmp(out, "HELLO_WORLD_DATA", 16) == 0);
    free(out);

    asset_cache_destroy(&cache);
    rm_tmpdir_();
    return true;
}

/** has() returns false for wrong hash. */
static bool test_hash_mismatch(void) {
    make_tmpdir_();
    asset_cache_t cache;
    ASSERT(asset_cache_init(&cache, g_tmpdir, 0));

    ASSERT(asset_cache_put(&cache, "tex/a.png", 0x1111, "AAA", 3));
    ASSERT(asset_cache_has(&cache, "tex/a.png", 0x1111));
    ASSERT(!asset_cache_has(&cache, "tex/a.png", 0x2222));

    asset_cache_destroy(&cache);
    rm_tmpdir_();
    return true;
}

/** get() on missing entry returns false. */
static bool test_get_missing(void) {
    make_tmpdir_();
    asset_cache_t cache;
    ASSERT(asset_cache_init(&cache, g_tmpdir, 0));

    void *out = NULL;
    uint32_t sz = 0;
    ASSERT(!asset_cache_get(&cache, "nope.glb", &out, &sz));

    asset_cache_destroy(&cache);
    rm_tmpdir_();
    return true;
}

/** Invalidate removes entry and file. */
static bool test_invalidate(void) {
    make_tmpdir_();
    asset_cache_t cache;
    ASSERT(asset_cache_init(&cache, g_tmpdir, 0));

    ASSERT(asset_cache_put(&cache, "m/x.glb", 0x42, "DATA", 4));
    ASSERT(asset_cache_count(&cache) == 1);

    ASSERT(asset_cache_invalidate(&cache, "m/x.glb"));
    ASSERT(asset_cache_count(&cache) == 0);
    ASSERT(!asset_cache_has(&cache, "m/x.glb", 0x42));

    /* get should fail. */
    void *out = NULL;
    uint32_t sz = 0;
    ASSERT(!asset_cache_get(&cache, "m/x.glb", &out, &sz));

    asset_cache_destroy(&cache);
    rm_tmpdir_();
    return true;
}

/** LRU eviction when cache is full. */
static bool test_lru_eviction(void) {
    make_tmpdir_();
    asset_cache_t cache;
    /* Limit: 12 bytes. */
    ASSERT(asset_cache_init(&cache, g_tmpdir, 12));

    /* Put 5 bytes. */
    ASSERT(asset_cache_put(&cache, "a.glb", 1, "AAAAA", 5));
    ASSERT(asset_cache_count(&cache) == 1);

    /* Put 5 more bytes — total 10, within limit. */
    ASSERT(asset_cache_put(&cache, "b.glb", 2, "BBBBB", 5));
    ASSERT(asset_cache_count(&cache) == 2);

    /* Access a.glb to make b.glb the LRU victim. */
    ASSERT(asset_cache_has(&cache, "a.glb", 1));

    /* Put 6 bytes — total would be 16 > 12, evicts b.glb (LRU).
     * After eviction: 5+6=11 <= 12, so a.glb survives. */
    ASSERT(asset_cache_put(&cache, "c.glb", 3, "CCCCCC", 6));
    ASSERT(!asset_cache_has(&cache, "b.glb", 2));
    ASSERT(asset_cache_has(&cache, "a.glb", 1));
    ASSERT(asset_cache_has(&cache, "c.glb", 3));

    asset_cache_destroy(&cache);
    rm_tmpdir_();
    return true;
}

/** Manifest save and reload. */
static bool test_manifest_persistence(void) {
    make_tmpdir_();

    /* Phase 1: create cache and add entries. */
    {
        asset_cache_t cache;
        ASSERT(asset_cache_init(&cache, g_tmpdir, 0));
        ASSERT(asset_cache_put(&cache, "meshes/a.glb", 111, "AAA", 3));
        ASSERT(asset_cache_put(&cache, "tex/b.png", 222, "BBBB", 4));
        asset_cache_destroy(&cache); /* Saves manifest. */
    }

    /* Phase 2: reload from manifest. */
    {
        asset_cache_t cache;
        ASSERT(asset_cache_init(&cache, g_tmpdir, 0));
        ASSERT(asset_cache_count(&cache) == 2);
        ASSERT(asset_cache_has(&cache, "meshes/a.glb", 111));
        ASSERT(asset_cache_has(&cache, "tex/b.png", 222));

        /* Data should still be readable. */
        void *data = NULL;
        uint32_t sz = 0;
        ASSERT(asset_cache_get(&cache, "meshes/a.glb", &data, &sz));
        ASSERT(sz == 3);
        ASSERT(memcmp(data, "AAA", 3) == 0);
        free(data);

        asset_cache_destroy(&cache);
    }

    rm_tmpdir_();
    return true;
}

/** Update an existing entry (same path, new data). */
static bool test_update_entry(void) {
    make_tmpdir_();
    asset_cache_t cache;
    ASSERT(asset_cache_init(&cache, g_tmpdir, 0));

    ASSERT(asset_cache_put(&cache, "m/x.glb", 0x10, "OLD", 3));
    ASSERT(asset_cache_put(&cache, "m/x.glb", 0x20, "NEWDATA", 7));
    ASSERT(asset_cache_count(&cache) == 1);
    ASSERT(asset_cache_has(&cache, "m/x.glb", 0x20));
    ASSERT(!asset_cache_has(&cache, "m/x.glb", 0x10));

    void *data = NULL;
    uint32_t sz = 0;
    ASSERT(asset_cache_get(&cache, "m/x.glb", &data, &sz));
    ASSERT(sz == 7);
    ASSERT(memcmp(data, "NEWDATA", 7) == 0);
    free(data);

    asset_cache_destroy(&cache);
    rm_tmpdir_();
    return true;
}

/** Null safety. */
static bool test_null_safety(void) {
    ASSERT(!asset_cache_init(NULL, NULL, 0));
    asset_cache_destroy(NULL);
    ASSERT(!asset_cache_has(NULL, NULL, 0));
    ASSERT(!asset_cache_get(NULL, NULL, NULL, NULL));
    ASSERT(!asset_cache_put(NULL, NULL, 0, NULL, 0));
    ASSERT(!asset_cache_invalidate(NULL, NULL));
    ASSERT(!asset_cache_save_manifest(NULL));
    ASSERT(asset_cache_count(NULL) == 0);
    return true;
}

int main(void) {
    RUN(test_put_get);
    RUN(test_hash_mismatch);
    RUN(test_get_missing);
    RUN(test_invalidate);
    RUN(test_lru_eviction);
    RUN(test_manifest_persistence);
    RUN(test_update_entry);
    RUN(test_null_safety);

    printf("\n%d / %d tests passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
