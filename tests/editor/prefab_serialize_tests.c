/**
 * @file prefab_serialize_tests.c
 * @brief Tests for .fpfab JSON serialization and deserialization.
 *
 * Tests the entity-tree prefab format: entities with transform, name,
 * local_parent, attrs, plus optional bones and hull vertices.
 */

#include "ferrum/editor/scene/prefab/prefab_def.h"
#include "ferrum/editor/scene/prefab/prefab_save.h"
#include "ferrum/editor/scene/prefab/prefab_load.h"
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

/** Serialize buffer. */
static char s_buf[65536];

/** Empty prefab (no entities, no bones) serializes and deserializes. */
static void test_empty(void) {
    prefab_def_t def;
    prefab_def_init(&def);

    size_t len = prefab_serialize(&def, s_buf, sizeof(s_buf));
    ASSERT(len > 0);
    ASSERT(len < sizeof(s_buf));

    prefab_def_t out;
    bool ok = prefab_deserialize(s_buf, len, &out);
    ASSERT(ok);
    ASSERT(out.entity_count == 0);
    ASSERT(out.bone_count == 0);
    ASSERT(out.hull_vert_count == 0);
    ASSERT(out.version == PREFAB_VERSION);
}

/** Single entity roundtrip with transform and name. */
static void test_single_entity(void) {
    prefab_def_t def;
    prefab_def_init(&def);
    def.entity_count = 1;

    prefab_entity_snapshot_t *snap = &def.entities[0];
    memset(snap, 0, sizeof(*snap));
    entity_attrs_init(&snap->attrs);
    snap->type = 5;
    snap->pos[0] = 1.0f; snap->pos[1] = 2.0f; snap->pos[2] = 3.0f;
    snap->rot[0] = 45.0f; snap->rot[1] = 0.0f; snap->rot[2] = 90.0f;
    snap->scale[0] = 1.0f; snap->scale[1] = 2.0f; snap->scale[2] = 0.5f;
    strcpy(snap->name, "root_box");
    snap->local_parent = -1;

    size_t len = prefab_serialize(&def, s_buf, sizeof(s_buf));
    ASSERT(len > 0);

    prefab_def_t out;
    bool ok = prefab_deserialize(s_buf, len, &out);
    ASSERT(ok);
    ASSERT(out.entity_count == 1);
    ASSERT(out.entities[0].type == 5);
    ASSERT(FLOAT_EQ(out.entities[0].pos[0], 1.0f));
    ASSERT(FLOAT_EQ(out.entities[0].pos[1], 2.0f));
    ASSERT(FLOAT_EQ(out.entities[0].pos[2], 3.0f));
    ASSERT(FLOAT_EQ(out.entities[0].rot[0], 45.0f));
    ASSERT(FLOAT_EQ(out.entities[0].rot[2], 90.0f));
    ASSERT(FLOAT_EQ(out.entities[0].scale[1], 2.0f));
    ASSERT(strcmp(out.entities[0].name, "root_box") == 0);
    ASSERT(out.entities[0].local_parent == -1);
}

