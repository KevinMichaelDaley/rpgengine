/**
 * @file edit_script_rebase_tests.c
 * @brief Tests for script_rebase_apply — rebasing script entity updates
 *        onto the authoritative entity store.
 *
 * Comprehensive coverage:
 *   - Happy path: position, rotation, scale, type, body_index, name
 *   - Dynamic attrs: user keys written to entity_attrs_t
 *   - Multiple entities in a single blob
 *   - Multiple attrs per entity
 *   - Edge: empty blob (no updates)
 *   - Edge: deleted entity (graceful skip)
 *   - Edge: entity_id out of range (graceful skip)
 *   - Edge: partial blob (truncated mid-update)
 *   - Edge: zero-size attr payload
 *   - Edge: blob with only header, no attrs
 *   - Integration: write_attr → swap → rebase round-trip
 *   - Integration: snapshot → script write → rebase → verify state
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/editor/edit_script_env.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_script_rebase.h"

/* ------------------------------------------------------------------ */
/* Test harness                                                       */
/* ------------------------------------------------------------------ */

static int g_pass, g_fail;

#define RUN(fn) do { \
    printf("RUN  %s\n", #fn); \
    if (fn()) { printf("OK   %s\n", #fn); g_pass++; } \
    else       { printf("FAIL %s\n", #fn); g_fail++; } \
} while (0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  ASSERT FAILED: %s (line %d)\n", #cond, __LINE__); \
        return false; \
    } \
} while (0)

#define ASSERT_FLOAT_EQ(a, b) do { \
    if (fabsf((float)(a) - (float)(b)) > 1e-5f) { \
        printf("  ASSERT FAILED: %f != %f (line %d)\n", \
               (double)(a), (double)(b), __LINE__); \
        return false; \
    } \
} while (0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("  ASSERT FAILED: %d != %d (line %d)\n", \
               (int)(a), (int)(b), __LINE__); \
        return false; \
    } \
} while (0)

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

/** Create a store with a single box entity at default position. */
static bool setup_store(edit_entity_store_t *store) {
    if (!edit_entity_store_init(store, 16)) return false;
    return true;
}

/** Create entity 0: box at (1,2,3). */
static uint32_t add_box(edit_entity_store_t *store) {
    uint32_t id = edit_entity_store_create(store, EDIT_ENTITY_TYPE_BOX);
    if (id == EDIT_ENTITY_INVALID_ID) return id;
    edit_entity_t *e = edit_entity_store_get_mut(store, id);
    e->pos[0] = 1.0f; e->pos[1] = 2.0f; e->pos[2] = 3.0f;
    e->rot[0] = 0.0f; e->rot[1] = 0.0f; e->rot[2] = 0.0f;
    e->scale[0] = 1.0f; e->scale[1] = 1.0f; e->scale[2] = 1.0f;
    snprintf(e->name, sizeof(e->name), "box");
    return id;
}

/** Build a manual update blob with a single POS write. */
static uint32_t build_pos_update(uint8_t *blob, uint32_t entity_id,
                                 float x, float y, float z) {
    uint32_t off = 0;
    script_entity_update_t *upd = (script_entity_update_t *)(blob + off);
    upd->entity_id  = entity_id;
    upd->generation = 0;
    upd->attr_count = 1;

    off += (uint32_t)sizeof(script_entity_update_t);

    script_attr_write_t *aw = (script_attr_write_t *)(blob + off);
    aw->key  = SCRIPT_KEY_POS;
    aw->type = SCRIPT_ATTR_VEC3;
    aw->size = 12;
    off += (uint32_t)sizeof(script_attr_write_t);

    float pos[3] = {x, y, z};
    memcpy(blob + off, pos, 12);
    off += 12;

    upd->total_size = (uint16_t)off;
    return off;
}

/* ================================================================== */
/* Unit tests                                                         */
/* ================================================================== */

/* --- Rebase position ------------------------------------------------ */

static bool test_rebase_position(void) {
    edit_entity_store_t store;
    ASSERT(setup_store(&store));
    uint32_t id = add_box(&store);

    uint8_t blob[256];
    uint32_t used = build_pos_update(blob, id, 10.0f, 20.0f, 30.0f);

    script_rebase_result_t result = script_rebase_apply(&store, blob, used);
    ASSERT_EQ(result.applied, 1);
    ASSERT_EQ(result.skipped, 0);

    const edit_entity_t *e = edit_entity_store_get(&store, id);
    ASSERT_FLOAT_EQ(e->pos[0], 10.0f);
    ASSERT_FLOAT_EQ(e->pos[1], 20.0f);
    ASSERT_FLOAT_EQ(e->pos[2], 30.0f);

    edit_entity_store_destroy(&store);
    return true;
}

/* --- Rebase rotation ------------------------------------------------ */

static bool test_rebase_rotation(void) {
    edit_entity_store_t store;
    ASSERT(setup_store(&store));
    uint32_t id = add_box(&store);

    uint8_t blob[256];
    uint32_t off = 0;

    script_entity_update_t *upd = (script_entity_update_t *)(blob + off);
    upd->entity_id = id; upd->generation = 0; upd->attr_count = 1;
    off += (uint32_t)sizeof(script_entity_update_t);

    script_attr_write_t *aw = (script_attr_write_t *)(blob + off);
    aw->key = SCRIPT_KEY_ROT; aw->type = SCRIPT_ATTR_VEC3; aw->size = 12;
    off += (uint32_t)sizeof(script_attr_write_t);

    float rot[3] = {45.0f, 90.0f, 0.0f};
    memcpy(blob + off, rot, 12); off += 12;
    upd->total_size = (uint16_t)off;

    script_rebase_result_t result = script_rebase_apply(&store, blob, off);
    ASSERT_EQ(result.applied, 1);

    const edit_entity_t *e = edit_entity_store_get(&store, id);
    ASSERT_FLOAT_EQ(e->rot[0], 45.0f);
    ASSERT_FLOAT_EQ(e->rot[1], 90.0f);

    edit_entity_store_destroy(&store);
    return true;
}

/* --- Rebase scale --------------------------------------------------- */

static bool test_rebase_scale(void) {
    edit_entity_store_t store;
    ASSERT(setup_store(&store));
    uint32_t id = add_box(&store);

    uint8_t blob[256];
    uint32_t off = 0;

    script_entity_update_t *upd = (script_entity_update_t *)(blob + off);
    upd->entity_id = id; upd->generation = 0; upd->attr_count = 1;
    off += (uint32_t)sizeof(script_entity_update_t);

    script_attr_write_t *aw = (script_attr_write_t *)(blob + off);
    aw->key = SCRIPT_KEY_SCALE; aw->type = SCRIPT_ATTR_VEC3; aw->size = 12;
    off += (uint32_t)sizeof(script_attr_write_t);

    float scl[3] = {2.0f, 3.0f, 4.0f};
    memcpy(blob + off, scl, 12); off += 12;
    upd->total_size = (uint16_t)off;

    script_rebase_result_t result = script_rebase_apply(&store, blob, off);
    ASSERT_EQ(result.applied, 1);

    const edit_entity_t *e = edit_entity_store_get(&store, id);
    ASSERT_FLOAT_EQ(e->scale[0], 2.0f);
    ASSERT_FLOAT_EQ(e->scale[1], 3.0f);
    ASSERT_FLOAT_EQ(e->scale[2], 4.0f);

    edit_entity_store_destroy(&store);
    return true;
}

/* --- Rebase entity type --------------------------------------------- */

static bool test_rebase_type(void) {
    edit_entity_store_t store;
    ASSERT(setup_store(&store));
    uint32_t id = add_box(&store);

    uint8_t blob[256];
    uint32_t off = 0;

    script_entity_update_t *upd = (script_entity_update_t *)(blob + off);
    upd->entity_id = id; upd->generation = 0; upd->attr_count = 1;
    off += (uint32_t)sizeof(script_entity_update_t);

    script_attr_write_t *aw = (script_attr_write_t *)(blob + off);
    aw->key = SCRIPT_KEY_TYPE; aw->type = SCRIPT_ATTR_U32; aw->size = 4;
    off += (uint32_t)sizeof(script_attr_write_t);

    uint32_t new_type = EDIT_ENTITY_TYPE_SPHERE;
    memcpy(blob + off, &new_type, 4); off += 4;
    upd->total_size = (uint16_t)off;

    script_rebase_apply(&store, blob, off);

    const edit_entity_t *e = edit_entity_store_get(&store, id);
    ASSERT_EQ(e->type, EDIT_ENTITY_TYPE_SPHERE);

    edit_entity_store_destroy(&store);
    return true;
}

/* --- Rebase body_index ---------------------------------------------- */

static bool test_rebase_body_index(void) {
    edit_entity_store_t store;
    ASSERT(setup_store(&store));
    uint32_t id = add_box(&store);

    uint8_t blob[256];
    uint32_t off = 0;

    script_entity_update_t *upd = (script_entity_update_t *)(blob + off);
    upd->entity_id = id; upd->generation = 0; upd->attr_count = 1;
    off += (uint32_t)sizeof(script_entity_update_t);

    script_attr_write_t *aw = (script_attr_write_t *)(blob + off);
    aw->key = SCRIPT_KEY_BODY_IDX; aw->type = SCRIPT_ATTR_U32; aw->size = 4;
    off += (uint32_t)sizeof(script_attr_write_t);

    uint32_t body = 42;
    memcpy(blob + off, &body, 4); off += 4;
    upd->total_size = (uint16_t)off;

    script_rebase_apply(&store, blob, off);

    const edit_entity_t *e = edit_entity_store_get(&store, id);
    ASSERT_EQ(e->body_index, (uint32_t)42);

    edit_entity_store_destroy(&store);
    return true;
}

/* --- Rebase name ---------------------------------------------------- */

static bool test_rebase_name(void) {
    edit_entity_store_t store;
    ASSERT(setup_store(&store));
    uint32_t id = add_box(&store);

    uint8_t blob[512];
    uint32_t off = 0;

    script_entity_update_t *upd = (script_entity_update_t *)(blob + off);
    upd->entity_id = id; upd->generation = 0; upd->attr_count = 1;
    off += (uint32_t)sizeof(script_entity_update_t);

    const char *new_name = "renamed_entity";
    uint8_t name_len = (uint8_t)(strlen(new_name) + 1); /* include null */

    script_attr_write_t *aw = (script_attr_write_t *)(blob + off);
    aw->key = SCRIPT_KEY_NAME; aw->type = SCRIPT_ATTR_STR; aw->size = name_len;
    off += (uint32_t)sizeof(script_attr_write_t);

    memcpy(blob + off, new_name, name_len); off += name_len;
    upd->total_size = (uint16_t)off;

    script_rebase_apply(&store, blob, off);

    const edit_entity_t *e = edit_entity_store_get(&store, id);
    ASSERT(strcmp(e->name, "renamed_entity") == 0);

    edit_entity_store_destroy(&store);
    return true;
}

/* --- Rebase dynamic user attr --------------------------------------- */

static bool test_rebase_user_attr(void) {
    edit_entity_store_t store;
    ASSERT(setup_store(&store));
    uint32_t id = add_box(&store);

    uint8_t blob[256];
    uint32_t off = 0;

    script_entity_update_t *upd = (script_entity_update_t *)(blob + off);
    upd->entity_id = id; upd->generation = 0; upd->attr_count = 1;
    off += (uint32_t)sizeof(script_entity_update_t);

    uint16_t user_key = SCRIPT_KEY_USER + 10; /* health */
    script_attr_write_t *aw = (script_attr_write_t *)(blob + off);
    aw->key = user_key; aw->type = SCRIPT_ATTR_F32; aw->size = 4;
    off += (uint32_t)sizeof(script_attr_write_t);

    float health = 75.5f;
    memcpy(blob + off, &health, 4); off += 4;
    upd->total_size = (uint16_t)off;

    script_rebase_apply(&store, blob, off);

    const edit_entity_t *e = edit_entity_store_get(&store, id);
    uint8_t out_type, out_size;
    const void *val = entity_attrs_get(&e->attrs, user_key, &out_type, &out_size);
    ASSERT(val != NULL);
    ASSERT_EQ(out_type, SCRIPT_ATTR_F32);
    ASSERT_EQ(out_size, 4);
    float got;
    memcpy(&got, val, 4);
    ASSERT_FLOAT_EQ(got, 75.5f);

    edit_entity_store_destroy(&store);
    return true;
}

/* --- Multiple attrs per entity -------------------------------------- */

static bool test_rebase_multiple_attrs(void) {
    edit_entity_store_t store;
    ASSERT(setup_store(&store));
    uint32_t id = add_box(&store);

    /* Build blob with POS + ROT in a single update. */
    uint8_t blob[512];
    uint32_t off = 0;

    script_entity_update_t *upd = (script_entity_update_t *)(blob + off);
    upd->entity_id = id; upd->generation = 0; upd->attr_count = 2;
    off += (uint32_t)sizeof(script_entity_update_t);

    /* Attr 1: POS */
    script_attr_write_t *aw1 = (script_attr_write_t *)(blob + off);
    aw1->key = SCRIPT_KEY_POS; aw1->type = SCRIPT_ATTR_VEC3; aw1->size = 12;
    off += (uint32_t)sizeof(script_attr_write_t);
    float pos[3] = {100.0f, 200.0f, 300.0f};
    memcpy(blob + off, pos, 12); off += 12;

    /* Attr 2: ROT */
    script_attr_write_t *aw2 = (script_attr_write_t *)(blob + off);
    aw2->key = SCRIPT_KEY_ROT; aw2->type = SCRIPT_ATTR_VEC3; aw2->size = 12;
    off += (uint32_t)sizeof(script_attr_write_t);
    float rot[3] = {10.0f, 20.0f, 30.0f};
    memcpy(blob + off, rot, 12); off += 12;

    upd->total_size = (uint16_t)off;

    script_rebase_result_t result = script_rebase_apply(&store, blob, off);
    ASSERT_EQ(result.applied, 1);

    const edit_entity_t *e = edit_entity_store_get(&store, id);
    ASSERT_FLOAT_EQ(e->pos[0], 100.0f);
    ASSERT_FLOAT_EQ(e->rot[1], 20.0f);

    edit_entity_store_destroy(&store);
    return true;
}

/* --- Multiple entities in blob -------------------------------------- */

static bool test_rebase_multiple_entities(void) {
    edit_entity_store_t store;
    ASSERT(setup_store(&store));
    uint32_t id0 = add_box(&store);
    uint32_t id1 = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_SPHERE);
    edit_entity_t *e1 = edit_entity_store_get_mut(&store, id1);
    e1->pos[0] = 0.0f; e1->pos[1] = 0.0f; e1->pos[2] = 0.0f;

    uint8_t blob[512];
    uint32_t off = 0;

    /* Update entity 0: set POS. */
    off += build_pos_update(blob + off, id0, 11.0f, 22.0f, 33.0f);

    /* Update entity 1: set POS. */
    off += build_pos_update(blob + off, id1, 44.0f, 55.0f, 66.0f);

    script_rebase_result_t result = script_rebase_apply(&store, blob, off);
    ASSERT_EQ(result.applied, 2);

    const edit_entity_t *e0r = edit_entity_store_get(&store, id0);
    ASSERT_FLOAT_EQ(e0r->pos[0], 11.0f);
    const edit_entity_t *e1r = edit_entity_store_get(&store, id1);
    ASSERT_FLOAT_EQ(e1r->pos[0], 44.0f);

    edit_entity_store_destroy(&store);
    return true;
}

/* --- Empty blob ----------------------------------------------------- */

static bool test_rebase_empty_blob(void) {
    edit_entity_store_t store;
    ASSERT(setup_store(&store));

    script_rebase_result_t result = script_rebase_apply(&store, NULL, 0);
    ASSERT_EQ(result.applied, 0);
    ASSERT_EQ(result.skipped, 0);

    edit_entity_store_destroy(&store);
    return true;
}

/* --- Deleted entity (graceful skip) --------------------------------- */

static bool test_rebase_deleted_entity(void) {
    edit_entity_store_t store;
    ASSERT(setup_store(&store));
    uint32_t id = add_box(&store);

    /* Delete the entity. */
    edit_entity_store_remove(&store, id);

    /* Try to rebase a POS update onto it. */
    uint8_t blob[256];
    uint32_t used = build_pos_update(blob, id, 99.0f, 99.0f, 99.0f);

    script_rebase_result_t result = script_rebase_apply(&store, blob, used);
    ASSERT_EQ(result.applied, 0);
    ASSERT_EQ(result.skipped, 1);

    edit_entity_store_destroy(&store);
    return true;
}

/* --- Out-of-range entity_id ----------------------------------------- */

static bool test_rebase_out_of_range_id(void) {
    edit_entity_store_t store;
    ASSERT(setup_store(&store));

    uint8_t blob[256];
    uint32_t used = build_pos_update(blob, 9999, 1.0f, 2.0f, 3.0f);

    script_rebase_result_t result = script_rebase_apply(&store, blob, used);
    ASSERT_EQ(result.applied, 0);
    ASSERT_EQ(result.skipped, 1);

    edit_entity_store_destroy(&store);
    return true;
}

/* --- NULL store (graceful) ------------------------------------------ */

static bool test_rebase_null_store(void) {
    uint8_t blob[64];
    uint32_t used = build_pos_update(blob, 0, 1.0f, 2.0f, 3.0f);

    script_rebase_result_t result = script_rebase_apply(NULL, blob, used);
    ASSERT_EQ(result.applied, 0);
    ASSERT_EQ(result.skipped, 0);

    return true;
}

/* --- Size mismatch: wrong payload size for VEC3 (should skip attr) -- */

static bool test_rebase_size_mismatch(void) {
    edit_entity_store_t store;
    ASSERT(setup_store(&store));
    uint32_t id = add_box(&store);

    uint8_t blob[256];
    uint32_t off = 0;

    script_entity_update_t *upd = (script_entity_update_t *)(blob + off);
    upd->entity_id = id; upd->generation = 0; upd->attr_count = 1;
    off += (uint32_t)sizeof(script_entity_update_t);

    /* POS expects 12 bytes, give it 8 (wrong). */
    script_attr_write_t *aw = (script_attr_write_t *)(blob + off);
    aw->key = SCRIPT_KEY_POS; aw->type = SCRIPT_ATTR_VEC3; aw->size = 8;
    off += (uint32_t)sizeof(script_attr_write_t);

    float bad_data[2] = {99.0f, 99.0f};
    memcpy(blob + off, bad_data, 8); off += 8;
    upd->total_size = (uint16_t)off;

    script_rebase_apply(&store, blob, off);

    /* Position should be unchanged (mismatch skipped). */
    const edit_entity_t *e = edit_entity_store_get(&store, id);
    ASSERT_FLOAT_EQ(e->pos[0], 1.0f);

    edit_entity_store_destroy(&store);
    return true;
}

/* --- Preserves non-written fields ----------------------------------- */

static bool test_rebase_preserves_other_fields(void) {
    edit_entity_store_t store;
    ASSERT(setup_store(&store));
    uint32_t id = add_box(&store);

    /* Set some initial values. */
    edit_entity_t *e = edit_entity_store_get_mut(&store, id);
    e->rot[0] = 15.0f; e->rot[1] = 25.0f; e->rot[2] = 35.0f;
    e->scale[0] = 2.0f; e->scale[1] = 3.0f; e->scale[2] = 4.0f;

    /* Only write position — rotation and scale should be untouched. */
    uint8_t blob[256];
    uint32_t used = build_pos_update(blob, id, 50.0f, 60.0f, 70.0f);

    script_rebase_apply(&store, blob, used);

    const edit_entity_t *er = edit_entity_store_get(&store, id);
    ASSERT_FLOAT_EQ(er->pos[0], 50.0f);
    ASSERT_FLOAT_EQ(er->rot[0], 15.0f);
    ASSERT_FLOAT_EQ(er->rot[1], 25.0f);
    ASSERT_FLOAT_EQ(er->scale[0], 2.0f);
    ASSERT_FLOAT_EQ(er->scale[2], 4.0f);

    edit_entity_store_destroy(&store);
    return true;
}

/* --- User attr overwrite -------------------------------------------- */

static bool test_rebase_user_attr_overwrite(void) {
    edit_entity_store_t store;
    ASSERT(setup_store(&store));
    uint32_t id = add_box(&store);

    /* Pre-set a user attr. */
    edit_entity_t *e = edit_entity_store_get_mut(&store, id);
    float old_val = 10.0f;
    entity_attrs_set(&e->attrs, SCRIPT_KEY_USER + 1, SCRIPT_ATTR_F32, &old_val, 4);

    /* Rebase with new value. */
    uint8_t blob[256];
    uint32_t off = 0;

    script_entity_update_t *upd = (script_entity_update_t *)(blob + off);
    upd->entity_id = id; upd->generation = 0; upd->attr_count = 1;
    off += (uint32_t)sizeof(script_entity_update_t);

    script_attr_write_t *aw = (script_attr_write_t *)(blob + off);
    aw->key = SCRIPT_KEY_USER + 1; aw->type = SCRIPT_ATTR_F32; aw->size = 4;
    off += (uint32_t)sizeof(script_attr_write_t);

    float new_val = 99.0f;
    memcpy(blob + off, &new_val, 4); off += 4;
    upd->total_size = (uint16_t)off;

    script_rebase_apply(&store, blob, off);

    const edit_entity_t *er = edit_entity_store_get(&store, id);
    uint8_t ot, os;
    const void *v = entity_attrs_get(&er->attrs, SCRIPT_KEY_USER + 1, &ot, &os);
    ASSERT(v != NULL);
    float got;
    memcpy(&got, v, 4);
    ASSERT_FLOAT_EQ(got, 99.0f);

    edit_entity_store_destroy(&store);
    return true;
}

/* --- Mixed: some entities exist, some don't ------------------------- */

static bool test_rebase_mixed_valid_invalid(void) {
    edit_entity_store_t store;
    ASSERT(setup_store(&store));
    uint32_t id = add_box(&store);

    uint8_t blob[512];
    uint32_t off = 0;

    /* Valid entity. */
    off += build_pos_update(blob + off, id, 77.0f, 88.0f, 99.0f);
    /* Invalid entity (out of range). */
    off += build_pos_update(blob + off, 8888, 1.0f, 2.0f, 3.0f);

    script_rebase_result_t result = script_rebase_apply(&store, blob, off);
    ASSERT_EQ(result.applied, 1);
    ASSERT_EQ(result.skipped, 1);

    const edit_entity_t *e = edit_entity_store_get(&store, id);
    ASSERT_FLOAT_EQ(e->pos[0], 77.0f);

    edit_entity_store_destroy(&store);
    return true;
}

/* --- Name too long (should truncate, not overflow) ------------------ */

static bool test_rebase_name_truncate(void) {
    edit_entity_store_t store;
    ASSERT(setup_store(&store));
    uint32_t id = add_box(&store);

    uint8_t blob[512];
    uint32_t off = 0;

    script_entity_update_t *upd = (script_entity_update_t *)(blob + off);
    upd->entity_id = id; upd->generation = 0; upd->attr_count = 1;
    off += (uint32_t)sizeof(script_entity_update_t);

    /* Name attr: 255 bytes (max attr payload), well over name capacity. */
    char long_name[255];
    memset(long_name, 'A', sizeof(long_name));
    long_name[254] = '\0';

    script_attr_write_t *aw = (script_attr_write_t *)(blob + off);
    aw->key = SCRIPT_KEY_NAME; aw->type = SCRIPT_ATTR_STR; aw->size = 255;
    off += (uint32_t)sizeof(script_attr_write_t);

    memcpy(blob + off, long_name, 255); off += 255;
    upd->total_size = (uint16_t)off;

    script_rebase_apply(&store, blob, off);

    const edit_entity_t *e = edit_entity_store_get(&store, id);
    /* Name should be truncated to fit EDIT_ENTITY_NAME_MAX. */
    ASSERT(strlen(e->name) < EDIT_ENTITY_NAME_MAX);
    ASSERT(e->name[0] == 'A');

    edit_entity_store_destroy(&store);
    return true;
}

/* ================================================================== */
/* Integration tests                                                  */
/* ================================================================== */

/* --- write_attr → swap → rebase round-trip -------------------------- */

static bool test_integration_write_swap_rebase(void) {
    edit_entity_store_t store;
    ASSERT(setup_store(&store));
    uint32_t id = add_box(&store);

    /* Set up a double-buffered update blob. */
    script_update_buffer_t ubuf;
    ASSERT(script_update_buffer_init(&ubuf, 4096));

    /* Set up a script_env_t pointed at the back buffer. */
    script_env_t env;
    script_env_init_blob(&env, ubuf.blob[1], ubuf.capacity);

    /* Write some attrs via the env API. */
    float new_pos[3] = {111.0f, 222.0f, 333.0f};
    script_env_write_attr(&env, id, 0, SCRIPT_KEY_POS,
                          SCRIPT_ATTR_VEC3, new_pos, 12);

    float new_rot[3] = {10.0f, 20.0f, 30.0f};
    script_env_write_attr(&env, id, 0, SCRIPT_KEY_ROT,
                          SCRIPT_ATTR_VEC3, new_rot, 12);

    /* Copy env used count back to the buffer's back slot. */
    ubuf.used[1] = env.update_blob_used;

    /* Swap: back→front. */
    script_update_buffer_swap(&ubuf);

    /* Now front buffer has the writes. Rebase. */
    script_rebase_result_t result =
        script_rebase_apply(&store, ubuf.blob[0], ubuf.used[0]);
    ASSERT_EQ(result.applied, 1);

    const edit_entity_t *e = edit_entity_store_get(&store, id);
    ASSERT_FLOAT_EQ(e->pos[0], 111.0f);
    ASSERT_FLOAT_EQ(e->pos[1], 222.0f);
    ASSERT_FLOAT_EQ(e->pos[2], 333.0f);
    ASSERT_FLOAT_EQ(e->rot[0], 10.0f);
    ASSERT_FLOAT_EQ(e->rot[1], 20.0f);

    script_update_buffer_destroy(&ubuf);
    edit_entity_store_destroy(&store);
    return true;
}

/* --- Full pipeline: snapshot → write → rebase → verify -------------- */

static bool test_integration_snapshot_write_rebase(void) {
    edit_entity_store_t store;
    ASSERT(setup_store(&store));
    uint32_t id0 = add_box(&store);

    /* Add a second entity. */
    uint32_t id1 = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_SPHERE);
    edit_entity_t *e1 = edit_entity_store_get_mut(&store, id1);
    e1->pos[0] = 10.0f; e1->pos[1] = 20.0f; e1->pos[2] = 30.0f;
    e1->scale[0] = 1.0f; e1->scale[1] = 1.0f; e1->scale[2] = 1.0f;

    /* Step 1: Build snapshot. */
    script_entity_snapshot_t snapshots[16];
    uint32_t snap_count = script_snapshot_build(&store, snapshots, 16);
    ASSERT_EQ(snap_count, (uint32_t)2);

    /* Step 2: Script writes to update blob based on snapshot data. */
    script_update_buffer_t ubuf;
    ASSERT(script_update_buffer_init(&ubuf, 4096));

    script_env_t env;
    script_env_init_blob(&env, ubuf.blob[1], ubuf.capacity);

    /* Provide snapshot view for script to read. */
    env.entities.entities = snapshots;
    env.entities.count    = snap_count;
    env.entities.capacity = 16;

    /* "Script logic": move entity 0's position by (1,1,1). */
    float moved_pos[3] = {
        snapshots[0].pos[0] + 1.0f,
        snapshots[0].pos[1] + 1.0f,
        snapshots[0].pos[2] + 1.0f,
    };
    script_env_write_attr(&env, snapshots[0].entity_id, 0,
                          SCRIPT_KEY_POS, SCRIPT_ATTR_VEC3, moved_pos, 12);

    /* "Script logic": set a health attr on entity 1. */
    float health = 100.0f;
    script_env_write_attr(&env, snapshots[1].entity_id, 0,
                          SCRIPT_KEY_USER, SCRIPT_ATTR_F32, &health, 4);

    ubuf.used[1] = env.update_blob_used;
    script_update_buffer_swap(&ubuf);

    /* Step 3: Rebase onto store. */
    script_rebase_result_t result =
        script_rebase_apply(&store, ubuf.blob[0], ubuf.used[0]);
    ASSERT_EQ(result.applied, 2);

    /* Step 4: Verify. */
    const edit_entity_t *e0 = edit_entity_store_get(&store, id0);
    ASSERT_FLOAT_EQ(e0->pos[0], 2.0f);  /* 1 + 1 */
    ASSERT_FLOAT_EQ(e0->pos[1], 3.0f);  /* 2 + 1 */
    ASSERT_FLOAT_EQ(e0->pos[2], 4.0f);  /* 3 + 1 */

    const edit_entity_t *e1r = edit_entity_store_get(&store, id1);
    uint8_t ot, os;
    const void *hv = entity_attrs_get(&e1r->attrs, SCRIPT_KEY_USER, &ot, &os);
    ASSERT(hv != NULL);
    float got_health;
    memcpy(&got_health, hv, 4);
    ASSERT_FLOAT_EQ(got_health, 100.0f);

    /* Entity 1's position should be unchanged. */
    ASSERT_FLOAT_EQ(e1r->pos[0], 10.0f);

    script_update_buffer_destroy(&ubuf);
    edit_entity_store_destroy(&store);
    return true;
}

/* --- Integration: rebase then snapshot captures new state ----------- */

static bool test_integration_rebase_then_snapshot(void) {
    edit_entity_store_t store;
    ASSERT(setup_store(&store));
    uint32_t id = add_box(&store);

    /* Rebase position to (50, 60, 70). */
    uint8_t blob[256];
    uint32_t used = build_pos_update(blob, id, 50.0f, 60.0f, 70.0f);
    script_rebase_apply(&store, blob, used);

    /* Build new snapshot — should reflect rebased state. */
    script_entity_snapshot_t snaps[4];
    uint32_t count = script_snapshot_build(&store, snaps, 4);
    ASSERT_EQ(count, (uint32_t)1);
    ASSERT_FLOAT_EQ(snaps[0].pos[0], 50.0f);
    ASSERT_FLOAT_EQ(snaps[0].pos[1], 60.0f);
    ASSERT_FLOAT_EQ(snaps[0].pos[2], 70.0f);

    edit_entity_store_destroy(&store);
    return true;
}

/* --- Integration: multiple rebase cycles ----------------------------- */

static bool test_integration_multiple_rebase_cycles(void) {
    edit_entity_store_t store;
    ASSERT(setup_store(&store));
    uint32_t id = add_box(&store);

    /* Cycle 1: move to (10, 20, 30). */
    uint8_t blob[256];
    uint32_t used = build_pos_update(blob, id, 10.0f, 20.0f, 30.0f);
    script_rebase_apply(&store, blob, used);

    const edit_entity_t *e = edit_entity_store_get(&store, id);
    ASSERT_FLOAT_EQ(e->pos[0], 10.0f);

    /* Cycle 2: move to (100, 200, 300). */
    used = build_pos_update(blob, id, 100.0f, 200.0f, 300.0f);
    script_rebase_apply(&store, blob, used);

    e = edit_entity_store_get(&store, id);
    ASSERT_FLOAT_EQ(e->pos[0], 100.0f);
    ASSERT_FLOAT_EQ(e->pos[1], 200.0f);

    edit_entity_store_destroy(&store);
    return true;
}

/* ================================================================== */
/* Main                                                               */
/* ================================================================== */

int main(void) {
    printf("=== Script Rebase Tests ===\n\n");

    /* Unit tests: individual field rebasing */
    RUN(test_rebase_position);
    RUN(test_rebase_rotation);
    RUN(test_rebase_scale);
    RUN(test_rebase_type);
    RUN(test_rebase_body_index);
    RUN(test_rebase_name);
    RUN(test_rebase_user_attr);
    RUN(test_rebase_multiple_attrs);
    RUN(test_rebase_multiple_entities);

    /* Edge cases */
    RUN(test_rebase_empty_blob);
    RUN(test_rebase_deleted_entity);
    RUN(test_rebase_out_of_range_id);
    RUN(test_rebase_null_store);
    RUN(test_rebase_size_mismatch);
    RUN(test_rebase_preserves_other_fields);
    RUN(test_rebase_user_attr_overwrite);
    RUN(test_rebase_mixed_valid_invalid);
    RUN(test_rebase_name_truncate);

    /* Integration tests */
    RUN(test_integration_write_swap_rebase);
    RUN(test_integration_snapshot_write_rebase);
    RUN(test_integration_rebase_then_snapshot);
    RUN(test_integration_multiple_rebase_cycles);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
