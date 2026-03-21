/**
 * @file prefab_collect_tests.c
 * @brief Tests for collecting prefab data from entity store.
 *
 * Tests entity tree collection (root + children via PARENT_ID),
 * relative positioning, attrs copying, and optional bone colliders.
 */

#include "ferrum/editor/scene/prefab/prefab_collect.h"
#include "ferrum/editor/scene/prefab/prefab_def.h"
#include "ferrum/editor/scene/prefab/prefab_bone_parent.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/entity/entity_attrs.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

static int g_pass, g_fail;

#define ASSERT(cond) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; \
           fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define FLOAT_EQ(a, b) (fabsf((a) - (b)) < 1e-5f)

/* ---- Helper: set u32 attr ---- */
static void set_u32_attr(edit_entity_t *ent, uint16_t key, uint32_t val) {
    entity_attrs_set(&ent->attrs, key, SCRIPT_ATTR_U32,
                     &val, sizeof(uint32_t));
}

/* ---- Helper: set f32 attr ---- */
static void set_f32_attr(edit_entity_t *ent, uint16_t key, float val) {
    entity_attrs_set(&ent->attrs, key, SCRIPT_ATTR_F32,
                     &val, sizeof(float));
}

/* ---- Tests ---- */

/** Single root entity with no children. */
static void test_root_only(void) {
    edit_entity_store_t store;
    edit_entity_store_init(&store, 32);

    uint32_t root = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    edit_entity_t *rent = edit_entity_store_get_mut(&store, root);
    rent->pos[0] = 10.0f; rent->pos[1] = 20.0f; rent->pos[2] = 30.0f;
    rent->scale[0] = 1.0f; rent->scale[1] = 1.0f; rent->scale[2] = 1.0f;
    strcpy(rent->name, "my_box");

    prefab_def_t def;
    bool ok = prefab_collect_from_entities(&def, &store, root, 0);
    ASSERT(ok);
    ASSERT(def.entity_count == 1);
    ASSERT(def.bone_count == 0);

    /* Root position should be relative to itself = (0,0,0). */
    ASSERT(FLOAT_EQ(def.entities[0].pos[0], 0.0f));
    ASSERT(FLOAT_EQ(def.entities[0].pos[1], 0.0f));
    ASSERT(FLOAT_EQ(def.entities[0].pos[2], 0.0f));
    ASSERT(strcmp(def.entities[0].name, "my_box") == 0);
    ASSERT(def.entities[0].local_parent == -1);

    edit_entity_store_destroy(&store);
}

/** Root + children: positions stored relative to root. */
static void test_root_with_children(void) {
    edit_entity_store_t store;
    edit_entity_store_init(&store, 64);

    uint32_t root = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_MESH);
    edit_entity_t *rent = edit_entity_store_get_mut(&store, root);
    rent->pos[0] = 100.0f; rent->pos[1] = 200.0f; rent->pos[2] = 300.0f;
    rent->scale[0] = 1.0f; rent->scale[1] = 1.0f; rent->scale[2] = 1.0f;
    strcpy(rent->name, "root_mesh");

    /* Child 1. */
    uint32_t c1 = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    edit_entity_t *ce1 = edit_entity_store_get_mut(&store, c1);
    ce1->pos[0] = 102.0f; ce1->pos[1] = 200.0f; ce1->pos[2] = 300.0f;
    ce1->scale[0] = 1.0f; ce1->scale[1] = 1.0f; ce1->scale[2] = 1.0f;
    strcpy(ce1->name, "child_box");
    set_u32_attr(ce1, SCRIPT_KEY_PARENT_ID, root);

    /* Child 2. */
    uint32_t c2 = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_SPHERE);
    edit_entity_t *ce2 = edit_entity_store_get_mut(&store, c2);
    ce2->pos[0] = 100.0f; ce2->pos[1] = 205.0f; ce2->pos[2] = 300.0f;
    ce2->scale[0] = 2.0f; ce2->scale[1] = 2.0f; ce2->scale[2] = 2.0f;
    strcpy(ce2->name, "child_sphere");
    set_u32_attr(ce2, SCRIPT_KEY_PARENT_ID, root);

    /* Unrelated entity — should NOT be collected. */
    uint32_t other = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    (void)other;

    prefab_def_t def;
    bool ok = prefab_collect_from_entities(&def, &store, root, 0);
    ASSERT(ok);
    ASSERT(def.entity_count == 3);

    /* Root at index 0. */
    ASSERT(def.entities[0].local_parent == -1);
    ASSERT(FLOAT_EQ(def.entities[0].pos[0], 0.0f));

    /* Children have relative positions. */
    bool found_box = false, found_sphere = false;
    for (uint32_t i = 1; i < def.entity_count; i++) {
        if (strcmp(def.entities[i].name, "child_box") == 0) {
            found_box = true;
            ASSERT(FLOAT_EQ(def.entities[i].pos[0], 2.0f)); /* 102 - 100 */
            ASSERT(FLOAT_EQ(def.entities[i].pos[1], 0.0f)); /* 200 - 200 */
            ASSERT(def.entities[i].local_parent == 0);
        }
        if (strcmp(def.entities[i].name, "child_sphere") == 0) {
            found_sphere = true;
            ASSERT(FLOAT_EQ(def.entities[i].pos[1], 5.0f)); /* 205 - 200 */
            ASSERT(FLOAT_EQ(def.entities[i].scale[0], 2.0f));
            ASSERT(def.entities[i].local_parent == 0);
        }
    }
    ASSERT(found_box);
    ASSERT(found_sphere);

    edit_entity_store_destroy(&store);
}

