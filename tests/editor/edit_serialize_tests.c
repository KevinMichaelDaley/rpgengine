/**
 * @file edit_serialize_tests.c
 * @brief Tests for level serialization: serialize, deserialize, save, load.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_serialize.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_dispatch.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/editor/edit_undo.h"
#include "ferrum/editor/json_parse.h"

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

#define ASSERT_NEAR(a, b, eps) \
    ASSERT(fabs((double)(a) - (double)(b)) < (eps))

/* ----------------------------------------------------------------------- */
/* Buffer serialization tests                                                */
/* ----------------------------------------------------------------------- */

/** Serialize an empty store → valid JSON with empty entities array. */
static bool test_serialize_empty(void) {
    edit_entity_store_t store;
    edit_entity_store_init(&store, 16);

    char buf[4096];
    size_t n = edit_level_serialize(&store, buf, sizeof(buf));
    ASSERT(n > 0);

    /* Should contain version and empty entities array. */
    ASSERT(strstr(buf, "\"version\":1") != NULL);
    ASSERT(strstr(buf, "\"entities\":[]") != NULL);

    edit_entity_store_destroy(&store);
    return true;
}

/** Serialize one box entity. */
static bool test_serialize_one(void) {
    edit_entity_store_t store;
    edit_entity_store_init(&store, 16);

    uint32_t eid = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    edit_entity_t *e = edit_entity_store_get_mut(&store, eid);
    e->pos[0] = 1.5f; e->pos[1] = 2.0f; e->pos[2] = -3.0f;
    e->scale[0] = 2.0f; e->scale[1] = 2.0f; e->scale[2] = 2.0f;

    char buf[4096];
    size_t n = edit_level_serialize(&store, buf, sizeof(buf));
    ASSERT(n > 0);

    /* Parse and verify structure. */
    uint8_t arena_buf[8192];
    json_arena_t arena;
    json_arena_init(&arena, arena_buf, sizeof(arena_buf));
    json_value_t root;
    ASSERT(json_parse(buf, n, &arena, &root));

    const json_value_t *entities = json_object_get(&root, "entities");
    ASSERT(entities && entities->type == JSON_ARRAY);
    ASSERT(entities->array.count == 1);

    const json_value_t *ent = &entities->array.items[0];
    const json_value_t *pos = json_object_get(ent, "pos");
    ASSERT(pos && pos->type == JSON_ARRAY && pos->array.count == 3);
    ASSERT_NEAR(pos->array.items[0].number, 1.5, 0.01);
    ASSERT_NEAR(pos->array.items[1].number, 2.0, 0.01);
    ASSERT_NEAR(pos->array.items[2].number, -3.0, 0.01);

    edit_entity_store_destroy(&store);
    return true;
}

/** Serialize multiple entities; only active ones are included. */
static bool test_serialize_multi(void) {
    edit_entity_store_t store;
    edit_entity_store_init(&store, 16);

    edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    uint32_t mid = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_SPHERE);
    edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    edit_entity_store_remove(&store, mid); /* Remove middle entity. */

    char buf[4096];
    size_t n = edit_level_serialize(&store, buf, sizeof(buf));
    ASSERT(n > 0);

    /* Parse and count entities. */
    uint8_t arena_buf[8192];
    json_arena_t arena;
    json_arena_init(&arena, arena_buf, sizeof(arena_buf));
    json_value_t root;
    ASSERT(json_parse(buf, n, &arena, &root));

    const json_value_t *entities = json_object_get(&root, "entities");
    ASSERT(entities && entities->array.count == 2);

    edit_entity_store_destroy(&store);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Deserialization tests                                                     */
/* ----------------------------------------------------------------------- */

/** Deserialize valid JSON into empty store. */
static bool test_deserialize_basic(void) {
    const char *json =
        "{\"version\":1,\"entities\":["
        "{\"id\":0,\"type\":\"box\",\"pos\":[1,2,3],"
        "\"rot\":[0,0,0],\"scale\":[1,1,1]}]}";

    edit_entity_store_t store;
    edit_entity_store_init(&store, 16);

    ASSERT(edit_level_deserialize(&store, json, strlen(json)));
    ASSERT(edit_entity_store_count(&store) == 1);

    const edit_entity_t *e = edit_entity_store_get(&store, 0);
    ASSERT(e != NULL);
    ASSERT(e->type == EDIT_ENTITY_TYPE_BOX);
    ASSERT_NEAR(e->pos[0], 1.0f, 0.001);
    ASSERT_NEAR(e->pos[1], 2.0f, 0.001);
    ASSERT_NEAR(e->pos[2], 3.0f, 0.001);

    edit_entity_store_destroy(&store);
    return true;
}

