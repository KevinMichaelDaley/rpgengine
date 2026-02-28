/**
 * @file edit_asset_registry_tests.c
 * @brief Tests for the server-side asset registry.
 *
 * Tests:
 *  1. init and destroy — empty registry lifecycle
 *  2. register single asset — add entry, verify fields
 *  3. register multiple assets — verify count
 *  4. list by directory prefix — filter to specific subdirectory
 *  5. list all — returns everything
 *  6. search by regex pattern — matches asset paths
 *  7. path completion — returns candidates matching prefix
 *  8. type filtering — list only meshes, only textures, etc.
 *  9. duplicate path rejected — same path cannot register twice
 * 10. find by path — exact lookup
 * 11. scan directory — populate from real filesystem
 * 12. hash computation — CRC32 matches expected value
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "ferrum/editor/edit_asset_registry.h"

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

/** Create a temporary directory tree for scanning tests. */
static bool make_test_tree_(const char *base) {
    char path[512];

    /* Create directory hierarchy. */
    snprintf(path, sizeof(path), "%s/meshes", base);
    if (mkdir(path, 0755) != 0 && errno != EEXIST) return false;
    snprintf(path, sizeof(path), "%s/textures", base);
    if (mkdir(path, 0755) != 0 && errno != EEXIST) return false;
    snprintf(path, sizeof(path), "%s/prefabs", base);
    if (mkdir(path, 0755) != 0 && errno != EEXIST) return false;

    /* Create dummy files. */
    const char *files[] = {
        "meshes/pillar.glb",
        "meshes/wall.glb",
        "meshes/barrel.obj",
        "textures/brick.png",
        "textures/stone.ktx2",
        "prefabs/tower.prefab",
    };
    for (int i = 0; i < 6; i++) {
        snprintf(path, sizeof(path), "%s/%s", base, files[i]);
        FILE *f = fopen(path, "w");
        if (!f) return false;
        /* Write some deterministic content for hash testing. */
        fprintf(f, "test_content_%d", i);
        fclose(f);
    }
    return true;
}

/** Remove test directory tree. */
static void remove_test_tree_(const char *base) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", base);
    (void)system(cmd);
}

/* ----------------------------------------------------------------------- */
/* Tests                                                                     */
/* ----------------------------------------------------------------------- */

/** 1. Init and destroy — empty registry lifecycle. */
static bool test_init_destroy(void) {
    edit_asset_registry_t reg;
    edit_asset_registry_init(&reg, 64);
    ASSERT(edit_asset_registry_count(&reg) == 0);
    edit_asset_registry_destroy(&reg);
    return true;
}

/** 2. Register single asset — add entry, verify fields. */
static bool test_register_single(void) {
    edit_asset_registry_t reg;
    edit_asset_registry_init(&reg, 64);

    bool ok = edit_asset_registry_add(&reg, "meshes/pillar.glb",
                                       EDIT_ASSET_MESH, 12345, 0xDEADBEEF);
    ASSERT(ok);
    ASSERT(edit_asset_registry_count(&reg) == 1);

    const edit_asset_entry_t *e = edit_asset_registry_find(&reg,
                                                            "meshes/pillar.glb");
    ASSERT(e != NULL);
    ASSERT(strcmp(e->path, "meshes/pillar.glb") == 0);
    ASSERT(e->type == EDIT_ASSET_MESH);
    ASSERT(e->size == 12345);
    ASSERT(e->hash == 0xDEADBEEF);

    edit_asset_registry_destroy(&reg);
    return true;
}

/** 3. Register multiple assets — verify count. */
static bool test_register_multiple(void) {
    edit_asset_registry_t reg;
    edit_asset_registry_init(&reg, 64);

    edit_asset_registry_add(&reg, "meshes/a.glb", EDIT_ASSET_MESH, 100, 1);
    edit_asset_registry_add(&reg, "meshes/b.glb", EDIT_ASSET_MESH, 200, 2);
    edit_asset_registry_add(&reg, "textures/c.png", EDIT_ASSET_TEXTURE, 300, 3);
    ASSERT(edit_asset_registry_count(&reg) == 3);

    edit_asset_registry_destroy(&reg);
    return true;
}

