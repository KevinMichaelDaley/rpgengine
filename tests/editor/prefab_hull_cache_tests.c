/**
 * @file prefab_hull_cache_tests.c
 * @brief Tests for prefab hull cache with generation-based invalidation.
 */

#include "ferrum/editor/scene/prefab/prefab_hull_cache.h"
#include "ferrum/editor/scene/prefab/prefab_hull_build.h"
#include "ferrum/editor/scene/prefab/prefab_bone_parent.h"
#include "ferrum/editor/edit_entity.h"

#include <stdio.h>
#include <string.h>

static int g_pass, g_fail;

#define ASSERT(cond) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; \
           fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

/* ---- Helper: spawn markers forming a tetrahedron ---- */

static void spawn_tetra_markers(edit_entity_store_t *store, uint32_t root,
                                uint32_t bone) {
    uint32_t m0 = edit_entity_store_create(store, EDIT_ENTITY_TYPE_MARKER);
    uint32_t m1 = edit_entity_store_create(store, EDIT_ENTITY_TYPE_MARKER);
    uint32_t m2 = edit_entity_store_create(store, EDIT_ENTITY_TYPE_MARKER);
    uint32_t m3 = edit_entity_store_create(store, EDIT_ENTITY_TYPE_MARKER);

    edit_entity_store_get_mut(store, m0)->pos[0] = 0;
    edit_entity_store_get_mut(store, m0)->pos[1] = 0;
    edit_entity_store_get_mut(store, m0)->pos[2] = 0;

    edit_entity_store_get_mut(store, m1)->pos[0] = 1;
    edit_entity_store_get_mut(store, m1)->pos[1] = 0;
    edit_entity_store_get_mut(store, m1)->pos[2] = 0;

    edit_entity_store_get_mut(store, m2)->pos[0] = 0;
    edit_entity_store_get_mut(store, m2)->pos[1] = 1;
    edit_entity_store_get_mut(store, m2)->pos[2] = 0;

    edit_entity_store_get_mut(store, m3)->pos[0] = 0;
    edit_entity_store_get_mut(store, m3)->pos[1] = 0;
    edit_entity_store_get_mut(store, m3)->pos[2] = 1;

    prefab_parent_to_bone(store, m0, root, bone);
    prefab_parent_to_bone(store, m1, root, bone);
    prefab_parent_to_bone(store, m2, root, bone);
    prefab_parent_to_bone(store, m3, root, bone);
}

/* ---- Tests ---- */

/** Init produces empty cache. */
static void test_init(void) {
    prefab_hull_cache_t cache;
    prefab_hull_cache_init(&cache);
    ASSERT(cache.count == 0);
    ASSERT(cache.generation == 0);
}

/** Rebuild populates cache entries. */
static void test_rebuild(void) {
    edit_entity_store_t store;
    edit_entity_store_init(&store, 64);
    uint32_t root = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_MESH);

    spawn_tetra_markers(&store, root, 0);
    spawn_tetra_markers(&store, root, 2);

    prefab_hull_cache_t cache;
    prefab_hull_cache_init(&cache);

    /* Rebuild with bone_count=4 (bones 0,1,2,3). */
    prefab_hull_cache_rebuild(&cache, &store, root, 4);

    /* Bone 0 and 2 should have valid hulls; 1 and 3 should be invalid. */
    const prefab_hull_entry_t *e0 = prefab_hull_cache_get(&cache, 0);
    ASSERT(e0 != NULL);
    ASSERT(e0->valid);
    ASSERT(e0->hull.vertex_count >= 4);

    const prefab_hull_entry_t *e1 = prefab_hull_cache_get(&cache, 1);
    ASSERT(e1 != NULL);
    ASSERT(!e1->valid);

    const prefab_hull_entry_t *e2 = prefab_hull_cache_get(&cache, 2);
    ASSERT(e2 != NULL);
    ASSERT(e2->valid);

    const prefab_hull_entry_t *e3 = prefab_hull_cache_get(&cache, 3);
    ASSERT(e3 != NULL);
    ASSERT(!e3->valid);

    edit_entity_store_destroy(&store);
}

/** Invalidate + rebuild updates generation. */
static void test_invalidate_rebuild(void) {
    edit_entity_store_t store;
    edit_entity_store_init(&store, 64);
    uint32_t root = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_MESH);
    spawn_tetra_markers(&store, root, 0);

    prefab_hull_cache_t cache;
    prefab_hull_cache_init(&cache);
    prefab_hull_cache_rebuild(&cache, &store, root, 2);

    uint32_t gen1 = cache.generation;

    /* Invalidate bone 0. */
    prefab_hull_cache_invalidate(&cache, 0);
    const prefab_hull_entry_t *e = prefab_hull_cache_get(&cache, 0);
    ASSERT(e != NULL);
    ASSERT(!e->valid); /* Invalidated. */

    /* Rebuild again. */
    prefab_hull_cache_rebuild(&cache, &store, root, 2);
    ASSERT(cache.generation == gen1 + 1);
    e = prefab_hull_cache_get(&cache, 0);
    ASSERT(e->valid); /* Rebuilt. */

    edit_entity_store_destroy(&store);
}

/** Get with nonexistent bone_index returns NULL. */
static void test_get_nonexistent(void) {
    prefab_hull_cache_t cache;
    prefab_hull_cache_init(&cache);
    ASSERT(prefab_hull_cache_get(&cache, 0) == NULL);
    ASSERT(prefab_hull_cache_get(&cache, 255) == NULL);
}

/* ---- Main ---- */

int main(void) {
    printf("prefab_hull_cache_tests:\n");

    test_init();
    test_rebuild();
    test_invalidate_rebuild();
    test_get_nonexistent();

    printf("prefab_hull_cache_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
