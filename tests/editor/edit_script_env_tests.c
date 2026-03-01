/**
 * @file edit_script_env_tests.c
 * @brief Tests for script environment: snapshot, update buffer, env API.
 *
 * Covers: snapshot building from entity store, update blob write/read,
 * double-buffer swap, env init/reset, attr get from snapshot.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "ferrum/editor/edit_script_env.h"
#include "ferrum/editor/edit_entity.h"

/* ----------------------------------------------------------------------- */
/* Test macros                                                               */
/* ----------------------------------------------------------------------- */

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
    if (fabsf((a) - (b)) > 1e-6f) { \
        printf("  ASSERT FAILED: %f != %f (line %d)\n", \
               (double)(a), (double)(b), __LINE__); \
        return false; \
    } \
} while (0)

/* ----------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ----------------------------------------------------------------------- */

/** Create a small entity store with some entities for testing. */
static bool setup_store(edit_entity_store_t *store) {
    if (!edit_entity_store_init(store, 16)) return false;

    /* Entity 0: box at (1,2,3) */
    uint32_t id0 = edit_entity_store_create(store, EDIT_ENTITY_TYPE_BOX);
    if (id0 == EDIT_ENTITY_INVALID_ID) return false;
    edit_entity_t *e0 = edit_entity_store_get_mut(store, id0);
    e0->pos[0] = 1.0f; e0->pos[1] = 2.0f; e0->pos[2] = 3.0f;
    snprintf(e0->name, sizeof(e0->name), "box1");

    /* Entity 1: sphere at (4,5,6) with rotation */
    uint32_t id1 = edit_entity_store_create(store, EDIT_ENTITY_TYPE_SPHERE);
    if (id1 == EDIT_ENTITY_INVALID_ID) return false;
    edit_entity_t *e1 = edit_entity_store_get_mut(store, id1);
    e1->pos[0] = 4.0f; e1->pos[1] = 5.0f; e1->pos[2] = 6.0f;
    e1->rot[1] = 45.0f;
    snprintf(e1->name, sizeof(e1->name), "sphere1");

    return true;
}

/* ----------------------------------------------------------------------- */
/* Snapshot builder tests                                                    */
/* ----------------------------------------------------------------------- */

/** Build snapshot from empty store produces count=0. */
static bool test_snapshot_empty_store(void) {
    edit_entity_store_t store;
    ASSERT(edit_entity_store_init(&store, 4));

    script_entity_snapshot_t buf[4];
    uint32_t count = script_snapshot_build(&store, buf, 4);
    ASSERT(count == 0);

    edit_entity_store_destroy(&store);
    return true;
}

/** Build snapshot captures entity fields correctly. */
static bool test_snapshot_captures_fields(void) {
    edit_entity_store_t store;
    ASSERT(setup_store(&store));

    script_entity_snapshot_t buf[16];
    uint32_t count = script_snapshot_build(&store, buf, 16);
    ASSERT(count == 2);

    /* Find box1. */
    const script_entity_snapshot_t *box = NULL;
    const script_entity_snapshot_t *sphere = NULL;
    for (uint32_t i = 0; i < count; i++) {
        if (strcmp(buf[i].name, "box1") == 0) box = &buf[i];
        if (strcmp(buf[i].name, "sphere1") == 0) sphere = &buf[i];
    }
    ASSERT(box != NULL);
    ASSERT(sphere != NULL);

    /* Verify box fields. */
    ASSERT(box->type == EDIT_ENTITY_TYPE_BOX);
    ASSERT(box->active == 1);
    ASSERT_FLOAT_EQ(box->pos[0], 1.0f);
    ASSERT_FLOAT_EQ(box->pos[1], 2.0f);
    ASSERT_FLOAT_EQ(box->pos[2], 3.0f);
    ASSERT_FLOAT_EQ(box->scale[0], 1.0f);

    /* Verify sphere fields. */
    ASSERT(sphere->type == EDIT_ENTITY_TYPE_SPHERE);
    ASSERT_FLOAT_EQ(sphere->rot[1], 45.0f);

    edit_entity_store_destroy(&store);
    return true;
}

