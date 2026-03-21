/**
 * @file prefab_child_spawn_tests.c
 * @brief Tests for prefab child spawn tracking (pending spawn queue).
 */

#include "ferrum/editor/scene/prefab/prefab_child_spawn.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/entity/entity_attrs.h"

#include <stdio.h>
#include <string.h>

static int g_pass, g_fail;

#define ASSERT(cond) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; \
           fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

/* ---- Init tests ---- */

static void test_init_zeroes(void) {
    prefab_pending_spawn_t ps;
    memset(&ps, 0xAB, sizeof(ps));
    prefab_pending_spawn_init(&ps);

    ASSERT(ps.count == 0);
    for (uint32_t i = 0; i < PREFAB_PENDING_SPAWN_MAX; i++) {
        ASSERT(ps.cmd_ids[i] == 0);
    }
}

static void test_init_null_safe(void) {
    prefab_pending_spawn_init(NULL);
    ASSERT(1);
}

/* ---- Add tests ---- */

static void test_add_tracks_cmd_id(void) {
    prefab_pending_spawn_t ps;
    prefab_pending_spawn_init(&ps);

    prefab_pending_spawn_add(&ps, 42);
    ASSERT(ps.count == 1);
    ASSERT(ps.cmd_ids[0] == 42);

    prefab_pending_spawn_add(&ps, 99);
    ASSERT(ps.count == 2);
    ASSERT(ps.cmd_ids[1] == 99);
}

static void test_add_overflow_safe(void) {
    prefab_pending_spawn_t ps;
    prefab_pending_spawn_init(&ps);

    /* Fill to max. */
    for (uint32_t i = 0; i < PREFAB_PENDING_SPAWN_MAX; i++) {
        prefab_pending_spawn_add(&ps, i + 1);
    }
    ASSERT(ps.count == PREFAB_PENDING_SPAWN_MAX);

    /* One more should not crash or exceed. */
    prefab_pending_spawn_add(&ps, 9999);
    ASSERT(ps.count == PREFAB_PENDING_SPAWN_MAX);
}

static void test_add_null_safe(void) {
    prefab_pending_spawn_add(NULL, 1);
    ASSERT(1);
}

/* ---- Resolve tests ---- */

static void test_resolve_sets_parent_id(void) {
    prefab_pending_spawn_t ps;
    prefab_pending_spawn_init(&ps);
    prefab_pending_spawn_add(&ps, 1);

    /* Create a store with an entity. */
    edit_entity_store_t store;
    edit_entity_store_init(&store, 32);
    uint32_t eid = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);

    uint32_t root_id = 999;
    prefab_pending_spawn_resolve(&ps, &store, eid, root_id);

    /* Should have decremented count. */
    ASSERT(ps.count == 0);

    /* Entity should have PARENT_ID set. */
    const edit_entity_t *ent = edit_entity_store_get(&store, eid);
    uint8_t at = 0, as = 0;
    const void *data = entity_attrs_get(&ent->attrs, SCRIPT_KEY_PARENT_ID,
                                         &at, &as);
    ASSERT(data != NULL);
    ASSERT(at == SCRIPT_ATTR_U32);
    uint32_t parent;
    memcpy(&parent, data, sizeof(uint32_t));
    ASSERT(parent == root_id);

    edit_entity_store_destroy(&store);
}

static void test_resolve_with_empty_queue(void) {
    prefab_pending_spawn_t ps;
    prefab_pending_spawn_init(&ps);

    edit_entity_store_t store;
    edit_entity_store_init(&store, 32);
    uint32_t eid = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);

    /* Resolve with empty queue should be no-op. */
    prefab_pending_spawn_resolve(&ps, &store, eid, 0);
    ASSERT(ps.count == 0);

    /* Entity should NOT have PARENT_ID. */
    const edit_entity_t *ent = edit_entity_store_get(&store, eid);
    uint8_t at = 0, as = 0;
    const void *data = entity_attrs_get(&ent->attrs, SCRIPT_KEY_PARENT_ID,
                                         &at, &as);
    ASSERT(data == NULL);

    edit_entity_store_destroy(&store);
}

static void test_resolve_null_safe(void) {
    prefab_pending_spawn_t ps;
    prefab_pending_spawn_init(&ps);
    prefab_pending_spawn_add(&ps, 1);

    prefab_pending_spawn_resolve(NULL, NULL, 0, 0);
    prefab_pending_spawn_resolve(&ps, NULL, 0, 0);
    ASSERT(1);
}

static void test_resolve_multiple(void) {
    prefab_pending_spawn_t ps;
    prefab_pending_spawn_init(&ps);
    prefab_pending_spawn_add(&ps, 10);
    prefab_pending_spawn_add(&ps, 20);
    prefab_pending_spawn_add(&ps, 30);
    ASSERT(ps.count == 3);

    edit_entity_store_t store;
    edit_entity_store_init(&store, 32);
    uint32_t e1 = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    uint32_t e2 = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_SPHERE);

    prefab_pending_spawn_resolve(&ps, &store, e1, 5);
    ASSERT(ps.count == 2);

    prefab_pending_spawn_resolve(&ps, &store, e2, 5);
    ASSERT(ps.count == 1);

    edit_entity_store_destroy(&store);
}

int main(void) {
    printf("prefab_child_spawn_tests:\n");
    test_init_zeroes();
    test_init_null_safe();
    test_add_tracks_cmd_id();
    test_add_overflow_safe();
    test_add_null_safe();
    test_resolve_sets_parent_id();
    test_resolve_with_empty_queue();
    test_resolve_null_safe();
    test_resolve_multiple();
    printf("prefab_child_spawn_tests: %d passed, %d failed\n",
           g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
