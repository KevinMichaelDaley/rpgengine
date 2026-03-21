/**
 * @file skel_promote_tests.c
 * @brief Tests for skeleton promotion state management.
 *
 * Tests the asset_ref_widget integration for assigning skeletons
 * to mesh entities. Does not test Clay rendering (requires GL).
 */

#include "ferrum/editor/panels/asset_ref_widget.h"
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

/** Simulate the skeleton promotion flow: widget accept + confirm + attr set. */
static void test_promote_flow(void) {
    /* Create entity store with a MESH entity. */
    edit_entity_store_t store;
    edit_entity_store_init(&store, 32);
    uint32_t eid = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_MESH);

    /* Initialize asset ref widget for skeleton selection. */
    asset_ref_state_t skel_ref;
    asset_ref_init(&skel_ref, 7); /* EDIT_ASSET_SKELETON */
    ASSERT(skel_ref.filter_type == 7);

    /* Simulate user clicking a skeleton in asset tree. */
    asset_ref_accept(&skel_ref, "humanoid.fskel");
    ASSERT(strcmp(skel_ref.path, "humanoid.fskel") == 0);
    ASSERT(skel_ref.confirmed == false);

    /* Simulate pressing enter to confirm. */
    asset_ref_confirm(&skel_ref);
    ASSERT(skel_ref.confirmed == true);

    /* On confirm, the UI would set the skel_path attr on the entity. */
    edit_entity_t *ent = edit_entity_store_get_mut(&store, eid);
    entity_attrs_set(&ent->attrs, SCRIPT_KEY_SKEL_PATH, SCRIPT_ATTR_STR,
                     skel_ref.path, (uint8_t)(strlen(skel_ref.path) + 1));

    /* Verify attr was set. */
    uint8_t at = 0, as = 0;
    const void *data = entity_attrs_get(&ent->attrs, SCRIPT_KEY_SKEL_PATH,
                                         &at, &as);
    ASSERT(data != NULL);
    ASSERT(at == SCRIPT_ATTR_STR);
    ASSERT(strcmp((const char *)data, "humanoid.fskel") == 0);

    edit_entity_store_destroy(&store);
}

/** Widget initialized with skeleton filter. */
static void test_widget_skeleton_filter(void) {
    asset_ref_state_t state;
    asset_ref_init(&state, 7);
    ASSERT(state.filter_type == 7);
    ASSERT(state.path[0] == '\0');
    ASSERT(state.focused == false);
}

/** Re-accept replaces previous path. */
static void test_reaccept_replaces(void) {
    asset_ref_state_t state;
    asset_ref_init(&state, 7);

    asset_ref_accept(&state, "old.fskel");
    ASSERT(strcmp(state.path, "old.fskel") == 0);

    asset_ref_accept(&state, "new.fskel");
    ASSERT(strcmp(state.path, "new.fskel") == 0);
}

/** Confirm then accept resets confirmed state. */
static void test_confirm_accept_resets(void) {
    asset_ref_state_t state;
    asset_ref_init(&state, 7);

    asset_ref_accept(&state, "first.fskel");
    asset_ref_confirm(&state);
    ASSERT(state.confirmed == true);

    asset_ref_accept(&state, "second.fskel");
    ASSERT(state.confirmed == false);
    ASSERT(strcmp(state.path, "second.fskel") == 0);
}

/** Entity without MESH type shouldn't get skel_path (caller responsibility). */
static void test_non_mesh_entity(void) {
    edit_entity_store_t store;
    edit_entity_store_init(&store, 32);
    uint32_t eid = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);

    /* The widget doesn't know about entity types — it's the inspector's
     * job to only show the promotion UI for MESH entities. */
    const edit_entity_t *ent = edit_entity_store_get(&store, eid);
    ASSERT(ent->type == EDIT_ENTITY_TYPE_BOX);

    edit_entity_store_destroy(&store);
}

/** Existing skel_path pre-populates widget. */
static void test_existing_skel_path(void) {
    edit_entity_store_t store;
    edit_entity_store_init(&store, 32);
    uint32_t eid = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_MESH);

    edit_entity_t *ent = edit_entity_store_get_mut(&store, eid);
    const char *existing = "goblin.fskel";
    entity_attrs_set(&ent->attrs, SCRIPT_KEY_SKEL_PATH, SCRIPT_ATTR_STR,
                     existing, (uint8_t)(strlen(existing) + 1));

    /* Widget should be pre-populated from entity attr. */
    asset_ref_state_t skel_ref;
    asset_ref_init(&skel_ref, 7);

    uint8_t at = 0, as = 0;
    const void *data = entity_attrs_get(&ent->attrs, SCRIPT_KEY_SKEL_PATH,
                                         &at, &as);
    if (data && at == SCRIPT_ATTR_STR) {
        asset_ref_set_path(&skel_ref, (const char *)data);
    }

    ASSERT(strcmp(skel_ref.path, "goblin.fskel") == 0);
    ASSERT(strcmp(skel_ref.display, "goblin.fskel") == 0);

    edit_entity_store_destroy(&store);
}

int main(void) {
    printf("skel_promote_tests:\n");
    test_promote_flow();
    test_widget_skeleton_filter();
    test_reaccept_replaces();
    test_confirm_accept_resets();
    test_non_mesh_entity();
    test_existing_skel_path();
    printf("skel_promote_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