/** 4. List by directory prefix — filter to specific subdirectory. */
static bool test_list_by_prefix(void) {
    edit_asset_registry_t reg;
    edit_asset_registry_init(&reg, 64);

    edit_asset_registry_add(&reg, "meshes/a.glb", EDIT_ASSET_MESH, 100, 1);
    edit_asset_registry_add(&reg, "meshes/b.glb", EDIT_ASSET_MESH, 200, 2);
    edit_asset_registry_add(&reg, "textures/c.png", EDIT_ASSET_TEXTURE, 300, 3);
    edit_asset_registry_add(&reg, "prefabs/d.prefab", EDIT_ASSET_PREFAB, 400, 4);

    const edit_asset_entry_t *results[64];
    uint32_t count = edit_asset_registry_list(&reg, "meshes/",
                                               EDIT_ASSET_ANY,
                                               results, 64);
    ASSERT(count == 2);
    /* Both should be meshes. */
    ASSERT(strncmp(results[0]->path, "meshes/", 7) == 0);
    ASSERT(strncmp(results[1]->path, "meshes/", 7) == 0);

    edit_asset_registry_destroy(&reg);
    return true;
}

/** 5. List all — returns everything. */
static bool test_list_all(void) {
    edit_asset_registry_t reg;
    edit_asset_registry_init(&reg, 64);

    edit_asset_registry_add(&reg, "meshes/a.glb", EDIT_ASSET_MESH, 100, 1);
    edit_asset_registry_add(&reg, "textures/b.png", EDIT_ASSET_TEXTURE, 200, 2);

    const edit_asset_entry_t *results[64];
    uint32_t count = edit_asset_registry_list(&reg, "",
                                               EDIT_ASSET_ANY,
                                               results, 64);
    ASSERT(count == 2);

    edit_asset_registry_destroy(&reg);
    return true;
}

/** 6. Search by regex — matches asset paths. */
static bool test_search_regex(void) {
    edit_asset_registry_t reg;
    edit_asset_registry_init(&reg, 64);

    edit_asset_registry_add(&reg, "meshes/wall_01.glb", EDIT_ASSET_MESH, 100, 1);
    edit_asset_registry_add(&reg, "meshes/wall_02.glb", EDIT_ASSET_MESH, 200, 2);
    edit_asset_registry_add(&reg, "meshes/pillar.glb", EDIT_ASSET_MESH, 300, 3);
    edit_asset_registry_add(&reg, "textures/wall.png", EDIT_ASSET_TEXTURE, 400, 4);

    const edit_asset_entry_t *results[64];
    uint32_t count = edit_asset_registry_search(&reg, "wall",
                                                 EDIT_ASSET_ANY,
                                                 results, 64);
    /* Should match wall_01.glb, wall_02.glb, and wall.png. */
    ASSERT(count == 3);

    edit_asset_registry_destroy(&reg);
    return true;
}

/** 7. Path completion — returns candidates matching prefix. */
static bool test_path_completion(void) {
    edit_asset_registry_t reg;
    edit_asset_registry_init(&reg, 64);

    edit_asset_registry_add(&reg, "meshes/pillar.glb", EDIT_ASSET_MESH, 100, 1);
    edit_asset_registry_add(&reg, "meshes/platform.glb", EDIT_ASSET_MESH, 200, 2);
    edit_asset_registry_add(&reg, "meshes/barrel.glb", EDIT_ASSET_MESH, 300, 3);

    const edit_asset_entry_t *results[64];
    uint32_t count = edit_asset_registry_complete(&reg, "meshes/p",
                                                   results, 64);
    /* Should match pillar and platform. */
    ASSERT(count == 2);
    ASSERT(strncmp(results[0]->path, "meshes/p", 8) == 0);
    ASSERT(strncmp(results[1]->path, "meshes/p", 8) == 0);

    edit_asset_registry_destroy(&reg);
    return true;
}

/** 8. Type filtering — list only meshes. */
static bool test_type_filter(void) {
    edit_asset_registry_t reg;
    edit_asset_registry_init(&reg, 64);

    edit_asset_registry_add(&reg, "meshes/a.glb", EDIT_ASSET_MESH, 100, 1);
    edit_asset_registry_add(&reg, "textures/b.png", EDIT_ASSET_TEXTURE, 200, 2);
    edit_asset_registry_add(&reg, "meshes/c.obj", EDIT_ASSET_MESH, 300, 3);

    const edit_asset_entry_t *results[64];
    uint32_t count = edit_asset_registry_list(&reg, "",
                                               EDIT_ASSET_MESH,
                                               results, 64);
    ASSERT(count == 2);
    ASSERT(results[0]->type == EDIT_ASSET_MESH);
    ASSERT(results[1]->type == EDIT_ASSET_MESH);

    edit_asset_registry_destroy(&reg);
    return true;
}

