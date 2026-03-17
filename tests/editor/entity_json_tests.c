/**
 * @file entity_json_tests.c
 * @brief Tests for entity JSON serialization and deserialization.
 *
 * Verifies round-trip fidelity: entity → JSON → entity must preserve
 * all fields including dynamic attributes.
 */

#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_entity_json.h"
#include "ferrum/entity/entity_attrs.h"
#include "ferrum/editor/json_parse.h"
#include "ferrum/math/quat.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---- Test helpers ---- */

static int s_pass, s_fail;

#define ASSERT(cond) do { \
    if (cond) { s_pass++; } \
    else { s_fail++; fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define ASSERT_FLOAT_EQ(a, b) ASSERT(fabsf((a) - (b)) < 1e-5f)

#define ARENA_SIZE (1024 * 64)
static uint8_t s_arena_buf[ARENA_SIZE];

/* ---- Tests ---- */

/**
 * @brief Test serialization of an entity with all static fields set.
 */
static void test_serialize_all_static_fields(void) {
    edit_entity_t ent;
    memset(&ent, 0, sizeof(ent));
    ent.active = true;
    ent.type = EDIT_ENTITY_TYPE_MESH;
    strncpy(ent.name, "test_entity", sizeof(ent.name) - 1);
    ent.pos[0] = 1.0f; ent.pos[1] = 2.0f; ent.pos[2] = 3.0f;
    ent.rot[0] = 10.0f; ent.rot[1] = 20.0f; ent.rot[2] = 30.0f;
    ent.scale[0] = 0.5f; ent.scale[1] = 1.5f; ent.scale[2] = 2.5f;
    ent.orientation = (quat_t){0.1f, 0.2f, 0.3f, 0.9f};
    ent.pivot_offset[0] = 0.0f; ent.pivot_offset[1] = 1.0f; ent.pivot_offset[2] = 0.0f;
    ent.body_index = 42;
    ent.hidden = true;
    ent.pending_delete = false;
    strncpy(ent.materials[0], "textures/albedo.png", EDIT_MATERIAL_PATH_MAX - 1);
    strncpy(ent.materials[1], "textures/normal.png", EDIT_MATERIAL_PATH_MAX - 1);
    entity_attrs_init(&ent.attrs);

    json_arena_t arena;
    json_arena_init(&arena, s_arena_buf, sizeof(s_arena_buf));

    json_value_t out;
    bool ok = edit_entity_json_build(&ent, 7, &out, &arena);
    ASSERT(ok);
    ASSERT(out.type == JSON_OBJECT);

    /* Check that key fields exist. */
    const json_value_t *id_v = json_object_get(&out, "id");
    ASSERT(id_v && id_v->type == JSON_NUMBER);
    ASSERT((uint32_t)id_v->number == 7);

    const json_value_t *name_v = json_object_get(&out, "name");
    ASSERT(name_v && name_v->type == JSON_STRING);

    const json_value_t *type_v = json_object_get(&out, "type");
    ASSERT(type_v && type_v->type == JSON_STRING);

    const json_value_t *pos_v = json_object_get(&out, "pos");
    ASSERT(pos_v && pos_v->type == JSON_ARRAY && pos_v->array.count == 3);
    ASSERT_FLOAT_EQ((float)pos_v->array.items[0].number, 1.0f);
    ASSERT_FLOAT_EQ((float)pos_v->array.items[1].number, 2.0f);
    ASSERT_FLOAT_EQ((float)pos_v->array.items[2].number, 3.0f);

    const json_value_t *orient_v = json_object_get(&out, "orient");
    ASSERT(orient_v && orient_v->type == JSON_ARRAY && orient_v->array.count == 4);

    const json_value_t *scale_v = json_object_get(&out, "scale");
    ASSERT(scale_v && scale_v->type == JSON_ARRAY && scale_v->array.count == 3);

    const json_value_t *rot_v = json_object_get(&out, "rot");
    ASSERT(rot_v && rot_v->type == JSON_ARRAY && rot_v->array.count == 3);

    const json_value_t *pivot_v = json_object_get(&out, "pivot_offset");
    ASSERT(pivot_v && pivot_v->type == JSON_ARRAY && pivot_v->array.count == 3);
    ASSERT_FLOAT_EQ((float)pivot_v->array.items[1].number, 1.0f);

    const json_value_t *bi_v = json_object_get(&out, "body_index");
    ASSERT(bi_v && bi_v->type == JSON_NUMBER);
    ASSERT((uint32_t)bi_v->number == 42);

    const json_value_t *hid_v = json_object_get(&out, "hidden");
    ASSERT(hid_v && hid_v->type == JSON_BOOL && hid_v->boolean == true);

    const json_value_t *pd_v = json_object_get(&out, "pending_delete");
    ASSERT(pd_v && pd_v->type == JSON_BOOL && pd_v->boolean == false);

    const json_value_t *mat_v = json_object_get(&out, "materials");
    ASSERT(mat_v && mat_v->type == JSON_ARRAY);
    ASSERT(mat_v->array.count == EDIT_MATERIAL_SLOT_COUNT);

    const json_value_t *attrs_v = json_object_get(&out, "attrs");
    ASSERT(attrs_v && attrs_v->type == JSON_ARRAY);
    ASSERT(attrs_v->array.count == 0); /* No dynamic attrs set. */
}

/**
 * @brief Test serialization of dynamic attributes.
 */
static void test_serialize_dynamic_attrs(void) {
    edit_entity_t ent;
    memset(&ent, 0, sizeof(ent));
    ent.active = true;
    ent.type = EDIT_ENTITY_TYPE_MESH;
    ent.scale[0] = ent.scale[1] = ent.scale[2] = 1.0f;
    ent.orientation = (quat_t){0, 0, 0, 1};
    ent.body_index = UINT32_MAX;
    entity_attrs_init(&ent.attrs);

    /* Set a string attr (mesh_path). */
    const char *mp = "models/humanoid.fvma";
    entity_attrs_set(&ent.attrs, SCRIPT_KEY_MESH_PATH,
                     SCRIPT_ATTR_STR, mp, (uint8_t)(strlen(mp) + 1));

    /* Set a float attr. */
    float mass = 75.0f;
    entity_attrs_set(&ent.attrs, SCRIPT_KEY_MASS,
                     SCRIPT_ATTR_F32, &mass, sizeof(float));

    /* Set a bool attr. */
    uint8_t ccd = 1;
    entity_attrs_set(&ent.attrs, SCRIPT_KEY_CCD,
                     SCRIPT_ATTR_BOOL, &ccd, sizeof(uint8_t));

    /* Set a u32 attr. */
    uint32_t tier = 2;
    entity_attrs_set(&ent.attrs, SCRIPT_KEY_TIER,
                     SCRIPT_ATTR_U32, &tier, sizeof(uint32_t));

    json_arena_t arena;
    json_arena_init(&arena, s_arena_buf, sizeof(s_arena_buf));

    json_value_t out;
    bool ok = edit_entity_json_build(&ent, 0, &out, &arena);
    ASSERT(ok);

    const json_value_t *attrs_v = json_object_get(&out, "attrs");
    ASSERT(attrs_v && attrs_v->type == JSON_ARRAY);
    ASSERT(attrs_v->array.count == 4); /* 4 dynamic attrs. */

    /* Each attr entry is [key, type, value]. */
    for (uint32_t i = 0; i < attrs_v->array.count; i++) {
        const json_value_t *entry = &attrs_v->array.items[i];
        ASSERT(entry->type == JSON_ARRAY);
        ASSERT(entry->array.count == 3);
        ASSERT(entry->array.items[0].type == JSON_NUMBER); /* key */
        ASSERT(entry->array.items[1].type == JSON_NUMBER); /* type */
    }
}

/**
 * @brief Test round-trip: serialize then parse, verify all static fields match.
 */
static void test_round_trip_static_fields(void) {
    edit_entity_t orig;
    memset(&orig, 0, sizeof(orig));
    orig.active = true;
    orig.type = EDIT_ENTITY_TYPE_SPHERE;
    strncpy(orig.name, "round_trip_test", sizeof(orig.name) - 1);
    orig.pos[0] = -5.0f; orig.pos[1] = 0.0f; orig.pos[2] = 10.0f;
    orig.rot[0] = 45.0f; orig.rot[1] = 90.0f; orig.rot[2] = 0.0f;
    orig.scale[0] = 2.0f; orig.scale[1] = 2.0f; orig.scale[2] = 2.0f;
    orig.orientation = (quat_t){0.0f, 0.707f, 0.0f, 0.707f};
    orig.pivot_offset[0] = 0.5f; orig.pivot_offset[1] = 0.0f; orig.pivot_offset[2] = -0.5f;
    orig.body_index = 99;
    orig.hidden = false;
    orig.pending_delete = true;
    strncpy(orig.materials[EDIT_MATERIAL_SLOT_ALBEDO], "mat/stone.png",
            EDIT_MATERIAL_PATH_MAX - 1);
    entity_attrs_init(&orig.attrs);

    /* Serialize. */
    json_arena_t arena;
    json_arena_init(&arena, s_arena_buf, sizeof(s_arena_buf));

    json_value_t json_obj;
    bool ok = edit_entity_json_build(&orig, 42, &json_obj, &arena);
    ASSERT(ok);

    /* Parse back. */
    edit_entity_t parsed;
    edit_entity_json_parse(&json_obj, &parsed);

    /* Verify static fields. */
    ASSERT(parsed.active == true);
    ASSERT(strcmp(parsed.name, "round_trip_test") == 0);
    ASSERT_FLOAT_EQ(parsed.pos[0], -5.0f);
    ASSERT_FLOAT_EQ(parsed.pos[1], 0.0f);
    ASSERT_FLOAT_EQ(parsed.pos[2], 10.0f);
    ASSERT_FLOAT_EQ(parsed.rot[0], 45.0f);
    ASSERT_FLOAT_EQ(parsed.rot[1], 90.0f);
    ASSERT_FLOAT_EQ(parsed.rot[2], 0.0f);
    ASSERT_FLOAT_EQ(parsed.scale[0], 2.0f);
    ASSERT_FLOAT_EQ(parsed.scale[1], 2.0f);
    ASSERT_FLOAT_EQ(parsed.scale[2], 2.0f);
    ASSERT_FLOAT_EQ(parsed.orientation.x, 0.0f);
    ASSERT_FLOAT_EQ(parsed.orientation.y, 0.707f);
    ASSERT_FLOAT_EQ(parsed.orientation.z, 0.0f);
    ASSERT_FLOAT_EQ(parsed.orientation.w, 0.707f);
    ASSERT_FLOAT_EQ(parsed.pivot_offset[0], 0.5f);
    ASSERT_FLOAT_EQ(parsed.pivot_offset[2], -0.5f);
    ASSERT(parsed.body_index == 99);
    ASSERT(parsed.hidden == false);
    ASSERT(parsed.pending_delete == true);
    ASSERT(strcmp(parsed.materials[EDIT_MATERIAL_SLOT_ALBEDO], "mat/stone.png") == 0);
    /* Other material slots should be empty. */
    ASSERT(parsed.materials[1][0] == '\0');
}

/**
 * @brief Test round-trip of dynamic attributes.
 */
static void test_round_trip_dynamic_attrs(void) {
    edit_entity_t orig;
    memset(&orig, 0, sizeof(orig));
    orig.active = true;
    orig.type = EDIT_ENTITY_TYPE_MESH;
    orig.scale[0] = orig.scale[1] = orig.scale[2] = 1.0f;
    orig.orientation = (quat_t){0, 0, 0, 1};
    orig.body_index = UINT32_MAX;
    entity_attrs_init(&orig.attrs);

    /* Set various attr types. */
    const char *mesh = "goblin.fvma";
    entity_attrs_set(&orig.attrs, SCRIPT_KEY_MESH_PATH,
                     SCRIPT_ATTR_STR, mesh, (uint8_t)(strlen(mesh) + 1));

    float mass = 50.0f;
    entity_attrs_set(&orig.attrs, SCRIPT_KEY_MASS,
                     SCRIPT_ATTR_F32, &mass, sizeof(float));

    uint8_t is_static = 1;
    entity_attrs_set(&orig.attrs, SCRIPT_KEY_STATIC,
                     SCRIPT_ATTR_BOOL, &is_static, sizeof(uint8_t));

    int32_t neg_val = -42;
    entity_attrs_set(&orig.attrs, SCRIPT_KEY_FRICTION,
                     SCRIPT_ATTR_I32, &neg_val, sizeof(int32_t));

    float vel[3] = {1.0f, 2.0f, 3.0f};
    entity_attrs_set(&orig.attrs, SCRIPT_KEY_LIN_VEL,
                     SCRIPT_ATTR_VEC3, vel, 12);

    /* Serialize. */
    json_arena_t arena;
    json_arena_init(&arena, s_arena_buf, sizeof(s_arena_buf));

    json_value_t json_obj;
    bool ok = edit_entity_json_build(&orig, 5, &json_obj, &arena);
    ASSERT(ok);

    /* Parse back. */
    edit_entity_t parsed;
    edit_entity_json_parse(&json_obj, &parsed);

    /* Verify dynamic attrs. */
    ASSERT(entity_attrs_count(&parsed.attrs) == 5);

    uint8_t atype, asize;
    const void *val;

    val = entity_attrs_get(&parsed.attrs, SCRIPT_KEY_MESH_PATH, &atype, &asize);
    ASSERT(val != NULL);
    ASSERT(atype == SCRIPT_ATTR_STR);
    ASSERT(strcmp((const char *)val, "goblin.fvma") == 0);

    val = entity_attrs_get(&parsed.attrs, SCRIPT_KEY_MASS, &atype, &asize);
    ASSERT(val != NULL);
    ASSERT(atype == SCRIPT_ATTR_F32);
    ASSERT_FLOAT_EQ(*(const float *)val, 50.0f);

    val = entity_attrs_get(&parsed.attrs, SCRIPT_KEY_STATIC, &atype, &asize);
    ASSERT(val != NULL);
    ASSERT(atype == SCRIPT_ATTR_BOOL);
    ASSERT(*(const uint8_t *)val == 1);

    val = entity_attrs_get(&parsed.attrs, SCRIPT_KEY_FRICTION, &atype, &asize);
    ASSERT(val != NULL);
    ASSERT(atype == SCRIPT_ATTR_I32);
    ASSERT(*(const int32_t *)val == -42);

    val = entity_attrs_get(&parsed.attrs, SCRIPT_KEY_LIN_VEL, &atype, &asize);
    ASSERT(val != NULL);
    ASSERT(atype == SCRIPT_ATTR_VEC3);
    const float *fv = (const float *)val;
    ASSERT_FLOAT_EQ(fv[0], 1.0f);
    ASSERT_FLOAT_EQ(fv[1], 2.0f);
    ASSERT_FLOAT_EQ(fv[2], 3.0f);
}

/**
 * @brief Test parse with missing optional fields uses defaults.
 */
static void test_parse_missing_fields_uses_defaults(void) {
    /* Build a minimal JSON: just {"id": 10} */
    json_arena_t arena;
    json_arena_init(&arena, s_arena_buf, sizeof(s_arena_buf));

    /* Manually build minimal JSON object. */
    const char *keys[] = {"id"};
    uint32_t klens[] = {2};
    json_value_t vals[1];
    vals[0].type = JSON_NUMBER;
    vals[0].number = 10.0;

    json_value_t obj;
    obj.type = JSON_OBJECT;
    obj.object.keys = keys;
    obj.object.key_lens = klens;
    obj.object.vals = vals;
    obj.object.count = 1;

    edit_entity_t parsed;
    edit_entity_json_parse(&obj, &parsed);

    /* Should get defaults. */
    ASSERT(parsed.active == true);
    ASSERT(parsed.scale[0] == 1.0f);
    ASSERT(parsed.scale[1] == 1.0f);
    ASSERT(parsed.scale[2] == 1.0f);
    ASSERT(parsed.body_index == UINT32_MAX);
    ASSERT(parsed.hidden == false);
    ASSERT(parsed.orientation.w == 1.0f);
    ASSERT(parsed.name[0] == '\0');
    ASSERT(entity_attrs_count(&parsed.attrs) == 0);
}

/**
 * @brief Test arena size estimation is sufficient.
 */
static void test_arena_size_estimation(void) {
    size_t est = edit_entity_json_arena_bytes(10, 50);
    /* Should be a reasonable positive number. */
    ASSERT(est > 0);
    ASSERT(est < 1024 * 1024); /* Less than 1 MB for 10 entities. */

    /* Check that 0 entities gives small size. */
    size_t est0 = edit_entity_json_arena_bytes(0, 0);
    ASSERT(est0 > 0);  /* Wrapper overhead. */
    ASSERT(est0 < 4096);
}

/**
 * @brief Test materials with 64 slots.
 */
static void test_materials_64_slots(void) {
    ASSERT(EDIT_MATERIAL_SLOT_COUNT == 64);

    edit_entity_t ent;
    memset(&ent, 0, sizeof(ent));
    ent.active = true;
    ent.type = EDIT_ENTITY_TYPE_BOX;
    ent.scale[0] = ent.scale[1] = ent.scale[2] = 1.0f;
    ent.orientation = (quat_t){0, 0, 0, 1};
    ent.body_index = UINT32_MAX;
    entity_attrs_init(&ent.attrs);

    /* Set a few material slots including high indices. */
    strncpy(ent.materials[0], "slot0.png", EDIT_MATERIAL_PATH_MAX - 1);
    strncpy(ent.materials[31], "slot31.png", EDIT_MATERIAL_PATH_MAX - 1);
    strncpy(ent.materials[63], "slot63.png", EDIT_MATERIAL_PATH_MAX - 1);

    json_arena_t arena;
    json_arena_init(&arena, s_arena_buf, sizeof(s_arena_buf));

    json_value_t out;
    bool ok = edit_entity_json_build(&ent, 0, &out, &arena);
    ASSERT(ok);

    const json_value_t *mat_v = json_object_get(&out, "materials");
    ASSERT(mat_v && mat_v->type == JSON_ARRAY);
    ASSERT(mat_v->array.count == 64);

    /* Round-trip. */
    edit_entity_t parsed;
    edit_entity_json_parse(&out, &parsed);

    ASSERT(strcmp(parsed.materials[0], "slot0.png") == 0);
    ASSERT(strcmp(parsed.materials[31], "slot31.png") == 0);
    ASSERT(strcmp(parsed.materials[63], "slot63.png") == 0);
    ASSERT(parsed.materials[1][0] == '\0');
    ASSERT(parsed.materials[62][0] == '\0');
}

/**
 * @brief Test blob attribute serialization (array of bytes).
 */
static void test_blob_attr_round_trip(void) {
    edit_entity_t orig;
    memset(&orig, 0, sizeof(orig));
    orig.active = true;
    orig.type = EDIT_ENTITY_TYPE_BOX;
    orig.scale[0] = orig.scale[1] = orig.scale[2] = 1.0f;
    orig.orientation = (quat_t){0, 0, 0, 1};
    orig.body_index = UINT32_MAX;
    entity_attrs_init(&orig.attrs);

    uint8_t blob_data[] = {0x00, 0x42, 0xFF, 0x80, 0x01};
    entity_attrs_set(&orig.attrs, SCRIPT_KEY_USER,
                     SCRIPT_ATTR_BLOB, blob_data, sizeof(blob_data));

    json_arena_t arena;
    json_arena_init(&arena, s_arena_buf, sizeof(s_arena_buf));

    json_value_t out;
    bool ok = edit_entity_json_build(&orig, 0, &out, &arena);
    ASSERT(ok);

    edit_entity_t parsed;
    edit_entity_json_parse(&out, &parsed);

    uint8_t atype, asize;
    const void *val = entity_attrs_get(&parsed.attrs, SCRIPT_KEY_USER,
                                        &atype, &asize);
    ASSERT(val != NULL);
    ASSERT(atype == SCRIPT_ATTR_BLOB);
    ASSERT(asize == sizeof(blob_data));
    ASSERT(memcmp(val, blob_data, sizeof(blob_data)) == 0);
}

/* ---- Main ---- */

int main(void) {
    test_serialize_all_static_fields();
    test_serialize_dynamic_attrs();
    test_round_trip_static_fields();
    test_round_trip_dynamic_attrs();
    test_parse_missing_fields_uses_defaults();
    test_arena_size_estimation();
    test_materials_64_slots();
    test_blob_attr_round_trip();

    printf("entity_json_tests: %d passed, %d failed\n", s_pass, s_fail);
    return s_fail > 0 ? 1 : 0;
}