/** Deserialize clears existing entities first. */
static bool test_deserialize_clears_existing(void) {
    edit_entity_store_t store;
    edit_entity_store_init(&store, 16);

    /* Pre-populate. */
    edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    ASSERT(edit_entity_store_count(&store) == 2);

    const char *json =
        "{\"version\":1,\"entities\":["
        "{\"id\":0,\"type\":\"sphere\",\"pos\":[0,0,0],"
        "\"rot\":[0,0,0],\"scale\":[1,1,1]}]}";

    ASSERT(edit_level_deserialize(&store, json, strlen(json)));
    ASSERT(edit_entity_store_count(&store) == 1);

    edit_entity_store_destroy(&store);
    return true;
}

/** Round-trip: serialize → deserialize → compare. */
static bool test_roundtrip(void) {
    edit_entity_store_t store;
    edit_entity_store_init(&store, 32);

    /* Create diverse entities. */
    uint32_t a = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    edit_entity_t *ea = edit_entity_store_get_mut(&store, a);
    ea->pos[0] = 10.5f; ea->pos[1] = -3.2f; ea->pos[2] = 0.0f;
    ea->rot[0] = 45.0f; ea->rot[1] = 90.0f; ea->rot[2] = 0.0f;
    ea->scale[0] = 2.0f; ea->scale[1] = 1.0f; ea->scale[2] = 0.5f;

    uint32_t b = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_SPHERE);
    edit_entity_t *eb = edit_entity_store_get_mut(&store, b);
    eb->pos[0] = -1.0f; eb->pos[1] = 100.0f; eb->pos[2] = 7.7f;

    /* Serialize. */
    char buf[16384];
    size_t n = edit_level_serialize(&store, buf, sizeof(buf));
    ASSERT(n > 0);

    /* Deserialize into a fresh store. */
    edit_entity_store_t store2;
    edit_entity_store_init(&store2, 32);
    ASSERT(edit_level_deserialize(&store2, buf, n));
    ASSERT(edit_entity_store_count(&store2) == 2);

    const edit_entity_t *ra = edit_entity_store_get(&store2, a);
    ASSERT(ra != NULL);
    ASSERT_NEAR(ra->pos[0], 10.5f, 0.01);
    ASSERT_NEAR(ra->pos[1], -3.2f, 0.01);
    ASSERT_NEAR(ra->rot[0], 45.0f, 0.01);
    ASSERT_NEAR(ra->scale[0], 2.0f, 0.01);

    const edit_entity_t *rb = edit_entity_store_get(&store2, b);
    ASSERT(rb != NULL);
    ASSERT(rb->type == EDIT_ENTITY_TYPE_SPHERE);
    ASSERT_NEAR(rb->pos[1], 100.0f, 0.01);

    edit_entity_store_destroy(&store);
    edit_entity_store_destroy(&store2);
    return true;
}

/* ----------------------------------------------------------------------- */
/* File I/O tests                                                            */
/* ----------------------------------------------------------------------- */

/** Save to file and load back. */
static bool test_file_roundtrip(void) {
    edit_entity_store_t store;
    edit_entity_store_init(&store, 16);

    uint32_t eid = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    edit_entity_t *e = edit_entity_store_get_mut(&store, eid);
    e->pos[0] = 42.0f;

    const char *path = "/tmp/test_edit_level.json";
    ASSERT(edit_level_save(&store, path));

    edit_entity_store_t store2;
    edit_entity_store_init(&store2, 16);
    ASSERT(edit_level_load(&store2, path));

    ASSERT(edit_entity_store_count(&store2) == 1);
    const edit_entity_t *loaded = edit_entity_store_get(&store2, 0);
    ASSERT(loaded != NULL);
    ASSERT_NEAR(loaded->pos[0], 42.0f, 0.01);

    unlink(path);
    edit_entity_store_destroy(&store);
    edit_entity_store_destroy(&store2);
    return true;
}

/** Path with ".." is rejected. */
static bool test_path_traversal_rejected(void) {
    edit_entity_store_t store;
    edit_entity_store_init(&store, 16);

    ASSERT(!edit_level_save(&store, "/tmp/../etc/passwd"));
    ASSERT(!edit_level_load(&store, "../../etc/passwd"));

    edit_entity_store_destroy(&store);
    return true;
}

