/**
 * @file cmd_entity_tests.c
 * @brief Tests for entity commands: spawn, delete, move, rotate, scale.
 *
 * Each test sets up a dispatch + entity store + selection + undo, registers
 * all commands, executes JSON via dispatch_exec, and verifies entity state
 * and undo entries.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/editor/edit_entity.h"
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
    setup(); \
    printf("RUN  %s\n", #fn); \
    if (fn()) { printf("OK   %s\n", #fn); g_pass++; } \
    else       { printf("FAIL %s\n", #fn); g_fail++; } \
    teardown(); \
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
/* Test context                                                              */
/* ----------------------------------------------------------------------- */

static edit_dispatch_t       g_dispatch;
static edit_entity_store_t   g_entities;
static edit_selection_t      g_selection;
static edit_undo_stack_t     g_undo;
static edit_cmd_ctx_t        g_ctx;

static char g_resp[4096];

static void setup(void) {
    memset(&g_ctx, 0, sizeof(g_ctx));
    edit_dispatch_init(&g_dispatch, 8192, &g_ctx);
    edit_entity_store_init(&g_entities, 256);
    edit_selection_init(&g_selection);
    edit_undo_init(&g_undo, 256, 1024 * 1024);

    g_ctx.entities = &g_entities;
    g_ctx.selection = &g_selection;
    g_ctx.undo = &g_undo;

    edit_commands_register_all(&g_dispatch);
}

static void teardown(void) {
    edit_dispatch_destroy(&g_dispatch);
    edit_entity_store_destroy(&g_entities);
    edit_selection_destroy(&g_selection);
    edit_undo_destroy(&g_undo);
}

/* ----------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ----------------------------------------------------------------------- */

/** @brief Execute a JSON command and store response in g_resp. */
static uint32_t exec(const char *json) {
    memset(g_resp, 0, sizeof(g_resp));
    return edit_dispatch_exec(&g_dispatch, json, (uint32_t)strlen(json),
                              g_resp, sizeof(g_resp));
}

/** @brief Check if response indicates success. */
static bool resp_ok(void) {
    return strstr(g_resp, "\"ok\":true") != NULL;
}

/** @brief Extract the numeric "result" value from the response JSON. */
static double resp_result_number(void) {
    uint8_t arena_buf[4096];
    json_arena_t arena;
    json_arena_init(&arena, arena_buf, sizeof(arena_buf));
    json_value_t root;
    if (!json_parse(g_resp, strlen(g_resp), &arena, &root)) return -1;
    const json_value_t *result = json_object_get(&root, "result");
    if (!result || result->type != JSON_NUMBER) return -1;
    return result->number;
}

/* ----------------------------------------------------------------------- */
/* Spawn tests                                                               */
/* ----------------------------------------------------------------------- */

/** Spawn a box at (1,2,3) and verify entity state. */
static bool test_spawn_box(void) {
    uint32_t n = exec(
        "{\"id\":1,\"cmd\":\"spawn\",\"args\":{\"type\":\"box\","
        "\"pos\":[1,2,3]}}");
    ASSERT(n > 0);
    ASSERT(resp_ok());

    uint32_t eid = (uint32_t)resp_result_number();
    ASSERT(eid != EDIT_ENTITY_INVALID_ID);

    const edit_entity_t *e = edit_entity_store_get(&g_entities, eid);
    ASSERT(e != NULL);
    ASSERT(e->type == EDIT_ENTITY_TYPE_BOX);
    ASSERT_NEAR(e->pos[0], 1.0f, 0.001);
    ASSERT_NEAR(e->pos[1], 2.0f, 0.001);
    ASSERT_NEAR(e->pos[2], 3.0f, 0.001);
    /* Default scale should be (1,1,1). */
    ASSERT_NEAR(e->scale[0], 1.0f, 0.001);
    ASSERT_NEAR(e->scale[1], 1.0f, 0.001);
    ASSERT_NEAR(e->scale[2], 1.0f, 0.001);
    return true;
}

