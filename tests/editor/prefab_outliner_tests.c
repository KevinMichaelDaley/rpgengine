/**
 * @file prefab_outliner_tests.c
 * @brief Tests for prefab outliner tree building and querying.
 *
 * Validates that prefab_outliner_build() constructs a correct bone
 * hierarchy with nested collider entities from a skeleton + entity store.
 */

#include "ferrum/editor/scene/prefab/prefab_outliner.h"
#include "ferrum/editor/scene/prefab/prefab_bone_parent.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/entity/entity_attrs.h"
#include "ferrum/animation/constraint_params.h"

#include <stdio.h>
#include <string.h>

static int g_pass, g_fail;

#define ASSERT(cond) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; \
           fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

/* ---- Helper: build a minimal skeleton ---- */

#define TEST_MAX_JOINTS 16

/** Minimal skeleton with named bones and parent indices. */
static void make_skeleton(skeleton_def_t *skel, uint32_t joint_count,
                          const char names[][SKELETON_JOINT_NAME_MAX],
                          const uint32_t *parents) {
    static char s_names[TEST_MAX_JOINTS][SKELETON_JOINT_NAME_MAX];
    static uint32_t s_parents[TEST_MAX_JOINTS];

    memset(skel, 0, sizeof(*skel));
    skel->joint_count = joint_count;

    for (uint32_t i = 0; i < joint_count && i < TEST_MAX_JOINTS; i++) {
        strncpy(s_names[i], names[i], SKELETON_JOINT_NAME_MAX - 1);
        s_names[i][SKELETON_JOINT_NAME_MAX - 1] = '\0';
        s_parents[i] = parents[i];
    }
    skel->joint_names = (char (*)[SKELETON_JOINT_NAME_MAX])s_names;
    skel->parent_indices = s_parents;
}

/* ---- Tests ---- */

/** Empty skeleton produces empty outliner. */
static void test_empty_skeleton(void) {
    prefab_outliner_t tree;
    prefab_outliner_init(&tree);

    skeleton_def_t skel;
    memset(&skel, 0, sizeof(skel));
    skel.joint_count = 0;

    edit_entity_store_t store;
    edit_entity_store_init(&store, 16);

    prefab_outliner_build(&tree, &skel, &store, 0);
    ASSERT(prefab_outliner_count(&tree) == 0);

    edit_entity_store_destroy(&store);
}

/** 3-bone skeleton with no colliders produces 3 bone entries. */
static void test_bones_only(void) {
    prefab_outliner_t tree;
    prefab_outliner_init(&tree);

    const char names[][SKELETON_JOINT_NAME_MAX] = {"Root", "Spine", "Head"};
    const uint32_t parents[] = {UINT32_MAX, 0, 1};
    skeleton_def_t skel;
    make_skeleton(&skel, 3, names, parents);

    edit_entity_store_t store;
    edit_entity_store_init(&store, 16);

    prefab_outliner_build(&tree, &skel, &store, 0);
    ASSERT(prefab_outliner_count(&tree) == 3);

    /* All entries should be bone entries. */
    for (uint32_t i = 0; i < 3; i++) {
        const prefab_outliner_entry_t *e = prefab_outliner_get(&tree, i);
        ASSERT(e != NULL);
        ASSERT(e->is_bone);
    }

    /* Check names. */
    ASSERT(strcmp(prefab_outliner_get(&tree, 0)->name, "Root") == 0);
    ASSERT(strcmp(prefab_outliner_get(&tree, 1)->name, "Spine") == 0);
    ASSERT(strcmp(prefab_outliner_get(&tree, 2)->name, "Head") == 0);

    edit_entity_store_destroy(&store);
}

