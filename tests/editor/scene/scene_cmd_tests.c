/**
 * @file scene_cmd_tests.c
 * @brief Unit tests for scene command formatting and response parsing.
 *
 * Tests cover: spawn/delete/select/deselect/list/move/rotate/scale command
 * formatting, buffer overflow handling, and JSON response parsing (ok, error,
 * boolean result).
 */

#include "ferrum/editor/scene/scene_cmd.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/json_parse.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ----------------------------------------------------------------------- */
/* Test harness                                                             */
/* ----------------------------------------------------------------------- */

static int g_pass, g_fail;

#define RUN(fn) do { \
    setup(); \
    printf("RUN  %s\n", #fn); \
    if (fn()) { printf("OK   %s\n", #fn); g_pass++; } \
    else { printf("FAIL %s\n", #fn); g_fail++; } \
    teardown(); \
} while (0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  ASSERT FAILED: %s (line %d)\n", #cond, __LINE__); \
        return false; \
    } \
} while (0)

/* ----------------------------------------------------------------------- */
/* Shared state                                                             */
/* ----------------------------------------------------------------------- */

/** Buffer for formatted commands. */
static char g_buf[1024];

/** Arena buffer for JSON parsing verification. */
static uint8_t g_arena_buf[16 * 1024];

static void setup(void) {
    memset(g_buf, 0, sizeof(g_buf));
    memset(g_arena_buf, 0, sizeof(g_arena_buf));
}

static void teardown(void) {
    /* Nothing to clean up. */
}

/**
 * @brief Helper: parse g_buf as JSON and return the root value.
 * @param arena  Arena to use (caller provides).
 * @param out    Output value.
 * @return true if the buffer contains valid JSON.
 */
static bool parse_buf(json_arena_t *arena, json_value_t *out) {
    json_arena_init(arena, g_arena_buf, sizeof(g_arena_buf));
    return json_parse(g_buf, strlen(g_buf), arena, out);
}

/**
 * @brief Helper: extract a null-terminated string from a JSON string value.
 * @param val  JSON string value.
 * @param dst  Output buffer.
 * @param cap  Capacity of dst.
 * @return true on success.
 */
static bool extract_str(const json_value_t *val, char *dst, size_t cap) {
    return json_string_copy(val, dst, cap);
}

/* ----------------------------------------------------------------------- */
/* Spawn command tests                                                      */
/* ----------------------------------------------------------------------- */

static bool test_format_spawn(void) {
    float pos[3] = {1.0f, 2.5f, -3.0f};
    int n = scene_cmd_format_spawn(g_buf, sizeof(g_buf), 1,
                                   EDIT_ENTITY_TYPE_BOX, pos, "test_box");
    ASSERT(n > 0);
    ASSERT((size_t)n == strlen(g_buf));

    /* Verify the output is valid JSON. */
    json_arena_t arena;
    json_value_t root;
    ASSERT(parse_buf(&arena, &root));
    ASSERT(root.type == JSON_OBJECT);

    /* Check "id" field. */
    const json_value_t *id_val = json_object_get(&root, "id");
    ASSERT(id_val != NULL);
    ASSERT(id_val->type == JSON_NUMBER);
    ASSERT((uint32_t)id_val->number == 1);

    /* Check "cmd" field. */
    const json_value_t *cmd_val = json_object_get(&root, "cmd");
    ASSERT(cmd_val != NULL);
    ASSERT(cmd_val->type == JSON_STRING);
    char cmd_str[32];
    ASSERT(extract_str(cmd_val, cmd_str, sizeof(cmd_str)));
    ASSERT(strcmp(cmd_str, "spawn") == 0);

    /* Check "args" object. */
    const json_value_t *args = json_object_get(&root, "args");
    ASSERT(args != NULL);
    ASSERT(args->type == JSON_OBJECT);

    /* args.type should be the entity type name string. */
    const json_value_t *type_val = json_object_get(args, "type");
    ASSERT(type_val != NULL);
    ASSERT(type_val->type == JSON_STRING);
    char type_str[32];
    ASSERT(extract_str(type_val, type_str, sizeof(type_str)));
    ASSERT(strcmp(type_str, "box") == 0);

    /* args.pos should be [1.0, 2.5, -3.0]. */
    const json_value_t *pos_val = json_object_get(args, "pos");
    ASSERT(pos_val != NULL);
    ASSERT(pos_val->type == JSON_ARRAY);
    ASSERT(pos_val->array.count == 3);

    const json_value_t *px = json_array_get(pos_val, 0);
    const json_value_t *py = json_array_get(pos_val, 1);
    const json_value_t *pz = json_array_get(pos_val, 2);
    ASSERT(px != NULL && py != NULL && pz != NULL);
    ASSERT(px->number > 0.99 && px->number < 1.01);
    ASSERT(py->number > 2.49 && py->number < 2.51);
    ASSERT(pz->number > -3.01 && pz->number < -2.99);

    /* args.name should be "test_box". */
    const json_value_t *name_val = json_object_get(args, "name");
    ASSERT(name_val != NULL);
    ASSERT(name_val->type == JSON_STRING);
    char name_str[64];
    ASSERT(extract_str(name_val, name_str, sizeof(name_str)));
    ASSERT(strcmp(name_str, "test_box") == 0);

    return true;
}

static bool test_format_spawn_buffer_too_small(void) {
    float pos[3] = {0.0f, 0.0f, 0.0f};
    /* A 4-byte buffer is far too small for a spawn command JSON. */
    char tiny[4];
    int n = scene_cmd_format_spawn(tiny, sizeof(tiny), 1,
                                   EDIT_ENTITY_TYPE_BOX, pos, "box");
    ASSERT(n == -1);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Delete command tests                                                     */
/* ----------------------------------------------------------------------- */

static bool test_format_delete(void) {
    int n = scene_cmd_format_delete(g_buf, sizeof(g_buf), 7);
    ASSERT(n > 0);

    json_arena_t arena;
    json_value_t root;
    ASSERT(parse_buf(&arena, &root));
    ASSERT(root.type == JSON_OBJECT);

    /* Check "cmd" is "delete". */
    const json_value_t *cmd_val = json_object_get(&root, "cmd");
    ASSERT(cmd_val != NULL);
    char cmd_str[32];
    ASSERT(extract_str(cmd_val, cmd_str, sizeof(cmd_str)));
    ASSERT(strcmp(cmd_str, "delete") == 0);

    /* Check "id". */
    const json_value_t *id_val = json_object_get(&root, "id");
    ASSERT(id_val != NULL);
    ASSERT((uint32_t)id_val->number == 7);

    return true;
}

/* ----------------------------------------------------------------------- */
/* Select / deselect command tests                                          */
/* ----------------------------------------------------------------------- */

static bool test_format_select(void) {
    int n = scene_cmd_format_select(g_buf, sizeof(g_buf), 3, 42);
    ASSERT(n > 0);

    json_arena_t arena;
    json_value_t root;
    ASSERT(parse_buf(&arena, &root));
    ASSERT(root.type == JSON_OBJECT);

    /* Check "cmd" is "select". */
    const json_value_t *cmd_val = json_object_get(&root, "cmd");
    ASSERT(cmd_val != NULL);
    char cmd_str[32];
    ASSERT(extract_str(cmd_val, cmd_str, sizeof(cmd_str)));
    ASSERT(strcmp(cmd_str, "select") == 0);

    /* Check "id". */
    const json_value_t *id_val = json_object_get(&root, "id");
    ASSERT(id_val != NULL);
    ASSERT((uint32_t)id_val->number == 3);

    /* Check args.entity_id. */
    const json_value_t *args = json_object_get(&root, "args");
    ASSERT(args != NULL);
    const json_value_t *eid = json_object_get(args, "entity_id");
    ASSERT(eid != NULL);
    ASSERT(eid->type == JSON_NUMBER);
    ASSERT((uint32_t)eid->number == 42);

    return true;
}

static bool test_format_deselect(void) {
    int n = scene_cmd_format_deselect(g_buf, sizeof(g_buf), 4, 99);
    ASSERT(n > 0);

    json_arena_t arena;
    json_value_t root;
    ASSERT(parse_buf(&arena, &root));
    ASSERT(root.type == JSON_OBJECT);

    /* Check "cmd" is "deselect". */
    const json_value_t *cmd_val = json_object_get(&root, "cmd");
    ASSERT(cmd_val != NULL);
    char cmd_str[32];
    ASSERT(extract_str(cmd_val, cmd_str, sizeof(cmd_str)));
    ASSERT(strcmp(cmd_str, "deselect") == 0);

    /* Check args.entity_id. */
    const json_value_t *args = json_object_get(&root, "args");
    ASSERT(args != NULL);
    const json_value_t *eid = json_object_get(args, "entity_id");
    ASSERT(eid != NULL);
    ASSERT((uint32_t)eid->number == 99);

    return true;
}

/* ----------------------------------------------------------------------- */
/* List command tests                                                       */
/* ----------------------------------------------------------------------- */

static bool test_format_list(void) {
    int n = scene_cmd_format_list(g_buf, sizeof(g_buf), 10);
    ASSERT(n > 0);

    json_arena_t arena;
    json_value_t root;
    ASSERT(parse_buf(&arena, &root));
    ASSERT(root.type == JSON_OBJECT);

    /* Check "cmd" is "list_entities". */
    const json_value_t *cmd_val = json_object_get(&root, "cmd");
    ASSERT(cmd_val != NULL);
    char cmd_str[32];
    ASSERT(extract_str(cmd_val, cmd_str, sizeof(cmd_str)));
    ASSERT(strcmp(cmd_str, "list_entities") == 0);

    /* Check "id". */
    const json_value_t *id_val = json_object_get(&root, "id");
    ASSERT(id_val != NULL);
    ASSERT((uint32_t)id_val->number == 10);

    return true;
}

/* ----------------------------------------------------------------------- */
/* Transform command tests (move, rotate, scale)                            */
/* ----------------------------------------------------------------------- */

static bool test_format_move(void) {
    float delta[3] = {5.0f, -1.0f, 0.0f};
    int n = scene_cmd_format_move(g_buf, sizeof(g_buf), 2, delta);
    ASSERT(n > 0);

    json_arena_t arena;
    json_value_t root;
    ASSERT(parse_buf(&arena, &root));

    /* Check "cmd" is "move". */
    const json_value_t *cmd_val = json_object_get(&root, "cmd");
    ASSERT(cmd_val != NULL);
    char cmd_str[32];
    ASSERT(extract_str(cmd_val, cmd_str, sizeof(cmd_str)));
    ASSERT(strcmp(cmd_str, "move") == 0);

    /* Check args.delta array. */
    const json_value_t *args = json_object_get(&root, "args");
    ASSERT(args != NULL);
    const json_value_t *d = json_object_get(args, "delta");
    ASSERT(d != NULL);
    ASSERT(d->type == JSON_ARRAY);
    ASSERT(d->array.count == 3);
    ASSERT(json_array_get(d, 0)->number > 4.99 &&
           json_array_get(d, 0)->number < 5.01);
    ASSERT(json_array_get(d, 1)->number > -1.01 &&
           json_array_get(d, 1)->number < -0.99);
    ASSERT(json_array_get(d, 2)->number > -0.01 &&
           json_array_get(d, 2)->number < 0.01);

    return true;
}

static bool test_format_rotate(void) {
    float delta[3] = {0.0f, 90.0f, 0.0f};
    int n = scene_cmd_format_rotate(g_buf, sizeof(g_buf), 5, delta);
    ASSERT(n > 0);

    json_arena_t arena;
    json_value_t root;
    ASSERT(parse_buf(&arena, &root));

    /* Check "cmd" is "rotate". */
    const json_value_t *cmd_val = json_object_get(&root, "cmd");
    ASSERT(cmd_val != NULL);
    char cmd_str[32];
    ASSERT(extract_str(cmd_val, cmd_str, sizeof(cmd_str)));
    ASSERT(strcmp(cmd_str, "rotate") == 0);

    /* Check args.delta array. */
    const json_value_t *args = json_object_get(&root, "args");
    ASSERT(args != NULL);
    const json_value_t *d = json_object_get(args, "delta");
    ASSERT(d != NULL);
    ASSERT(d->type == JSON_ARRAY);
    ASSERT(d->array.count == 3);
    ASSERT(json_array_get(d, 1)->number > 89.99 &&
           json_array_get(d, 1)->number < 90.01);

    return true;
}

static bool test_format_scale(void) {
    float factor[3] = {2.0f, 2.0f, 0.5f};
    int n = scene_cmd_format_scale(g_buf, sizeof(g_buf), 6, factor);
    ASSERT(n > 0);

    json_arena_t arena;
    json_value_t root;
    ASSERT(parse_buf(&arena, &root));

    /* Check "cmd" is "scale". */
    const json_value_t *cmd_val = json_object_get(&root, "cmd");
    ASSERT(cmd_val != NULL);
    char cmd_str[32];
    ASSERT(extract_str(cmd_val, cmd_str, sizeof(cmd_str)));
    ASSERT(strcmp(cmd_str, "scale") == 0);

    /* Check args.factor array. */
    const json_value_t *args = json_object_get(&root, "args");
    ASSERT(args != NULL);
    const json_value_t *f = json_object_get(args, "factor");
    ASSERT(f != NULL);
    ASSERT(f->type == JSON_ARRAY);
    ASSERT(f->array.count == 3);
    ASSERT(json_array_get(f, 0)->number > 1.99 &&
           json_array_get(f, 0)->number < 2.01);
    ASSERT(json_array_get(f, 1)->number > 1.99 &&
           json_array_get(f, 1)->number < 2.01);
    ASSERT(json_array_get(f, 2)->number > 0.49 &&
           json_array_get(f, 2)->number < 0.51);

    return true;
}

/* ----------------------------------------------------------------------- */
/* Response parsing tests                                                   */
/* ----------------------------------------------------------------------- */

static bool test_parse_ok_response(void) {
    const char *json = "{\"id\":1,\"ok\":true,\"result\":42}";
    scene_cmd_response_t resp;
    memset(&resp, 0, sizeof(resp));

    ASSERT(scene_cmd_parse_response(json, strlen(json), &resp));
    ASSERT(resp.id == 1);
    ASSERT(resp.ok == true);
    ASSERT(resp.has_result == true);
    ASSERT(resp.result_number > 41.99 && resp.result_number < 42.01);

    return true;
}

static bool test_parse_error_response(void) {
    const char *json = "{\"id\":1,\"ok\":false,\"error\":\"not_found\"}";
    scene_cmd_response_t resp;
    memset(&resp, 0, sizeof(resp));

    ASSERT(scene_cmd_parse_response(json, strlen(json), &resp));
    ASSERT(resp.id == 1);
    ASSERT(resp.ok == false);
    ASSERT(strcmp(resp.error, "not_found") == 0);

    return true;
}

static bool test_parse_bool_result(void) {
    const char *json = "{\"id\":1,\"ok\":true,\"result\":true}";
    scene_cmd_response_t resp;
    memset(&resp, 0, sizeof(resp));

    ASSERT(scene_cmd_parse_response(json, strlen(json), &resp));
    ASSERT(resp.id == 1);
    ASSERT(resp.ok == true);
    ASSERT(resp.has_result == true);
    ASSERT(resp.result_bool == true);

    return true;
}

/* ----------------------------------------------------------------------- */
/* Test runner                                                              */
/* ----------------------------------------------------------------------- */

int main(void) {
    g_pass = 0;
    g_fail = 0;

    /* Format tests (happy path). */
    RUN(test_format_spawn);
    RUN(test_format_delete);
    RUN(test_format_select);
    RUN(test_format_deselect);
    RUN(test_format_list);
    RUN(test_format_move);
    RUN(test_format_rotate);
    RUN(test_format_scale);

    /* Format tests (edge / failure). */
    RUN(test_format_spawn_buffer_too_small);

    /* Response parsing tests. */
    RUN(test_parse_ok_response);
    RUN(test_parse_error_response);
    RUN(test_parse_bool_result);

    printf("\n%d / %d tests passed\n", g_pass, g_pass + g_fail);
    return g_fail != 0;
}