/** Spawn a sphere. */
static bool test_spawn_sphere(void) {
    uint32_t n = exec(
        "{\"id\":1,\"cmd\":\"spawn\",\"args\":{\"type\":\"sphere\","
        "\"pos\":[5,0,0]}}");
    ASSERT(n > 0 && resp_ok());

    uint32_t eid = (uint32_t)resp_result_number();
    const edit_entity_t *e = edit_entity_store_get(&g_entities, eid);
    ASSERT(e && e->type == EDIT_ENTITY_TYPE_SPHERE);
    ASSERT_NEAR(e->pos[0], 5.0f, 0.001);
    return true;
}

/** Spawn with no pos → default (0,0,0). */
static bool test_spawn_default_pos(void) {
    uint32_t n = exec(
        "{\"id\":1,\"cmd\":\"spawn\",\"args\":{\"type\":\"box\"}}");
    ASSERT(n > 0 && resp_ok());

    uint32_t eid = (uint32_t)resp_result_number();
    const edit_entity_t *e = edit_entity_store_get(&g_entities, eid);
    ASSERT(e != NULL);
    ASSERT_NEAR(e->pos[0], 0.0f, 0.001);
    ASSERT_NEAR(e->pos[1], 0.0f, 0.001);
    ASSERT_NEAR(e->pos[2], 0.0f, 0.001);
    return true;
}

