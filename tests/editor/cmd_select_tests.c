/**
 * @file cmd_select_tests.c
 * @brief Tests for select/deselect commands and ID-based command variants.
 *
 * Covers: cmd_select, cmd_deselect, cmd_select_all, cmd_deselect_all,
 *         cmd_delete_id, cmd_move_id.
 */

#include <stdio.h>
#include <string.h>
#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/editor/edit_dispatch.h"
#include "ferrum/editor/edit_undo.h"

/* ── Minimal test harness ─────────────────────────────────────────── */

static int g_pass, g_fail;

#define RUN(fn) do { \
    printf("RUN  %s\n", #fn); fn(); printf("OK   %s\n", #fn); g_pass++; \
} while (0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_fail++; return; \
    } \
} while (0)

/* ── Test fixture ─────────────────────────────────────────────────── */

typedef struct {
    edit_entity_store_t entities;
    edit_selection_t    selection;
    edit_undo_stack_t   undo;
    edit_dispatch_t     dispatch;
    edit_cmd_ctx_t      cmd_ctx;
} test_fixture_t;

static void fixture_init(test_fixture_t *f) {
    memset(f, 0, sizeof(*f));
    edit_entity_store_init(&f->entities, 64);
    edit_selection_init(&f->selection);
    edit_undo_init(&f->undo, 32, 64 * 1024);
    f->cmd_ctx.entities  = &f->entities;
    f->cmd_ctx.selection = &f->selection;
    f->cmd_ctx.undo      = &f->undo;
    edit_dispatch_init(&f->dispatch, 8192, &f->cmd_ctx);
    edit_commands_register_all(&f->dispatch);
}

static void fixture_destroy(test_fixture_t *f) {
    edit_dispatch_destroy(&f->dispatch);
    edit_undo_destroy(&f->undo);
    edit_selection_destroy(&f->selection);
    edit_entity_store_destroy(&f->entities);
}

/* Helper: execute a JSON command string and return success/failure. */
static bool exec_json(test_fixture_t *f, const char *json, char *resp, uint32_t cap) {
    uint32_t n = edit_dispatch_exec(&f->dispatch, json, (uint32_t)strlen(json),
                                    resp, cap);
    return n > 0;
}

/* ── Select command tests ─────────────────────────────────────────── */

static void test_select_by_id(void) {
    test_fixture_t f;
    fixture_init(&f);
    uint32_t eid = edit_entity_store_create(&f.entities, EDIT_ENTITY_TYPE_BOX);
    ASSERT(eid != EDIT_ENTITY_INVALID_ID);

    char resp[256];
    ASSERT(exec_json(&f, "{\"id\":1,\"cmd\":\"select\",\"args\":{\"entity_id\":0}}", resp, sizeof(resp)));
    ASSERT(edit_selection_contains(&f.selection, eid));
    ASSERT(edit_selection_count(&f.selection) == 1);
    fixture_destroy(&f);
}

static void test_select_toggle(void) {
    test_fixture_t f;
    fixture_init(&f);
    uint32_t eid = edit_entity_store_create(&f.entities, EDIT_ENTITY_TYPE_BOX);

    char resp[256];
    /* Select it */
    ASSERT(exec_json(&f, "{\"id\":1,\"cmd\":\"select\",\"args\":{\"entity_id\":0,\"toggle\":true}}", resp, sizeof(resp)));
    ASSERT(edit_selection_contains(&f.selection, eid));
    /* Toggle off */
    ASSERT(exec_json(&f, "{\"id\":2,\"cmd\":\"select\",\"args\":{\"entity_id\":0,\"toggle\":true}}", resp, sizeof(resp)));
    ASSERT(!edit_selection_contains(&f.selection, eid));
    fixture_destroy(&f);
}

static void test_select_invalid_id(void) {
    test_fixture_t f;
    fixture_init(&f);
    char resp[256];
    /* Entity 99 does not exist */
    ASSERT(exec_json(&f, "{\"id\":1,\"cmd\":\"select\",\"args\":{\"entity_id\":99}}", resp, sizeof(resp)));
    /* Should fail — response contains "error" */
    ASSERT(strstr(resp, "handler_failed") != NULL);
    fixture_destroy(&f);
}

static void test_deselect_by_id(void) {
    test_fixture_t f;
    fixture_init(&f);
    uint32_t eid = edit_entity_store_create(&f.entities, EDIT_ENTITY_TYPE_BOX);
    edit_selection_add(&f.selection, eid);

    char resp[256];
    ASSERT(exec_json(&f, "{\"id\":1,\"cmd\":\"deselect\",\"args\":{\"entity_id\":0}}", resp, sizeof(resp)));
    ASSERT(!edit_selection_contains(&f.selection, eid));
    fixture_destroy(&f);
}