/** Snapshot respects output capacity (truncation). */
static bool test_snapshot_truncation(void) {
    edit_entity_store_t store;
    ASSERT(setup_store(&store));

    script_entity_snapshot_t buf[1];
    uint32_t count = script_snapshot_build(&store, buf, 1);
    ASSERT(count == 1);
    ASSERT(buf[0].active == 1);

    edit_entity_store_destroy(&store);
    return true;
}

/** Snapshot skips inactive entities. */
static bool test_snapshot_skips_inactive(void) {
    edit_entity_store_t store;
    ASSERT(setup_store(&store));

    /* Remove entity 0. */
    edit_entity_store_remove(&store, 0);

    script_entity_snapshot_t buf[16];
    uint32_t count = script_snapshot_build(&store, buf, 16);
    ASSERT(count == 1);
    ASSERT(strcmp(buf[0].name, "sphere1") == 0);

    edit_entity_store_destroy(&store);
    return true;
}

/** Snapshot copies entity_attrs_t from source entity. */
static bool test_snapshot_copies_attrs(void) {
    edit_entity_store_t store;
    ASSERT(setup_store(&store));

    /* Set a dynamic attribute on entity 0. */
    edit_entity_t *e0 = edit_entity_store_get_mut(&store, 0);
    float health = 100.0f;
    entity_attrs_init(&e0->attrs);
    ASSERT(entity_attrs_set(&e0->attrs, SCRIPT_KEY_USER, SCRIPT_ATTR_F32,
                            &health, sizeof(health)));

    script_entity_snapshot_t buf[16];
    uint32_t count = script_snapshot_build(&store, buf, 16);
    ASSERT(count == 2);

    /* Find entity 0's snapshot and check attrs. */
    const script_entity_snapshot_t *snap = NULL;
    for (uint32_t i = 0; i < count; i++) {
        if (buf[i].entity_id == 0) { snap = &buf[i]; break; }
    }
    ASSERT(snap != NULL);

    uint8_t t, s;
    const void *data = script_entity_get_attr(snap, SCRIPT_KEY_USER, &t, &s);
    ASSERT(data != NULL);
    ASSERT(t == SCRIPT_ATTR_F32);
    ASSERT_FLOAT_EQ(*(const float *)data, 100.0f);

    edit_entity_store_destroy(&store);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Update buffer tests                                                       */
/* ----------------------------------------------------------------------- */

/** Init update buffer, verify empty. */
static bool test_update_buffer_init(void) {
    script_update_buffer_t ubuf;
    ASSERT(script_update_buffer_init(&ubuf, 4096));
    ASSERT(ubuf.used[0] == 0);
    ASSERT(ubuf.used[1] == 0);
    ASSERT(ubuf.capacity == 4096);
    script_update_buffer_destroy(&ubuf);
    return true;
}

/** Swap moves back buffer to front. */
static bool test_update_buffer_swap(void) {
    script_update_buffer_t ubuf;
    ASSERT(script_update_buffer_init(&ubuf, 4096));

    /* Write some data to back buffer (index 1). */
    ubuf.used[1] = 42;

    script_update_buffer_swap(&ubuf);

    /* After swap: front (0) should have 42 bytes, back (1) should be 0. */
    ASSERT(ubuf.used[0] == 42);
    ASSERT(ubuf.used[1] == 0);

    script_update_buffer_destroy(&ubuf);
    return true;
}

/* ----------------------------------------------------------------------- */
/* script_env_write_attr tests                                               */
/* ----------------------------------------------------------------------- */

/** Write a single attribute update to the blob. */
static bool test_write_attr_single(void) {
    script_env_t env;
    uint8_t blob[4096];
    script_env_init_blob(&env, blob, sizeof(blob));

    float pos[3] = { 10.0f, 20.0f, 30.0f };
    script_env_write_attr(&env, 42, 0, SCRIPT_KEY_POS,
                          SCRIPT_ATTR_VEC3, pos, sizeof(pos));

    /* Verify blob has one update header + one attr write. */
    ASSERT(env.update_blob_used > 0);

    /* Parse back: read the update header. */
    const script_entity_update_t *upd =
        (const script_entity_update_t *)blob;
    ASSERT(upd->entity_id == 42);
    ASSERT(upd->attr_count == 1);

    /* Read the attr write following the header. */
    const script_attr_write_t *aw =
        (const script_attr_write_t *)(blob + sizeof(script_entity_update_t));
    ASSERT(aw->key == SCRIPT_KEY_POS);
    ASSERT(aw->type == SCRIPT_ATTR_VEC3);
    ASSERT(aw->size == 12);

    /* Read the payload. */
    const float *p = (const float *)(blob + sizeof(script_entity_update_t)
                                     + sizeof(script_attr_write_t));
    ASSERT_FLOAT_EQ(p[0], 10.0f);
    ASSERT_FLOAT_EQ(p[1], 20.0f);
    ASSERT_FLOAT_EQ(p[2], 30.0f);

    return true;
}

/** Write multiple attributes for the same entity appends to same update. */
static bool test_write_attr_same_entity(void) {
    script_env_t env;
    uint8_t blob[4096];
    script_env_init_blob(&env, blob, sizeof(blob));

    float pos[3] = { 1.0f, 2.0f, 3.0f };
    float rot[3] = { 0.0f, 90.0f, 0.0f };
    script_env_write_attr(&env, 7, 0, SCRIPT_KEY_POS,
                          SCRIPT_ATTR_VEC3, pos, sizeof(pos));
    script_env_write_attr(&env, 7, 0, SCRIPT_KEY_ROT,
                          SCRIPT_ATTR_VEC3, rot, sizeof(rot));

    /* Should still be one update header with attr_count=2. */
    const script_entity_update_t *upd =
        (const script_entity_update_t *)blob;
    ASSERT(upd->entity_id == 7);
    ASSERT(upd->attr_count == 2);

    return true;
}

/** Write attributes for different entities creates separate updates. */
static bool test_write_attr_different_entities(void) {
    script_env_t env;
    uint8_t blob[4096];
    script_env_init_blob(&env, blob, sizeof(blob));

    float v1 = 1.0f, v2 = 2.0f;
    script_env_write_attr(&env, 10, 0, SCRIPT_KEY_USER,
                          SCRIPT_ATTR_F32, &v1, sizeof(v1));
    script_env_write_attr(&env, 20, 0, SCRIPT_KEY_USER,
                          SCRIPT_ATTR_F32, &v2, sizeof(v2));

    /* Parse: first update for entity 10. */
    const script_entity_update_t *upd1 =
        (const script_entity_update_t *)blob;
    ASSERT(upd1->entity_id == 10);
    ASSERT(upd1->attr_count == 1);

    /* Second update follows at offset total_size. */
    const script_entity_update_t *upd2 =
        (const script_entity_update_t *)(blob + upd1->total_size);
    ASSERT(upd2->entity_id == 20);
    ASSERT(upd2->attr_count == 1);

    return true;
}

/** Write attr fails gracefully when blob is full. */
static bool test_write_attr_blob_full(void) {
    script_env_t env;
    /* Tiny blob that can't fit even one update. */
    uint8_t blob[4];
    script_env_init_blob(&env, blob, sizeof(blob));

    float v = 1.0f;
    script_env_write_attr(&env, 1, 0, SCRIPT_KEY_USER,
                          SCRIPT_ATTR_F32, &v, sizeof(v));
    /* Should not crash — just silently fail. */
    ASSERT(env.update_blob_used == 0);

    return true;
}

/** Reset clears the update blob. */
static bool test_env_reset(void) {
    script_env_t env;
    uint8_t blob[4096];
    script_env_init_blob(&env, blob, sizeof(blob));

    float v = 1.0f;
    script_env_write_attr(&env, 1, 0, SCRIPT_KEY_USER,
                          SCRIPT_ATTR_F32, &v, sizeof(v));
    ASSERT(env.update_blob_used > 0);

    script_env_reset(&env);
    ASSERT(env.update_blob_used == 0);

    return true;
}

/* ----------------------------------------------------------------------- */
/* script_entity_get_attr tests                                              */
/* ----------------------------------------------------------------------- */

/** Get attr from snapshot entity with dynamic attrs. */
static bool test_get_attr_from_snapshot(void) {
    script_entity_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    entity_attrs_init(&snap.attrs);

    int32_t hp = 75;
    entity_attrs_set(&snap.attrs, SCRIPT_KEY_USER + 1, SCRIPT_ATTR_I32,
                     &hp, sizeof(hp));

    uint8_t t, s;
    const void *data = script_entity_get_attr(&snap, SCRIPT_KEY_USER + 1,
                                              &t, &s);
    ASSERT(data != NULL);
    ASSERT(t == SCRIPT_ATTR_I32);
    ASSERT(*(const int32_t *)data == 75);
    return true;
}

/** Get attr returns NULL for missing key. */
static bool test_get_attr_missing(void) {
    script_entity_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    entity_attrs_init(&snap.attrs);

    uint8_t t, s;
    ASSERT(script_entity_get_attr(&snap, SCRIPT_KEY_USER, &t, &s) == NULL);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Iterator tests                                                            */
/* ----------------------------------------------------------------------- */

/** Iterate updates in blob. */
static bool test_iterate_updates(void) {
    script_env_t env;
    uint8_t blob[4096];
    script_env_init_blob(&env, blob, sizeof(blob));

    float v1 = 1.0f, v2 = 2.0f, v3 = 3.0f;
    script_env_write_attr(&env, 10, 0, SCRIPT_KEY_USER,
                          SCRIPT_ATTR_F32, &v1, sizeof(v1));
    script_env_write_attr(&env, 20, 0, SCRIPT_KEY_USER,
                          SCRIPT_ATTR_F32, &v2, sizeof(v2));
    script_env_write_attr(&env, 30, 0, SCRIPT_KEY_USER,
                          SCRIPT_ATTR_F32, &v3, sizeof(v3));

    /* Iterate using the public iterator pattern. */
    uint32_t offset = 0;
    int count = 0;
    uint32_t seen_ids[3] = {0};
    while (offset < env.update_blob_used) {
        const script_entity_update_t *upd =
            (const script_entity_update_t *)(blob + offset);
        ASSERT(upd->total_size > 0);
        if (count < 3) seen_ids[count] = upd->entity_id;
        count++;
        offset += upd->total_size;
    }
    ASSERT(count == 3);
    ASSERT(seen_ids[0] == 10);
    ASSERT(seen_ids[1] == 20);
    ASSERT(seen_ids[2] == 30);
    return true;
}

/* ----------------------------------------------------------------------- */
/* main                                                                      */
/* ----------------------------------------------------------------------- */

int main(void) {
    /* Snapshot builder */
    RUN(test_snapshot_empty_store);
    RUN(test_snapshot_captures_fields);
    RUN(test_snapshot_truncation);
    RUN(test_snapshot_skips_inactive);
    RUN(test_snapshot_copies_attrs);

    /* Update buffer */
    RUN(test_update_buffer_init);
    RUN(test_update_buffer_swap);

    /* script_env_write_attr */
    RUN(test_write_attr_single);
    RUN(test_write_attr_same_entity);
    RUN(test_write_attr_different_entities);
    RUN(test_write_attr_blob_full);
    RUN(test_env_reset);

    /* script_entity_get_attr */
    RUN(test_get_attr_from_snapshot);
    RUN(test_get_attr_missing);

    /* Iterator */
    RUN(test_iterate_updates);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