/** Spawn records correct undo entry (forward=SPAWN, inverse=DELETE). */
static bool test_spawn_undo_entry(void) {
    exec("{\"id\":1,\"cmd\":\"spawn\",\"args\":{\"type\":\"box\","
         "\"pos\":[1,0,0]}}");
    ASSERT(resp_ok());

    ASSERT(edit_undo_count(&g_undo) == 1);
    const edit_undo_entry_t *entry = edit_undo_peek_undo(&g_undo);
    ASSERT(entry != NULL);
    ASSERT(entry->forward_type == EDIT_CMD_TYPE_SPAWN);
    ASSERT(entry->inverse_type == EDIT_CMD_TYPE_DELETE);

    uint32_t eid = (uint32_t)resp_result_number();
    ASSERT(entry->entity_id == eid);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Delete tests                                                              */
/* ----------------------------------------------------------------------- */

/** Select entity, delete it, verify gone. */
static bool test_delete_selected(void) {
    exec("{\"id\":1,\"cmd\":\"spawn\",\"args\":{\"type\":\"box\"}}");
    uint32_t eid = (uint32_t)resp_result_number();
    edit_selection_add(&g_selection, eid);

    exec("{\"id\":2,\"cmd\":\"delete\",\"args\":{}}");
    ASSERT(resp_ok());

    /* Entity should be removed. */
    ASSERT(edit_entity_store_get(&g_entities, eid) == NULL);
    /* Selection should be cleared. */
    ASSERT(edit_selection_count(&g_selection) == 0);
    return true;
}

/** Delete with empty selection → still succeeds (no-op). */
static bool test_delete_empty_selection(void) {
    exec("{\"id\":1,\"cmd\":\"delete\",\"args\":{}}");
    ASSERT(resp_ok());
    return true;
}

/** Delete records snapshot in undo for future restore. */
static bool test_delete_undo_entry(void) {
    exec("{\"id\":1,\"cmd\":\"spawn\",\"args\":{\"type\":\"box\","
         "\"pos\":[3,4,5]}}");
    uint32_t eid = (uint32_t)resp_result_number();
    edit_selection_add(&g_selection, eid);

    /* Clear spawn's undo entry so we can inspect delete's. */
    edit_undo_clear(&g_undo);

    exec("{\"id\":2,\"cmd\":\"delete\",\"args\":{}}");
    ASSERT(resp_ok());

    ASSERT(edit_undo_count(&g_undo) == 1);
    const edit_undo_entry_t *entry = edit_undo_peek_undo(&g_undo);
    ASSERT(entry->forward_type == EDIT_CMD_TYPE_DELETE);
    ASSERT(entry->inverse_type == EDIT_CMD_TYPE_SPAWN);
    ASSERT(entry->entity_id == eid);
    ASSERT(entry->snapshot_data != NULL);
    ASSERT(entry->snapshot_size == sizeof(edit_entity_t));

    /* Verify the snapshot captured the entity's position. */
    const edit_entity_t *snap = (const edit_entity_t *)entry->snapshot_data;
    ASSERT_NEAR(snap->pos[0], 3.0f, 0.001);
    ASSERT_NEAR(snap->pos[1], 4.0f, 0.001);
    ASSERT_NEAR(snap->pos[2], 5.0f, 0.001);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Move tests                                                                */
/* ----------------------------------------------------------------------- */

/** Move a single selected entity. */
static bool test_move_single(void) {
    exec("{\"id\":1,\"cmd\":\"spawn\",\"args\":{\"type\":\"box\","
         "\"pos\":[0,0,0]}}");
    uint32_t eid = (uint32_t)resp_result_number();
    edit_selection_add(&g_selection, eid);

    exec("{\"id\":2,\"cmd\":\"move\",\"args\":{\"delta\":[5,0,-3]}}");
    ASSERT(resp_ok());

    const edit_entity_t *e = edit_entity_store_get(&g_entities, eid);
    ASSERT_NEAR(e->pos[0],  5.0f, 0.001);
    ASSERT_NEAR(e->pos[1],  0.0f, 0.001);
    ASSERT_NEAR(e->pos[2], -3.0f, 0.001);
    return true;
}

/** Move 3 selected entities — all should shift by delta. */
static bool test_move_multi(void) {
    exec("{\"id\":1,\"cmd\":\"spawn\",\"args\":{\"type\":\"box\","
         "\"pos\":[0,0,0]}}");
    uint32_t eid0 = (uint32_t)resp_result_number();

    exec("{\"id\":2,\"cmd\":\"spawn\",\"args\":{\"type\":\"box\","
         "\"pos\":[10,0,0]}}");
    uint32_t eid1 = (uint32_t)resp_result_number();

    exec("{\"id\":3,\"cmd\":\"spawn\",\"args\":{\"type\":\"box\","
         "\"pos\":[20,0,0]}}");
    uint32_t eid2 = (uint32_t)resp_result_number();

    edit_selection_add(&g_selection, eid0);
    edit_selection_add(&g_selection, eid1);
    edit_selection_add(&g_selection, eid2);
    edit_undo_clear(&g_undo);

    exec("{\"id\":4,\"cmd\":\"move\",\"args\":{\"delta\":[1,2,3]}}");
    ASSERT(resp_ok());

    ASSERT_NEAR(edit_entity_store_get(&g_entities, eid0)->pos[0],  1.0f, 0.001);
    ASSERT_NEAR(edit_entity_store_get(&g_entities, eid1)->pos[0], 11.0f, 0.001);
    ASSERT_NEAR(edit_entity_store_get(&g_entities, eid2)->pos[0], 21.0f, 0.001);

    /* All 3 entries should be in a group. */
    ASSERT(edit_undo_count(&g_undo) == 3);
    return true;
}

/** Move records inverse delta in undo entry. */
static bool test_move_undo_entry(void) {
    exec("{\"id\":1,\"cmd\":\"spawn\",\"args\":{\"type\":\"box\","
         "\"pos\":[5,5,5]}}");
    uint32_t eid = (uint32_t)resp_result_number();
    edit_selection_add(&g_selection, eid);
    edit_undo_clear(&g_undo);

    exec("{\"id\":2,\"cmd\":\"move\",\"args\":{\"delta\":[1,-2,3]}}");
    ASSERT(resp_ok());

    const edit_undo_entry_t *entry = edit_undo_peek_undo(&g_undo);
    ASSERT(entry->forward_type == EDIT_CMD_TYPE_MOVE);
    ASSERT(entry->inverse_type == EDIT_CMD_TYPE_MOVE);
    /* Inverse delta should be negated. */
    ASSERT_NEAR(entry->delta[0], -1.0f, 0.001);
    ASSERT_NEAR(entry->delta[1],  2.0f, 0.001);
    ASSERT_NEAR(entry->delta[2], -3.0f, 0.001);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Rotate tests                                                              */
/* ----------------------------------------------------------------------- */

/** Rotate a single entity. */
static bool test_rotate(void) {
    exec("{\"id\":1,\"cmd\":\"spawn\",\"args\":{\"type\":\"box\"}}");
    uint32_t eid = (uint32_t)resp_result_number();
    edit_selection_add(&g_selection, eid);

    exec("{\"id\":2,\"cmd\":\"rotate\",\"args\":{\"delta\":[0,90,0]}}");
    ASSERT(resp_ok());

    const edit_entity_t *e = edit_entity_store_get(&g_entities, eid);
    ASSERT_NEAR(e->rot[1], 90.0f, 0.001);
    return true;
}

/** Rotate records inverse quaternion in undo entry. */
static bool test_rotate_undo_entry(void) {
    exec("{\"id\":1,\"cmd\":\"spawn\",\"args\":{\"type\":\"box\"}}");
    uint32_t eid = (uint32_t)resp_result_number();
    edit_selection_add(&g_selection, eid);
    edit_undo_clear(&g_undo);

    exec("{\"id\":2,\"cmd\":\"rotate\",\"args\":{\"delta\":[45,90,-30]}}");
    ASSERT(resp_ok());

    const edit_undo_entry_t *entry = edit_undo_peek_undo(&g_undo);
    ASSERT(entry->forward_type == EDIT_CMD_TYPE_ROTATE);
    ASSERT(entry->inverse_type == EDIT_CMD_TYPE_ROTATE);
    /* Undo stores the conjugate (inverse) quaternion in delta[0..3].
     * Verify it's a valid unit quaternion (length ≈ 1). */
    float len2 = entry->delta[0] * entry->delta[0]
               + entry->delta[1] * entry->delta[1]
               + entry->delta[2] * entry->delta[2]
               + entry->delta[3] * entry->delta[3];
    ASSERT_NEAR(len2, 1.0f, 0.01);
    /* w component (delta[3]) should be positive for conjugate of a
     * rotation < 180°. */
    ASSERT(entry->delta[3] > 0.0f);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Scale tests                                                               */
/* ----------------------------------------------------------------------- */

/** Scale a single entity by factors. */
static bool test_scale(void) {
    exec("{\"id\":1,\"cmd\":\"spawn\",\"args\":{\"type\":\"box\"}}");
    uint32_t eid = (uint32_t)resp_result_number();
    edit_selection_add(&g_selection, eid);

    exec("{\"id\":2,\"cmd\":\"scale\",\"args\":{\"factor\":[2,3,0.5]}}");
    ASSERT(resp_ok());

    const edit_entity_t *e = edit_entity_store_get(&g_entities, eid);
    ASSERT_NEAR(e->scale[0], 2.0f,  0.001);
    ASSERT_NEAR(e->scale[1], 3.0f,  0.001);
    ASSERT_NEAR(e->scale[2], 0.5f,  0.001);
    return true;
}

/** Scale records inverse factors in undo entry. */
static bool test_scale_undo_entry(void) {
    exec("{\"id\":1,\"cmd\":\"spawn\",\"args\":{\"type\":\"box\"}}");
    uint32_t eid = (uint32_t)resp_result_number();
    edit_selection_add(&g_selection, eid);
    edit_undo_clear(&g_undo);

    exec("{\"id\":2,\"cmd\":\"scale\",\"args\":{\"factor\":[2,4,0.5]}}");
    ASSERT(resp_ok());

    const edit_undo_entry_t *entry = edit_undo_peek_undo(&g_undo);
    ASSERT(entry->forward_type == EDIT_CMD_TYPE_SCALE);
    ASSERT(entry->inverse_type == EDIT_CMD_TYPE_SCALE);
    /* Inverse factors: 1/2, 1/4, 1/0.5 */
    ASSERT_NEAR(entry->delta[0], 0.5f,  0.001);
    ASSERT_NEAR(entry->delta[1], 0.25f, 0.001);
    ASSERT_NEAR(entry->delta[2], 2.0f,  0.001);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Main                                                                      */
/* ----------------------------------------------------------------------- */

int main(void) {
    RUN(test_spawn_box);
    RUN(test_spawn_sphere);
    RUN(test_spawn_default_pos);
    RUN(test_spawn_undo_entry);
    RUN(test_delete_selected);
    RUN(test_delete_empty_selection);
    RUN(test_delete_undo_entry);
    RUN(test_move_single);
    RUN(test_move_multi);
    RUN(test_move_undo_entry);
    RUN(test_rotate);
    RUN(test_rotate_undo_entry);
    RUN(test_scale);
    RUN(test_scale_undo_entry);

    printf("\n%d / %d tests passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