static void test_select_all(void) {
    test_fixture_t f;
    fixture_init(&f);
    edit_entity_store_create(&f.entities, EDIT_ENTITY_TYPE_BOX);
    edit_entity_store_create(&f.entities, EDIT_ENTITY_TYPE_SPHERE);
    edit_entity_store_create(&f.entities, EDIT_ENTITY_TYPE_BOX);

    char resp[256];
    ASSERT(exec_json(&f, "{\"id\":1,\"cmd\":\"select_all\"}", resp, sizeof(resp)));
    ASSERT(edit_selection_count(&f.selection) == 3);
    fixture_destroy(&f);
}

static void test_deselect_all(void) {
    test_fixture_t f;
    fixture_init(&f);
    uint32_t e0 = edit_entity_store_create(&f.entities, EDIT_ENTITY_TYPE_BOX);
    uint32_t e1 = edit_entity_store_create(&f.entities, EDIT_ENTITY_TYPE_SPHERE);
    edit_selection_add(&f.selection, e0);
    edit_selection_add(&f.selection, e1);

    char resp[256];
    ASSERT(exec_json(&f, "{\"id\":1,\"cmd\":\"deselect_all\"}", resp, sizeof(resp)));
    ASSERT(edit_selection_count(&f.selection) == 0);
    fixture_destroy(&f);
}

/* ── ID-based delete test ─────────────────────────────────────────── */

static void test_delete_id(void) {
    test_fixture_t f;
    fixture_init(&f);
    uint32_t e0 = edit_entity_store_create(&f.entities, EDIT_ENTITY_TYPE_BOX);
    uint32_t e1 = edit_entity_store_create(&f.entities, EDIT_ENTITY_TYPE_BOX);
    (void)e1;

    char resp[256];
    ASSERT(exec_json(&f, "{\"id\":1,\"cmd\":\"delete_id\",\"args\":{\"entity_id\":0}}", resp, sizeof(resp)));
    ASSERT(strstr(resp, "\"ok\":true") != NULL);
    /* Entity 0 should be gone. */
    ASSERT(edit_entity_store_get(&f.entities, e0) == NULL);
    /* Entity 1 should remain. */
    ASSERT(edit_entity_store_get(&f.entities, e1) != NULL);
    fixture_destroy(&f);
}

static void test_delete_id_invalid(void) {
    test_fixture_t f;
    fixture_init(&f);
    char resp[256];
    ASSERT(exec_json(&f, "{\"id\":1,\"cmd\":\"delete_id\",\"args\":{\"entity_id\":99}}", resp, sizeof(resp)));
    ASSERT(strstr(resp, "handler_failed") != NULL);
    fixture_destroy(&f);
}

/* ── ID-based move test ───────────────────────────────────────────── */

static void test_move_id(void) {
    test_fixture_t f;
    fixture_init(&f);
    uint32_t eid = edit_entity_store_create(&f.entities, EDIT_ENTITY_TYPE_BOX);

    char resp[256];
    ASSERT(exec_json(&f,
        "{\"id\":1,\"cmd\":\"move_id\",\"args\":{\"entity_id\":0,\"delta\":[1.0,2.0,3.0]}}",
        resp, sizeof(resp)));
    ASSERT(strstr(resp, "\"ok\":true") != NULL);

    const edit_entity_t *e = edit_entity_store_get(&f.entities, eid);
    ASSERT(e != NULL);
    ASSERT(e->pos[0] > 0.9f && e->pos[0] < 1.1f);
    ASSERT(e->pos[1] > 1.9f && e->pos[1] < 2.1f);
    ASSERT(e->pos[2] > 2.9f && e->pos[2] < 3.1f);
    fixture_destroy(&f);
}

static void test_move_id_invalid(void) {
    test_fixture_t f;
    fixture_init(&f);
    char resp[256];
    ASSERT(exec_json(&f,
        "{\"id\":1,\"cmd\":\"move_id\",\"args\":{\"entity_id\":99,\"delta\":[1,0,0]}}",
        resp, sizeof(resp)));
    ASSERT(strstr(resp, "handler_failed") != NULL);
    fixture_destroy(&f);
}

/* ── Main ─────────────────────────────────────────────────────────── */

int main(void) {
    RUN(test_select_by_id);
    RUN(test_select_toggle);
    RUN(test_select_invalid_id);
    RUN(test_deselect_by_id);
    RUN(test_select_all);
    RUN(test_deselect_all);
    RUN(test_delete_id);
    RUN(test_delete_id_invalid);
    RUN(test_move_id);
    RUN(test_move_id_invalid);

    printf("\n%d / %d tests passed\n", g_pass, g_pass + g_fail);
    return g_fail > 0 ? 1 : 0;
}