/** Entity hierarchy: root + 2 children. */
static void test_entity_hierarchy(void) {
    prefab_def_t def;
    prefab_def_init(&def);
    def.entity_count = 3;

    /* Root. */
    memset(&def.entities[0], 0, sizeof(def.entities[0]));
    entity_attrs_init(&def.entities[0].attrs);
    def.entities[0].type = 5;
    strcpy(def.entities[0].name, "root");
    def.entities[0].local_parent = -1;
    def.entities[0].scale[0] = 1.0f;
    def.entities[0].scale[1] = 1.0f;
    def.entities[0].scale[2] = 1.0f;

    /* Child 1. */
    memset(&def.entities[1], 0, sizeof(def.entities[1]));
    entity_attrs_init(&def.entities[1].attrs);
    def.entities[1].type = 1;
    def.entities[1].pos[0] = 2.0f;
    strcpy(def.entities[1].name, "child_a");
    def.entities[1].local_parent = 0;
    def.entities[1].scale[0] = 1.0f;
    def.entities[1].scale[1] = 1.0f;
    def.entities[1].scale[2] = 1.0f;

    /* Child 2. */
    memset(&def.entities[2], 0, sizeof(def.entities[2]));
    entity_attrs_init(&def.entities[2].attrs);
    def.entities[2].type = 2;
    def.entities[2].pos[1] = 5.0f;
    strcpy(def.entities[2].name, "child_b");
    def.entities[2].local_parent = 0;
    def.entities[2].scale[0] = 1.0f;
    def.entities[2].scale[1] = 1.0f;
    def.entities[2].scale[2] = 1.0f;

    size_t len = prefab_serialize(&def, s_buf, sizeof(s_buf));
    ASSERT(len > 0);

    prefab_def_t out;
    bool ok = prefab_deserialize(s_buf, len, &out);
    ASSERT(ok);
    ASSERT(out.entity_count == 3);
    ASSERT(out.entities[0].local_parent == -1);
    ASSERT(out.entities[1].local_parent == 0);
    ASSERT(out.entities[2].local_parent == 0);
    ASSERT(strcmp(out.entities[1].name, "child_a") == 0);
    ASSERT(strcmp(out.entities[2].name, "child_b") == 0);
    ASSERT(FLOAT_EQ(out.entities[1].pos[0], 2.0f));
    ASSERT(FLOAT_EQ(out.entities[2].pos[1], 5.0f));
}

/** Entity attrs roundtrip (f32, u32, bool). */
static void test_entity_attrs_roundtrip(void) {
    prefab_def_t def;
    prefab_def_init(&def);
    def.entity_count = 1;

    prefab_entity_snapshot_t *snap = &def.entities[0];
    memset(snap, 0, sizeof(*snap));
    entity_attrs_init(&snap->attrs);
    snap->type = 1;
    snap->local_parent = -1;
    snap->scale[0] = 1.0f;
    snap->scale[1] = 1.0f;
    snap->scale[2] = 1.0f;

    /* Set some attrs. */
    float fv = 42.5f;
    entity_attrs_set(&snap->attrs, 10, SCRIPT_ATTR_F32, &fv, sizeof(float));
    uint32_t uv = 99;
    entity_attrs_set(&snap->attrs, 11, SCRIPT_ATTR_U32, &uv, sizeof(uint32_t));
    uint8_t bv = 1;
    entity_attrs_set(&snap->attrs, 12, SCRIPT_ATTR_BOOL, &bv, 1);

    size_t len = prefab_serialize(&def, s_buf, sizeof(s_buf));
    ASSERT(len > 0);

    prefab_def_t out;
    bool ok = prefab_deserialize(s_buf, len, &out);
    ASSERT(ok);
    ASSERT(out.entity_count == 1);
    ASSERT(out.entities[0].attrs.count == 3);

    /* Check f32 attr. */
    uint8_t at = 0, as = 0;
    const void *data = entity_attrs_get(&out.entities[0].attrs, 10, &at, &as);
    ASSERT(data != NULL);
    ASSERT(at == SCRIPT_ATTR_F32);
    float got_f;
    memcpy(&got_f, data, sizeof(float));
    ASSERT(FLOAT_EQ(got_f, 42.5f));

    /* Check u32 attr. */
    data = entity_attrs_get(&out.entities[0].attrs, 11, &at, &as);
    ASSERT(data != NULL);
    ASSERT(at == SCRIPT_ATTR_U32);
    uint32_t got_u;
    memcpy(&got_u, data, sizeof(uint32_t));
    ASSERT(got_u == 99);

    /* Check bool attr. */
    data = entity_attrs_get(&out.entities[0].attrs, 12, &at, &as);
    ASSERT(data != NULL);
    ASSERT(at == SCRIPT_ATTR_BOOL);
    ASSERT(*(const uint8_t *)data == 1);
}