/** Bone indices are correct in entries. */
static void test_bone_indices(void) {
    prefab_outliner_t tree;
    prefab_outliner_init(&tree);

    const char names[][SKELETON_JOINT_NAME_MAX] = {"A", "B", "C"};
    const uint32_t parents[] = {UINT32_MAX, 0, 0};
    skeleton_def_t skel;
    make_skeleton(&skel, 3, names, parents);

    edit_entity_store_t store;
    edit_entity_store_init(&store, 16);

    prefab_outliner_build(&tree, &skel, &store, 0);
    ASSERT(prefab_outliner_get(&tree, 0)->bone_index == 0);
    ASSERT(prefab_outliner_get(&tree, 1)->bone_index == 1);
    ASSERT(prefab_outliner_get(&tree, 2)->bone_index == 2);

    edit_entity_store_destroy(&store);
}

/** Indent levels match parent-child depth. */
static void test_hierarchy_indent(void) {
    prefab_outliner_t tree;
    prefab_outliner_init(&tree);

    /* Root(0) -> Spine(1) -> Head(2) */
    const char names[][SKELETON_JOINT_NAME_MAX] = {"Root", "Spine", "Head"};
    const uint32_t parents[] = {UINT32_MAX, 0, 1};
    skeleton_def_t skel;
    make_skeleton(&skel, 3, names, parents);

    edit_entity_store_t store;
    edit_entity_store_init(&store, 16);

    prefab_outliner_build(&tree, &skel, &store, 0);
    ASSERT(prefab_outliner_get(&tree, 0)->indent == 0); /* Root */
    ASSERT(prefab_outliner_get(&tree, 1)->indent == 1); /* Spine */
    ASSERT(prefab_outliner_get(&tree, 2)->indent == 2); /* Head */

    edit_entity_store_destroy(&store);
}

/** Colliders parented to bones appear after their bone with +1 indent. */
static void test_with_colliders(void) {
    prefab_outliner_t tree;
    prefab_outliner_init(&tree);

    const char names[][SKELETON_JOINT_NAME_MAX] = {"Root", "Arm", "Hand"};
    const uint32_t parents[] = {UINT32_MAX, 0, 1};
    skeleton_def_t skel;
    make_skeleton(&skel, 3, names, parents);

    edit_entity_store_t store;
    edit_entity_store_init(&store, 32);

    /* Create root entity (the prefab root). */
    uint32_t root_eid = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_MESH);

    /* Create two colliders parented to bone 1 ("Arm"). */
    uint32_t c1 = edit_entity_store_create(&store,
                                            EDIT_ENTITY_TYPE_COLLIDER_BOX);
    uint32_t c2 = edit_entity_store_create(&store,
                                            EDIT_ENTITY_TYPE_COLLIDER_SPHERE);
    prefab_parent_to_bone(&store, c1, root_eid, 1);
    prefab_parent_to_bone(&store, c2, root_eid, 1);

    prefab_outliner_build(&tree, &skel, &store, root_eid);

    /* Expected order: Root(bone), Arm(bone), c1(collider), c2(collider),
     *                 Hand(bone) */
    ASSERT(prefab_outliner_count(&tree) == 5);

    /* Root bone at indent 0. */
    const prefab_outliner_entry_t *e0 = prefab_outliner_get(&tree, 0);
    ASSERT(e0->is_bone && e0->bone_index == 0 && e0->indent == 0);

    /* Arm bone at indent 1. */
    const prefab_outliner_entry_t *e1 = prefab_outliner_get(&tree, 1);
    ASSERT(e1->is_bone && e1->bone_index == 1 && e1->indent == 1);

    /* Two colliders at indent 2 (bone indent + 1). */
    const prefab_outliner_entry_t *e2 = prefab_outliner_get(&tree, 2);
    ASSERT(!e2->is_bone && e2->bone_index == 1 && e2->indent == 2);
    ASSERT(e2->entity_id == c1);

    const prefab_outliner_entry_t *e3 = prefab_outliner_get(&tree, 3);
    ASSERT(!e3->is_bone && e3->bone_index == 1 && e3->indent == 2);
    ASSERT(e3->entity_id == c2);

    /* Hand bone at indent 2. */
    const prefab_outliner_entry_t *e4 = prefab_outliner_get(&tree, 4);
    ASSERT(e4->is_bone && e4->bone_index == 2 && e4->indent == 2);

    edit_entity_store_destroy(&store);
}