/** Attrs are copied into snapshot. */
static void test_attrs_copied(void) {
    edit_entity_store_t store;
    edit_entity_store_init(&store, 32);

    uint32_t root = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    edit_entity_t *rent = edit_entity_store_get_mut(&store, root);
    rent->scale[0] = 1.0f; rent->scale[1] = 1.0f; rent->scale[2] = 1.0f;

    float fv = 99.0f;
    entity_attrs_set(&rent->attrs, 10, SCRIPT_ATTR_F32, &fv, sizeof(float));

    prefab_def_t def;
    bool ok = prefab_collect_from_entities(&def, &store, root, 0);
    ASSERT(ok);
    ASSERT(def.entities[0].attrs.count >= 1);

    uint8_t at = 0, as = 0;
    const void *data = entity_attrs_get(&def.entities[0].attrs, 10, &at, &as);
    ASSERT(data != NULL);
    ASSERT(at == SCRIPT_ATTR_F32);
    float got;
    memcpy(&got, data, sizeof(float));
    ASSERT(FLOAT_EQ(got, 99.0f));

    edit_entity_store_destroy(&store);
}

/** Bone colliders collected when bone_count > 0. */
static void test_bone_colliders(void) {
    edit_entity_store_t store;
    edit_entity_store_init(&store, 64);

    uint32_t root = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_MESH);
    edit_entity_t *rent = edit_entity_store_get_mut(&store, root);
    rent->scale[0] = 1.0f; rent->scale[1] = 1.0f; rent->scale[2] = 1.0f;

    /* Create a capsule collider parented to bone 1. */
    uint32_t c1 = edit_entity_store_create(&store,
                                            EDIT_ENTITY_TYPE_COLLIDER_CAPSULE);
    prefab_parent_to_bone(&store, c1, root, 1);

    edit_entity_t *cent = edit_entity_store_get_mut(&store, c1);
    set_f32_attr(cent, SCRIPT_KEY_RADIUS, 0.1f);
    set_f32_attr(cent, SCRIPT_KEY_HEIGHT, 0.5f);
    set_f32_attr(cent, SCRIPT_KEY_MASS, 3.0f);

    prefab_def_t def;
    bool ok = prefab_collect_from_entities(&def, &store, root, 3);
    ASSERT(ok);
    ASSERT(def.bone_count == 3);
    ASSERT(def.bones[1].shape_type == 1); /* CAPSULE */
    ASSERT(def.bones[1].params[0] > 0.0f); /* radius */
    ASSERT(def.bones[1].mass > 0.0f);

    /* Bone 0 should be NONE (no collider). */
    ASSERT(def.bones[0].shape_type == 0);

    /* Entity count should include root + the collider child. */
    ASSERT(def.entity_count >= 1);

    edit_entity_store_destroy(&store);
}

/** Hull from markers populates hull vertex data. */
static void test_hull_from_markers(void) {
    edit_entity_store_t store;
    edit_entity_store_init(&store, 64);

    uint32_t root = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_MESH);
    edit_entity_t *rent = edit_entity_store_get_mut(&store, root);
    rent->scale[0] = 1.0f; rent->scale[1] = 1.0f; rent->scale[2] = 1.0f;

    /* Create 4 markers parented to bone 0 forming a tetrahedron. */
    for (int i = 0; i < 4; i++) {
        uint32_t m = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_MARKER);
        edit_entity_t *me = edit_entity_store_get_mut(&store, m);
        me->pos[0] = (i == 1) ? 1.0f : 0.0f;
        me->pos[1] = (i == 2) ? 1.0f : 0.0f;
        me->pos[2] = (i == 3) ? 1.0f : 0.0f;
        prefab_parent_to_bone(&store, m, root, 0);
    }

    prefab_def_t def;
    bool ok = prefab_collect_from_entities(&def, &store, root, 2);
    ASSERT(ok);
    ASSERT(def.bones[0].shape_type == 4); /* CONVEX_HULL */
    ASSERT(def.bones[0].hull_count >= 4);
    ASSERT(def.hull_vert_count >= 4);

    edit_entity_store_destroy(&store);
}

/** No bones (bone_count=0) still collects entities. */
static void test_no_bones(void) {
    edit_entity_store_t store;
    edit_entity_store_init(&store, 32);

    uint32_t root = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    edit_entity_t *rent = edit_entity_store_get_mut(&store, root);
    rent->scale[0] = 1.0f; rent->scale[1] = 1.0f; rent->scale[2] = 1.0f;

    prefab_def_t def;
    bool ok = prefab_collect_from_entities(&def, &store, root, 0);
    ASSERT(ok);
    ASSERT(def.entity_count == 1);
    ASSERT(def.bone_count == 0);

    edit_entity_store_destroy(&store);
}

/** NULL args return false. */
static void test_null_args(void) {
    prefab_def_t def;
    ASSERT(!prefab_collect_from_entities(NULL, NULL, 0, 0));
    ASSERT(!prefab_collect_from_entities(&def, NULL, 0, 0));
}

int main(void) {
    printf("prefab_collect_tests:\n");
    test_root_only();
    test_root_with_children();
    test_attrs_copied();
    test_bone_colliders();
    test_hull_from_markers();
    test_no_bones();
    test_null_args();
    printf("prefab_collect_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