/** Bone collider roundtrip (entities + bones). */
static void test_bones_roundtrip(void) {
    prefab_def_t def;
    prefab_def_init(&def);
    def.entity_count = 1;

    memset(&def.entities[0], 0, sizeof(def.entities[0]));
    entity_attrs_init(&def.entities[0].attrs);
    def.entities[0].type = 5;
    def.entities[0].local_parent = -1;
    def.entities[0].scale[0] = 1.0f;
    def.entities[0].scale[1] = 1.0f;
    def.entities[0].scale[2] = 1.0f;

    def.bone_count = 2;
    def.bones[0].shape_type = 1; /* CAPSULE */
    def.bones[0].params[0] = 0.1f;
    def.bones[0].params[1] = 0.5f;
    def.bones[0].mass = 2.5f;
    def.bones[0].collision_group = 1;
    def.bones[0].ccd_enabled = 0;

    def.bones[1].shape_type = 3; /* SPHERE */
    def.bones[1].params[0] = 0.2f;
    def.bones[1].mass = 1.0f;
    def.bones[1].collision_group = 0;
    def.bones[1].ccd_enabled = 1;

    size_t len = prefab_serialize(&def, s_buf, sizeof(s_buf));
    ASSERT(len > 0);

    prefab_def_t out;
    bool ok = prefab_deserialize(s_buf, len, &out);
    ASSERT(ok);
    ASSERT(out.bone_count == 2);
    ASSERT(out.bones[0].shape_type == 1);
    ASSERT(FLOAT_EQ(out.bones[0].params[0], 0.1f));
    ASSERT(FLOAT_EQ(out.bones[0].params[1], 0.5f));
    ASSERT(FLOAT_EQ(out.bones[0].mass, 2.5f));
    ASSERT(out.bones[0].collision_group == 1);
    ASSERT(out.bones[0].ccd_enabled == 0);

    ASSERT(out.bones[1].shape_type == 3);
    ASSERT(FLOAT_EQ(out.bones[1].params[0], 0.2f));
    ASSERT(FLOAT_EQ(out.bones[1].mass, 1.0f));
    ASSERT(out.bones[1].ccd_enabled == 1);
}

/** Hull vertex roundtrip. */
static void test_hull_vertices(void) {
    prefab_def_t def;
    prefab_def_init(&def);
    def.bone_count = 1;
    def.bones[0].shape_type = 4; /* CONVEX_HULL */
    def.bones[0].hull_offset = 0;
    def.bones[0].hull_count = 4;

    /* 4 hull vertices (tetrahedron). */
    float verts[] = {
        0,0,0,  1,0,0,  0,1,0,  0,0,1
    };
    memcpy(def.hull_verts, verts, sizeof(verts));
    def.hull_vert_count = 4;

    size_t len = prefab_serialize(&def, s_buf, sizeof(s_buf));
    ASSERT(len > 0);

    prefab_def_t out;
    bool ok = prefab_deserialize(s_buf, len, &out);
    ASSERT(ok);
    ASSERT(out.hull_vert_count == 4);
    ASSERT(out.bones[0].hull_count == 4);
    ASSERT(FLOAT_EQ(out.hull_verts[3], 1.0f)); /* x of vertex 1 */
    ASSERT(FLOAT_EQ(out.hull_verts[7], 1.0f)); /* y of vertex 2 */
    ASSERT(FLOAT_EQ(out.hull_verts[11], 1.0f)); /* z of vertex 3 */
}

/** Invalid JSON returns false. */
static void test_invalid_json(void) {
    prefab_def_t out;
    bool ok = prefab_deserialize("{not valid json!!", 17, &out);
    ASSERT(!ok);
}

/** Wrong version returns false. */
static void test_wrong_version(void) {
    const char *json = "{\"version\":999,\"entities\":[],\"bones\":[],\"hull_verts\":[]}";
    prefab_def_t out;
    bool ok = prefab_deserialize(json, strlen(json), &out);
    ASSERT(!ok);
}

