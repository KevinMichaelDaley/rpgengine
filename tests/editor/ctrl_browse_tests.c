/**
 * @file ctrl_browse_tests.c
 * @brief Tests for ctrl_browse — browse result cache and #N expansion.
 */

#include "ferrum/editor/ctrl_browse.h"

#include <stdio.h>
#include <string.h>

/* ── Minimal test harness ────────────────────────────────────────── */
static int g_pass, g_fail;
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  ASSERT FAILED: %s (line %d)\n", #cond, __LINE__); \
        g_fail++; return; \
    } \
} while (0)
#define RUN(fn) do { \
    int prev = g_fail; \
    printf("RUN  %s\n", #fn); fn(); \
    if (g_fail == prev) { g_pass++; printf("OK   %s\n", #fn); } \
    else printf("FAIL %s\n", #fn); \
} while(0)

/* ── Tests ────────────────────────────────────────────────────────── */

/** Test: init produces empty cache. */
static void test_init(void) {
    ctrl_browse_t browse;
    ctrl_browse_init(&browse);
    ASSERT(browse.count == 0);
    ASSERT(ctrl_browse_expand(&browse, "#1") == NULL);
}

/** Test: set stores paths and expand retrieves them by #N. */
static void test_set_and_expand(void) {
    ctrl_browse_t browse;
    ctrl_browse_init(&browse);

    const char *paths[] = {"meshes/box.glb", "meshes/sphere.glb", "meshes/pillar.glb"};
    ctrl_browse_set(&browse, paths, 3);

    ASSERT(browse.count == 3);
    const char *r1 = ctrl_browse_expand(&browse, "#1");
    ASSERT(r1 != NULL);
    ASSERT(strcmp(r1, "meshes/box.glb") == 0);

    const char *r2 = ctrl_browse_expand(&browse, "#2");
    ASSERT(r2 != NULL);
    ASSERT(strcmp(r2, "meshes/sphere.glb") == 0);

    const char *r3 = ctrl_browse_expand(&browse, "#3");
    ASSERT(r3 != NULL);
    ASSERT(strcmp(r3, "meshes/pillar.glb") == 0);
}

/** Test: expand returns NULL for out-of-range #N. */
static void test_expand_out_of_range(void) {
    ctrl_browse_t browse;
    ctrl_browse_init(&browse);

    const char *paths[] = {"meshes/box.glb"};
    ctrl_browse_set(&browse, paths, 1);

    ASSERT(ctrl_browse_expand(&browse, "#0") == NULL);
    ASSERT(ctrl_browse_expand(&browse, "#2") == NULL);
    ASSERT(ctrl_browse_expand(&browse, "#999") == NULL);
}

/** Test: expand returns NULL for non-#N tokens. */
static void test_expand_invalid_token(void) {
    ctrl_browse_t browse;
    ctrl_browse_init(&browse);

    const char *paths[] = {"meshes/box.glb"};
    ctrl_browse_set(&browse, paths, 1);

    ASSERT(ctrl_browse_expand(&browse, "box.glb") == NULL);
    ASSERT(ctrl_browse_expand(&browse, "#") == NULL);
    ASSERT(ctrl_browse_expand(&browse, "#abc") == NULL);
    ASSERT(ctrl_browse_expand(&browse, "") == NULL);
}

/** Test: clear resets the cache. */
static void test_clear(void) {
    ctrl_browse_t browse;
    ctrl_browse_init(&browse);

    const char *paths[] = {"meshes/box.glb", "meshes/sphere.glb"};
    ctrl_browse_set(&browse, paths, 2);
    ASSERT(browse.count == 2);

    ctrl_browse_clear(&browse);
    ASSERT(browse.count == 0);
    ASSERT(ctrl_browse_expand(&browse, "#1") == NULL);
}

/** Test: set replaces previous results. */
static void test_set_replaces(void) {
    ctrl_browse_t browse;
    ctrl_browse_init(&browse);

    const char *paths1[] = {"meshes/box.glb"};
    ctrl_browse_set(&browse, paths1, 1);
    ASSERT(strcmp(ctrl_browse_expand(&browse, "#1"), "meshes/box.glb") == 0);

    const char *paths2[] = {"textures/brick.png", "textures/stone.png"};
    ctrl_browse_set(&browse, paths2, 2);
    ASSERT(browse.count == 2);
    ASSERT(strcmp(ctrl_browse_expand(&browse, "#1"), "textures/brick.png") == 0);
    ASSERT(strcmp(ctrl_browse_expand(&browse, "#2"), "textures/stone.png") == 0);
}

/** Test: null safety. */
static void test_null_safety(void) {
    ctrl_browse_init(NULL);
    ctrl_browse_clear(NULL);
    ctrl_browse_set(NULL, NULL, 0);
    ASSERT(ctrl_browse_expand(NULL, "#1") == NULL);

    ctrl_browse_t browse;
    ctrl_browse_init(&browse);
    ASSERT(ctrl_browse_expand(&browse, NULL) == NULL);

    /* Set with NULL paths clears. */
    ctrl_browse_set(&browse, NULL, 5);
    ASSERT(browse.count == 0);
}

int main(void) {
    RUN(test_init);
    RUN(test_set_and_expand);
    RUN(test_expand_out_of_range);
    RUN(test_expand_invalid_token);
    RUN(test_clear);
    RUN(test_set_replaces);
    RUN(test_null_safety);
    printf("\n%d / %d tests passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