/** Entities not parented to root_entity_id are excluded. */
static void test_unparented_excluded(void) {
    prefab_outliner_t tree;
    prefab_outliner_init(&tree);

    const char names[][SKELETON_JOINT_NAME_MAX] = {"Root"};
    const uint32_t parents[] = {UINT32_MAX};
    skeleton_def_t skel;
    make_skeleton(&skel, 1, names, parents);

    edit_entity_store_t store;
    edit_entity_store_init(&store, 32);

    uint32_t root_eid = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_MESH);

    /* Create a collider parented to a DIFFERENT root entity. */
    uint32_t other_root = edit_entity_store_create(&store,
                                                    EDIT_ENTITY_TYPE_MESH);
    uint32_t c1 = edit_entity_store_create(&store,
                                            EDIT_ENTITY_TYPE_COLLIDER_BOX);
    prefab_parent_to_bone(&store, c1, other_root, 0);

    /* Also create a totally unparented entity. */
    edit_entity_store_create(&store, EDIT_ENTITY_TYPE_SPHERE);

    prefab_outliner_build(&tree, &skel, &store, root_eid);

    /* Only the bone entry, no colliders. */
    ASSERT(prefab_outliner_count(&tree) == 1);
    ASSERT(prefab_outliner_get(&tree, 0)->is_bone);

    edit_entity_store_destroy(&store);
}

/** Query out of range returns NULL. */
static void test_get_out_of_range(void) {
    prefab_outliner_t tree;
    prefab_outliner_init(&tree);
    ASSERT(prefab_outliner_get(&tree, 0) == NULL);
    ASSERT(prefab_outliner_get(&tree, 100) == NULL);
}

/** Init sets count to zero. */
static void test_init(void) {
    prefab_outliner_t tree;
    prefab_outliner_init(&tree);
    ASSERT(prefab_outliner_count(&tree) == 0);
}

/** Collider entity name appears in outliner entry. */
static void test_collider_name(void) {
    prefab_outliner_t tree;
    prefab_outliner_init(&tree);

    const char names[][SKELETON_JOINT_NAME_MAX] = {"Pelvis"};
    const uint32_t parents[] = {UINT32_MAX};
    skeleton_def_t skel;
    make_skeleton(&skel, 1, names, parents);

    edit_entity_store_t store;
    edit_entity_store_init(&store, 32);

    uint32_t root_eid = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_MESH);
    uint32_t coll = edit_entity_store_create(&store,
                                              EDIT_ENTITY_TYPE_COLLIDER_CAPSULE);
    /* Give the collider a name. */
    edit_entity_t *ent = edit_entity_store_get_mut(&store, coll);
    strncpy(ent->name, "hip_collider", EDIT_ENTITY_NAME_MAX - 1);

    prefab_parent_to_bone(&store, coll, root_eid, 0);
    prefab_outliner_build(&tree, &skel, &store, root_eid);

    ASSERT(prefab_outliner_count(&tree) == 2);
    const prefab_outliner_entry_t *ce = prefab_outliner_get(&tree, 1);
    ASSERT(!ce->is_bone);
    ASSERT(strcmp(ce->name, "hip_collider") == 0);

    edit_entity_store_destroy(&store);
}

/* ---- Main ---- */

int main(void) {
    printf("prefab_outliner_tests:\n");

    test_empty_skeleton();
    test_bones_only();
    test_bone_indices();
    test_hierarchy_indent();
    test_with_colliders();
    test_unparented_excluded();
    test_get_out_of_range();
    test_init();
    test_collider_name();

    printf("prefab_outliner_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