/** NULL args return safe defaults. */
static void test_null_args(void) {
    ASSERT(prefab_serialize(NULL, s_buf, sizeof(s_buf)) == 0);
    ASSERT(!prefab_deserialize(NULL, 0, NULL));
}

/** Buffer too small returns needed size but doesn't crash. */
static void test_small_buffer(void) {
    prefab_def_t def;
    prefab_def_init(&def);

    /* Tiny buffer. */
    char tiny[8];
    size_t needed = prefab_serialize(&def, tiny, sizeof(tiny));
    ASSERT(needed > sizeof(tiny));
}

/** Vec3 attr roundtrip. */
static void test_vec3_attr_roundtrip(void) {
    prefab_def_t def;
    prefab_def_init(&def);
    def.entity_count = 1;

    prefab_entity_snapshot_t *snap = &def.entities[0];
    memset(snap, 0, sizeof(*snap));
    entity_attrs_init(&snap->attrs);
    snap->type = 1;
    snap->local_parent = -1;
    snap->scale[0] = 1.0f;
    snap->scale[1] = 1.0f;
    snap->scale[2] = 1.0f;

    float vec[3] = {1.0f, 2.0f, 3.0f};
    entity_attrs_set(&snap->attrs, 15, SCRIPT_ATTR_VEC3, vec, 12);

    size_t len = prefab_serialize(&def, s_buf, sizeof(s_buf));
    ASSERT(len > 0);

    prefab_def_t out;
    bool ok = prefab_deserialize(s_buf, len, &out);
    ASSERT(ok);

    uint8_t at = 0, as = 0;
    const void *data = entity_attrs_get(&out.entities[0].attrs, 15, &at, &as);
    ASSERT(data != NULL);
    ASSERT(at == SCRIPT_ATTR_VEC3);
    const float *gv = (const float *)data;
    ASSERT(FLOAT_EQ(gv[0], 1.0f));
    ASSERT(FLOAT_EQ(gv[1], 2.0f));
    ASSERT(FLOAT_EQ(gv[2], 3.0f));
}

/** String attr roundtrip. */
static void test_str_attr_roundtrip(void) {
    prefab_def_t def;
    prefab_def_init(&def);
    def.entity_count = 1;

    prefab_entity_snapshot_t *snap = &def.entities[0];
    memset(snap, 0, sizeof(*snap));
    entity_attrs_init(&snap->attrs);
    snap->type = 1;
    snap->local_parent = -1;
    snap->scale[0] = 1.0f;
    snap->scale[1] = 1.0f;
    snap->scale[2] = 1.0f;

    const char *str = "hello_prefab";
    entity_attrs_set(&snap->attrs, 20, SCRIPT_ATTR_STR,
                     str, (uint8_t)(strlen(str) + 1));

    size_t len = prefab_serialize(&def, s_buf, sizeof(s_buf));
    ASSERT(len > 0);

    prefab_def_t out;
    bool ok = prefab_deserialize(s_buf, len, &out);
    ASSERT(ok);

    uint8_t at = 0, as = 0;
    const void *data = entity_attrs_get(&out.entities[0].attrs, 20, &at, &as);
    ASSERT(data != NULL);
    ASSERT(at == SCRIPT_ATTR_STR);
    ASSERT(strcmp((const char *)data, "hello_prefab") == 0);
}

int main(void) {
    printf("prefab_serialize_tests:\n");
    test_empty();
    test_single_entity();
    test_entity_hierarchy();
    test_entity_attrs_roundtrip();
    test_bones_roundtrip();
    test_hull_vertices();
    test_invalid_json();
    test_wrong_version();
    test_null_args();
    test_small_buffer();
    test_vec3_attr_roundtrip();
    test_str_attr_roundtrip();
    printf("prefab_serialize_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
