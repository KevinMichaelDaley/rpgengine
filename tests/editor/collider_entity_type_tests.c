/**
 * @file collider_entity_type_tests.c
 * @brief Tests for collider-only entity types.
 *
 * Validates that the 4 new collider-only entity types (sphere, box,
 * capsule, hull) have distinct type IDs, are registered in the type
 * registry, and can be resolved by name.
 */

#include "ferrum/editor/edit_entity.h"

#include <stdio.h>
#include <string.h>

static int g_pass, g_fail;

#define ASSERT(cond) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; \
           fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

/* ---- Tests ---- */

/** New type constants exist and are distinct from existing types. */
static void test_type_constants(void) {
    /* Values are distinct. */
    ASSERT(EDIT_ENTITY_TYPE_COLLIDER_SPHERE != EDIT_ENTITY_TYPE_BOX);
    ASSERT(EDIT_ENTITY_TYPE_COLLIDER_SPHERE != EDIT_ENTITY_TYPE_SPHERE);
    ASSERT(EDIT_ENTITY_TYPE_COLLIDER_SPHERE != EDIT_ENTITY_TYPE_CAPSULE);
    ASSERT(EDIT_ENTITY_TYPE_COLLIDER_SPHERE != EDIT_ENTITY_TYPE_MARKER);
    ASSERT(EDIT_ENTITY_TYPE_COLLIDER_SPHERE != EDIT_ENTITY_TYPE_MESH);
    ASSERT(EDIT_ENTITY_TYPE_COLLIDER_SPHERE != EDIT_ENTITY_TYPE_HALFSPACE);

    ASSERT(EDIT_ENTITY_TYPE_COLLIDER_BOX != EDIT_ENTITY_TYPE_COLLIDER_SPHERE);
    ASSERT(EDIT_ENTITY_TYPE_COLLIDER_CAPSULE != EDIT_ENTITY_TYPE_COLLIDER_BOX);
    ASSERT(EDIT_ENTITY_TYPE_COLLIDER_HULL != EDIT_ENTITY_TYPE_COLLIDER_CAPSULE);

    /* All within MAX range. */
    ASSERT(EDIT_ENTITY_TYPE_COLLIDER_SPHERE < EDIT_ENTITY_TYPE_MAX);
    ASSERT(EDIT_ENTITY_TYPE_COLLIDER_BOX < EDIT_ENTITY_TYPE_MAX);
    ASSERT(EDIT_ENTITY_TYPE_COLLIDER_CAPSULE < EDIT_ENTITY_TYPE_MAX);
    ASSERT(EDIT_ENTITY_TYPE_COLLIDER_HULL < EDIT_ENTITY_TYPE_MAX);
}

/** Type registry includes the new collider types. */
static void test_type_registry_contains_colliders(void) {
    uint32_t count = 0;
    const edit_entity_type_info_t *types = edit_entity_type_registry(&count);
    ASSERT(types != NULL);
    ASSERT(count >= 10); /* 6 original + 4 new */

    /* Check each collider type is present. */
    bool found_csphere = false, found_cbox = false;
    bool found_ccapsule = false, found_chull = false;
    for (uint32_t i = 0; i < count; i++) {
        if (types[i].type_id == EDIT_ENTITY_TYPE_COLLIDER_SPHERE)
            found_csphere = true;
        if (types[i].type_id == EDIT_ENTITY_TYPE_COLLIDER_BOX)
            found_cbox = true;
        if (types[i].type_id == EDIT_ENTITY_TYPE_COLLIDER_CAPSULE)
            found_ccapsule = true;
        if (types[i].type_id == EDIT_ENTITY_TYPE_COLLIDER_HULL)
            found_chull = true;
    }
    ASSERT(found_csphere);
    ASSERT(found_cbox);
    ASSERT(found_ccapsule);
    ASSERT(found_chull);
}

/** Name lookup resolves the new collider types. */
static void test_type_by_name(void) {
    ASSERT(edit_entity_type_by_name("collider_sphere") ==
           EDIT_ENTITY_TYPE_COLLIDER_SPHERE);
    ASSERT(edit_entity_type_by_name("collider_box") ==
           EDIT_ENTITY_TYPE_COLLIDER_BOX);
    ASSERT(edit_entity_type_by_name("collider_capsule") ==
           EDIT_ENTITY_TYPE_COLLIDER_CAPSULE);
    ASSERT(edit_entity_type_by_name("collider_hull") ==
           EDIT_ENTITY_TYPE_COLLIDER_HULL);

    /* Existing types still work. */
    ASSERT(edit_entity_type_by_name("box") == EDIT_ENTITY_TYPE_BOX);
    ASSERT(edit_entity_type_by_name("sphere") == EDIT_ENTITY_TYPE_SPHERE);
}

/** Collider types are distinct from their visual counterparts. */
static void test_collider_vs_visual(void) {
    ASSERT(edit_entity_type_by_name("sphere") !=
           edit_entity_type_by_name("collider_sphere"));
    ASSERT(edit_entity_type_by_name("box") !=
           edit_entity_type_by_name("collider_box"));
    ASSERT(edit_entity_type_by_name("capsule") !=
           edit_entity_type_by_name("collider_capsule"));
}

/** Entity store can create collider-only entities. */
static void test_create_collider_entity(void) {
    edit_entity_store_t store;
    ASSERT(edit_entity_store_init(&store, 16));

    uint32_t id = edit_entity_store_create(&store,
                                            EDIT_ENTITY_TYPE_COLLIDER_SPHERE);
    ASSERT(id != EDIT_ENTITY_INVALID_ID);

    const edit_entity_t *ent = edit_entity_store_get(&store, id);
    ASSERT(ent != NULL);
    ASSERT(ent->type == EDIT_ENTITY_TYPE_COLLIDER_SPHERE);
    ASSERT(ent->active);

    edit_entity_store_destroy(&store);
}

/* ---- Main ---- */

int main(void) {
    printf("collider_entity_type_tests:\n");

    test_type_constants();
    test_type_registry_contains_colliders();
    test_type_by_name();
    test_collider_vs_visual();
    test_create_collider_entity();

    printf("collider_entity_type_tests: %d passed, %d failed\n",
           g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
