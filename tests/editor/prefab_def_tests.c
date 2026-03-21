/**
 * @file prefab_def_tests.c
 * @brief Tests for prefab definition init/clear and entity snapshot layout.
 */

#include "ferrum/editor/scene/prefab/prefab_def.h"

#include <stdio.h>
#include <string.h>

static int g_pass, g_fail;

#define ASSERT(cond) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; \
           fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static void test_init(void) {
    prefab_def_t def;
    prefab_def_init(&def);
    ASSERT(def.version == PREFAB_VERSION);
    ASSERT(def.entity_count == 0);
    ASSERT(def.bone_count == 0);
    ASSERT(def.hull_vert_count == 0);
}

static void test_clear(void) {
    prefab_def_t def;
    prefab_def_init(&def);
    def.entity_count = 3;
    def.bone_count = 5;

    prefab_def_clear(&def);
    ASSERT(def.entity_count == 0);
    ASSERT(def.bone_count == 0);
    ASSERT(def.version == PREFAB_VERSION);
}

static void test_constants(void) {
    ASSERT(PREFAB_MAX_ENTITIES == 64);
    ASSERT(PREFAB_MAX_BONES == 256);
    ASSERT(PREFAB_MAX_HULL_VERTS == 4096);
    ASSERT(PREFAB_VERSION == 1);
}

static void test_entity_snapshot_fields(void) {
    prefab_entity_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    snap.type = 2;
    snap.pos[0] = 1.0f;
    snap.pos[1] = 2.0f;
    snap.pos[2] = 3.0f;
    snap.rot[0] = 45.0f;
    snap.scale[0] = 1.0f;
    snap.scale[1] = 1.0f;
    snap.scale[2] = 1.0f;
    snap.local_parent = -1;
    strcpy(snap.name, "test_entity");

    ASSERT(snap.type == 2);
    ASSERT(snap.pos[0] == 1.0f);
    ASSERT(snap.pos[1] == 2.0f);
    ASSERT(snap.pos[2] == 3.0f);
    ASSERT(snap.rot[0] == 45.0f);
    ASSERT(snap.scale[0] == 1.0f);
    ASSERT(snap.local_parent == -1);
    ASSERT(strcmp(snap.name, "test_entity") == 0);
}

static void test_bone_inline_fields(void) {
    prefab_def_t def;
    prefab_def_init(&def);
    def.bones[0].shape_type = 1; /* capsule */
    def.bones[0].params[0] = 0.5f;
    def.bones[0].mass = 2.0f;
    def.bones[0].collision_group = 3;
    def.bones[0].ccd_enabled = 1;
    def.bones[0].hull_offset = 10;
    def.bones[0].hull_count = 4;

    ASSERT(def.bones[0].shape_type == 1);
    ASSERT(def.bones[0].params[0] == 0.5f);
    ASSERT(def.bones[0].mass == 2.0f);
    ASSERT(def.bones[0].collision_group == 3);
    ASSERT(def.bones[0].ccd_enabled == 1);
    ASSERT(def.bones[0].hull_offset == 10);
    ASSERT(def.bones[0].hull_count == 4);
}

static void test_entity_with_attrs(void) {
    prefab_entity_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    entity_attrs_init(&snap.attrs);

    float fv = 3.14f;
    entity_attrs_set(&snap.attrs, 10, SCRIPT_ATTR_F32, &fv, sizeof(float));

    uint8_t type = 0, size = 0;
    const void *data = entity_attrs_get(&snap.attrs, 10, &type, &size);
    ASSERT(data != NULL);
    ASSERT(type == SCRIPT_ATTR_F32);

    float got;
    memcpy(&got, data, sizeof(float));
    ASSERT(got == 3.14f);
}

int main(void) {
    printf("prefab_def_tests:\n");
    test_init();
    test_clear();
    test_constants();
    test_entity_snapshot_fields();
    test_bone_inline_fields();
    test_entity_with_attrs();
    printf("prefab_def_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