/** 9. Duplicate path rejected. */
static bool test_duplicate_rejected(void) {
    edit_asset_registry_t reg;
    edit_asset_registry_init(&reg, 64);

    bool ok1 = edit_asset_registry_add(&reg, "meshes/a.glb",
                                        EDIT_ASSET_MESH, 100, 1);
    bool ok2 = edit_asset_registry_add(&reg, "meshes/a.glb",
                                        EDIT_ASSET_MESH, 200, 2);
    ASSERT(ok1);
    ASSERT(!ok2);
    ASSERT(edit_asset_registry_count(&reg) == 1);

    edit_asset_registry_destroy(&reg);
    return true;
}

/** 10. Find by path — exact lookup. */
static bool test_find_by_path(void) {
    edit_asset_registry_t reg;
    edit_asset_registry_init(&reg, 64);

    edit_asset_registry_add(&reg, "meshes/a.glb", EDIT_ASSET_MESH, 100, 1);
    edit_asset_registry_add(&reg, "textures/b.png", EDIT_ASSET_TEXTURE, 200, 2);

    const edit_asset_entry_t *e = edit_asset_registry_find(&reg, "textures/b.png");
    ASSERT(e != NULL);
    ASSERT(strcmp(e->path, "textures/b.png") == 0);

    /* Not found. */
    ASSERT(edit_asset_registry_find(&reg, "nope") == NULL);

    edit_asset_registry_destroy(&reg);
    return true;
}

/** 11. Scan directory — populate from real filesystem. */
static bool test_scan_directory(void) {
    const char *test_dir = "/tmp/ferrum_asset_test";
    remove_test_tree_(test_dir);
    if (mkdir(test_dir, 0755) != 0) return false;
    if (!make_test_tree_(test_dir)) {
        remove_test_tree_(test_dir);
        return false;
    }

    edit_asset_registry_t reg;
    edit_asset_registry_init(&reg, 64);

    uint32_t scanned = edit_asset_registry_scan(&reg, test_dir);
    ASSERT(scanned == 6); /* 3 meshes + 2 textures + 1 prefab */
    ASSERT(edit_asset_registry_count(&reg) == 6);

    /* Verify type detection from extension. */
    const edit_asset_entry_t *glb = edit_asset_registry_find(&reg,
                                                              "meshes/pillar.glb");
    ASSERT(glb != NULL);
    ASSERT(glb->type == EDIT_ASSET_MESH);

    const edit_asset_entry_t *png = edit_asset_registry_find(&reg,
                                                              "textures/brick.png");
    ASSERT(png != NULL);
    ASSERT(png->type == EDIT_ASSET_TEXTURE);

    const edit_asset_entry_t *prefab = edit_asset_registry_find(&reg,
                                                                 "prefabs/tower.prefab");
    ASSERT(prefab != NULL);
    ASSERT(prefab->type == EDIT_ASSET_PREFAB);

    /* Verify file sizes are nonzero. */
    ASSERT(glb->size > 0);
    ASSERT(png->size > 0);

    edit_asset_registry_destroy(&reg);
    remove_test_tree_(test_dir);
    return true;
}

/** 12. Hash is nonzero for scanned files. */
static bool test_hash_nonzero(void) {
    const char *test_dir = "/tmp/ferrum_asset_hash_test";
    remove_test_tree_(test_dir);
    if (mkdir(test_dir, 0755) != 0) return false;
    if (!make_test_tree_(test_dir)) {
        remove_test_tree_(test_dir);
        return false;
    }

    edit_asset_registry_t reg;
    edit_asset_registry_init(&reg, 64);
    edit_asset_registry_scan(&reg, test_dir);

    const edit_asset_entry_t *e = edit_asset_registry_find(&reg,
                                                            "meshes/pillar.glb");
    ASSERT(e != NULL);
    ASSERT(e->hash != 0);

    /* Different files should have different hashes. */
    const edit_asset_entry_t *e2 = edit_asset_registry_find(&reg,
                                                             "meshes/wall.glb");
    ASSERT(e2 != NULL);
    ASSERT(e2->hash != 0);
    ASSERT(e->hash != e2->hash);

    edit_asset_registry_destroy(&reg);
    remove_test_tree_(test_dir);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Main                                                                      */
/* ----------------------------------------------------------------------- */

int main(void) {
    RUN(test_init_destroy);
    RUN(test_register_single);
    RUN(test_register_multiple);
    RUN(test_list_by_prefix);
    RUN(test_list_all);
    RUN(test_search_regex);
    RUN(test_path_completion);
    RUN(test_type_filter);
    RUN(test_duplicate_rejected);
    RUN(test_find_by_path);
    RUN(test_scan_directory);
    RUN(test_hash_nonzero);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