/** Deserialize invalid JSON → error. */
static bool test_deserialize_invalid_json(void) {
    edit_entity_store_t store;
    edit_entity_store_init(&store, 16);
    ASSERT(!edit_level_deserialize(&store, "not json!", 9));
    edit_entity_store_destroy(&store);
    return true;
}

/** Load nonexistent file → error. */
static bool test_load_missing_file(void) {
    edit_entity_store_t store;
    edit_entity_store_init(&store, 16);
    ASSERT(!edit_level_load(&store, "/tmp/nonexistent_level_12345.json"));
    edit_entity_store_destroy(&store);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Dispatch command tests                                                    */
/* ----------------------------------------------------------------------- */

/** cmd_save and cmd_load via dispatch. */
static bool test_cmd_save_load(void) {
    edit_dispatch_t dispatch;
    edit_entity_store_t entities;
    edit_selection_t selection;
    edit_undo_stack_t undo;
    edit_cmd_ctx_t ctx;

    edit_dispatch_init(&dispatch, 8192, &ctx);
    edit_entity_store_init(&entities, 64);
    edit_selection_init(&selection);
    edit_undo_init(&undo, 64, 1024 * 1024);
    ctx.entities = &entities;
    ctx.selection = &selection;
    ctx.undo = &undo;
    edit_commands_register_all(&dispatch);

    /* Spawn an entity. */
    char resp[4096];
    const char *spawn_cmd = "{\"id\":1,\"cmd\":\"spawn\","
                            "\"args\":{\"type\":\"box\",\"pos\":[99,88,77]}}";
    edit_dispatch_exec(&dispatch, spawn_cmd, (uint32_t)strlen(spawn_cmd),
                       resp, sizeof(resp));

    /* Save. */
    const char *save_cmd = "{\"id\":2,\"cmd\":\"save\","
                           "\"args\":{\"path\":\"/tmp/test_cmd_level.json\"}}";
    uint32_t n = edit_dispatch_exec(&dispatch, save_cmd,
                                     (uint32_t)strlen(save_cmd),
                                     resp, sizeof(resp));
    ASSERT(n > 0);
    ASSERT(strstr(resp, "\"ok\":true") != NULL);

    /* Load into a clean entity store (simulate by removing all). */
    edit_entity_store_remove(&entities, 0);
    ASSERT(edit_entity_store_count(&entities) == 0);

    const char *load_cmd = "{\"id\":3,\"cmd\":\"load\","
                           "\"args\":{\"path\":\"/tmp/test_cmd_level.json\"}}";
    n = edit_dispatch_exec(&dispatch, load_cmd, (uint32_t)strlen(load_cmd),
                           resp, sizeof(resp));
    ASSERT(n > 0);
    ASSERT(strstr(resp, "\"ok\":true") != NULL);
    ASSERT(edit_entity_store_count(&entities) == 1);

    const edit_entity_t *e = edit_entity_store_get(&entities, 0);
    ASSERT(e != NULL);
    ASSERT_NEAR(e->pos[0], 99.0f, 0.01);

    unlink("/tmp/test_cmd_level.json");
    edit_dispatch_destroy(&dispatch);
    edit_entity_store_destroy(&entities);
    edit_selection_destroy(&selection);
    edit_undo_destroy(&undo);
    return true;
}

/** Null params to serialization functions. */
static bool test_null_params(void) {
    ASSERT(edit_level_serialize(NULL, NULL, 0) == 0);
    ASSERT(!edit_level_deserialize(NULL, NULL, 0));
    ASSERT(!edit_level_save(NULL, NULL));
    ASSERT(!edit_level_load(NULL, NULL));
    return true;
}

/* ----------------------------------------------------------------------- */
/* Main                                                                      */
/* ----------------------------------------------------------------------- */

int main(void) {
    RUN(test_serialize_empty);
    RUN(test_serialize_one);
    RUN(test_serialize_multi);
    RUN(test_deserialize_basic);
    RUN(test_deserialize_clears_existing);
    RUN(test_roundtrip);
    RUN(test_file_roundtrip);
    RUN(test_path_traversal_rejected);
    RUN(test_deserialize_invalid_json);
    RUN(test_load_missing_file);
    RUN(test_cmd_save_load);
    RUN(test_null_params);

    printf("\n%d / %d tests passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
