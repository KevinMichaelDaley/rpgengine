/**
 * @file prefab_bone_parent_tests.c
 * @brief Tests for prefab bone parenting — setting/removing bone attrs.
 *
 * Validates prefab_parent_to_bone and prefab_unparent which manage
 * SCRIPT_KEY_PARENT_ID and SCRIPT_KEY_BONE_INDEX on entities.
 */

#include "ferrum/editor/scene/prefab/prefab_bone_parent.h"
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

/* ---- Helper: read uint32 attr ---- */
static bool get_u32_attr(const entity_attrs_t *attrs, uint16_t key,
                         uint32_t *out) {
    uint8_t type, size;
    const void *data = entity_attrs_get(attrs, key, &type, &size);
    if (!data || type != SCRIPT_ATTR_U32 || size != 4) return false;
    memcpy(out, data, 4);
    return true;
}

/* ---- Tests ---- */

/** Parent sets both PARENT_ID and BONE_INDEX attrs. */
static void test_parent_sets_attrs(void) {
    edit_entity_store_t store;
    ASSERT(edit_entity_store_init(&store, 64));

    uint32_t root_id = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_MESH);
    uint32_t child_id = edit_entity_store_create(&store,
                                                  EDIT_ENTITY_TYPE_COLLIDER_BOX);
    ASSERT(root_id != EDIT_ENTITY_INVALID_ID);
    ASSERT(child_id != EDIT_ENTITY_INVALID_ID);

    bool ok = prefab_parent_to_bone(&store, child_id, root_id, 5);
    ASSERT(ok);

    /* Verify PARENT_ID attr. */
    const edit_entity_t *ent = edit_entity_store_get(&store, child_id);
    ASSERT(ent != NULL);
    uint32_t parent_id_val = 0;
    ASSERT(get_u32_attr(&ent->attrs, SCRIPT_KEY_PARENT_ID, &parent_id_val));
    ASSERT(parent_id_val == root_id);

    /* Verify BONE_INDEX attr. */
    uint32_t bone_idx_val = 0;
    ASSERT(get_u32_attr(&ent->attrs, SCRIPT_KEY_BONE_INDEX, &bone_idx_val));
    ASSERT(bone_idx_val == 5);

    edit_entity_store_destroy(&store);
}

/** Unparent removes both attrs. */
static void test_unparent_removes(void) {
    edit_entity_store_t store;
    ASSERT(edit_entity_store_init(&store, 64));

    uint32_t root_id = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_MESH);
    uint32_t child_id = edit_entity_store_create(&store,
                                                  EDIT_ENTITY_TYPE_COLLIDER_SPHERE);

    prefab_parent_to_bone(&store, child_id, root_id, 3);

    bool ok = prefab_unparent(&store, child_id);
    ASSERT(ok);

    /* Both attrs should be gone. */
    const edit_entity_t *ent = edit_entity_store_get(&store, child_id);
    ASSERT(ent != NULL);
    uint32_t val = 0;
    ASSERT(!get_u32_attr(&ent->attrs, SCRIPT_KEY_PARENT_ID, &val));
    ASSERT(!get_u32_attr(&ent->attrs, SCRIPT_KEY_BONE_INDEX, &val));

    edit_entity_store_destroy(&store);
}

/** Parent with invalid entity ID returns false. */
static void test_invalid_entity(void) {
    edit_entity_store_t store;
    ASSERT(edit_entity_store_init(&store, 64));

    bool ok = prefab_parent_to_bone(&store, EDIT_ENTITY_INVALID_ID, 0, 0);
    ASSERT(!ok);

    /* Out of range. */
    ok = prefab_parent_to_bone(&store, 999, 0, 0);
    ASSERT(!ok);

    edit_entity_store_destroy(&store);
}

/** Re-parenting replaces existing bone index. */
static void test_replaces_existing(void) {
    edit_entity_store_t store;
    ASSERT(edit_entity_store_init(&store, 64));

    uint32_t root_id = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_MESH);
    uint32_t child_id = edit_entity_store_create(&store,
                                                  EDIT_ENTITY_TYPE_COLLIDER_CAPSULE);

    prefab_parent_to_bone(&store, child_id, root_id, 3);
    prefab_parent_to_bone(&store, child_id, root_id, 7);

    const edit_entity_t *ent = edit_entity_store_get(&store, child_id);
    ASSERT(ent != NULL);
    uint32_t bone_idx = 0;
    ASSERT(get_u32_attr(&ent->attrs, SCRIPT_KEY_BONE_INDEX, &bone_idx));
    ASSERT(bone_idx == 7);

    edit_entity_store_destroy(&store);
}

/** Unparent on unparented entity returns false. */
static void test_unparent_no_attrs(void) {
    edit_entity_store_t store;
    ASSERT(edit_entity_store_init(&store, 64));

    uint32_t id = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    bool ok = prefab_unparent(&store, id);
    ASSERT(!ok);

    edit_entity_store_destroy(&store);
}

/** NULL store returns false. */
static void test_null_store(void) {
    ASSERT(!prefab_parent_to_bone(NULL, 0, 0, 0));
    ASSERT(!prefab_unparent(NULL, 0));
}

/* ---- Main ---- */

int main(void) {
    printf("prefab_bone_parent_tests:\n");

    test_parent_sets_attrs();
    test_unparent_removes();
    test_invalid_entity();
    test_replaces_existing();
    test_unparent_no_attrs();
    test_null_store();

    printf("prefab_bone_parent_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
