/**
 * @file cmd_material_tests.c
 * @brief Tests for cmd_material — material assignment and query.
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_dispatch.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/editor/edit_undo.h"

/* ── Minimal test harness ────────────────────────────────────────── */
static int g_pass, g_fail;
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  ASSERT FAILED: %s (line %d)\n", #cond, __LINE__); \
        g_fail++; return; \
    } \
} while (0)
#define RUN(fn) do { \
    int prev = g_fail; \
    printf("RUN  %s\n", #fn); fn(); \
    if (g_fail == prev) { g_pass++; printf("OK   %s\n", #fn); } \
    else printf("FAIL %s\n", #fn); \
} while(0)

/* ── Shared state ─────────────────────────────────────────────────── */
static edit_dispatch_t      g_dispatch;
static edit_entity_store_t  g_entities;
static edit_selection_t     g_selection;
static edit_undo_stack_t    g_undo;
static edit_cmd_ctx_t       g_ctx;
static char                 g_resp[4096];

static int exec_(const char *json) {
    return edit_dispatch_exec(&g_dispatch, json, (uint32_t)strlen(json),
                              g_resp, sizeof(g_resp));
}

static bool resp_ok_(void) {
    return strstr(g_resp, "\"ok\":true") != NULL;
}

static void setup_(void) {
    memset(&g_ctx, 0, sizeof(g_ctx));
    edit_dispatch_init(&g_dispatch, 16384, &g_ctx);
    edit_entity_store_init(&g_entities, 64);
    edit_selection_init(&g_selection);
    edit_undo_init(&g_undo, 64, 4096);

    g_ctx.entities  = &g_entities;
    g_ctx.selection = &g_selection;
    g_ctx.undo      = &g_undo;

    edit_commands_register_all(&g_dispatch);

    /* Create a test entity. */
    edit_entity_store_create(&g_entities, EDIT_ENTITY_TYPE_BOX);
}

static void teardown_(void) {
    edit_dispatch_destroy(&g_dispatch);
    edit_selection_destroy(&g_selection);
    edit_undo_destroy(&g_undo);
    edit_entity_store_destroy(&g_entities);
}

/* ── Tests ────────────────────────────────────────────────────────── */

/** Test: set albedo material on entity 0. */
static void test_material_set(void) {
    setup_();
    ASSERT(exec_("{\"id\":1,\"cmd\":\"material\","
                  "\"args\":{\"sub\":\"set\",\"entity\":0,"
                  "\"slot\":\"albedo\",\"path\":\"textures/brick.png\"}}") > 0);
    ASSERT(resp_ok_());

    /* Verify entity has material set. */
    const edit_entity_t *ent = edit_entity_store_get(&g_entities, 0);
    ASSERT(ent != NULL);
    ASSERT(strcmp(ent->materials[EDIT_MATERIAL_SLOT_ALBEDO], "textures/brick.png") == 0);
    teardown_();
}

/** Test: get materials for entity with no assignments returns null. */
static void test_material_get_empty(void) {
    setup_();
    ASSERT(exec_("{\"id\":1,\"cmd\":\"material\","
                  "\"args\":{\"sub\":\"get\",\"entity\":0}}") > 0);
    ASSERT(resp_ok_());
    /* Result should be null. */
    ASSERT(strstr(g_resp, "\"result\":null") != NULL);
    teardown_();
}

/** Test: get materials after set returns assigned slots. */
static void test_material_get_after_set(void) {
    setup_();
    /* Set albedo. */
    ASSERT(exec_("{\"id\":1,\"cmd\":\"material\","
                  "\"args\":{\"sub\":\"set\",\"entity\":0,"
                  "\"slot\":\"albedo\",\"path\":\"textures/brick.png\"}}") > 0);
    ASSERT(resp_ok_());

    /* Set normal. */
    ASSERT(exec_("{\"id\":2,\"cmd\":\"material\","
                  "\"args\":{\"sub\":\"set\",\"entity\":0,"
                  "\"slot\":\"normal\",\"path\":\"textures/brick_n.png\"}}") > 0);
    ASSERT(resp_ok_());

    /* Get. */
    ASSERT(exec_("{\"id\":3,\"cmd\":\"material\","
                  "\"args\":{\"sub\":\"get\",\"entity\":0}}") > 0);
    ASSERT(resp_ok_());
    ASSERT(strstr(g_resp, "albedo") != NULL);
    ASSERT(strstr(g_resp, "brick.png") != NULL);
    ASSERT(strstr(g_resp, "normal") != NULL);
    ASSERT(strstr(g_resp, "brick_n.png") != NULL);
    teardown_();
}

/** Test: set material on invalid entity fails. */
static void test_material_invalid_entity(void) {
    setup_();
    ASSERT(exec_("{\"id\":1,\"cmd\":\"material\","
                  "\"args\":{\"sub\":\"set\",\"entity\":999,"
                  "\"slot\":\"albedo\",\"path\":\"textures/brick.png\"}}") > 0);
    ASSERT(strstr(g_resp, "\"ok\":false") != NULL);
    teardown_();
}

/** Test: set material with invalid slot name fails. */
static void test_material_invalid_slot(void) {
    setup_();
    ASSERT(exec_("{\"id\":1,\"cmd\":\"material\","
                  "\"args\":{\"sub\":\"set\",\"entity\":0,"
                  "\"slot\":\"diffuse\",\"path\":\"textures/brick.png\"}}") > 0);
    ASSERT(strstr(g_resp, "\"ok\":false") != NULL);
    teardown_();
}

/** Test: material set records undo. */
static void test_material_undo(void) {
    setup_();
    ASSERT(exec_("{\"id\":1,\"cmd\":\"material\","
                  "\"args\":{\"sub\":\"set\",\"entity\":0,"
                  "\"slot\":\"albedo\",\"path\":\"textures/brick.png\"}}") > 0);
    ASSERT(resp_ok_());

    /* Verify undo stack has a record. */
    ASSERT(edit_undo_peek_undo(&g_undo) != NULL);
    teardown_();
}

/** Test: set all 5 material slots. */
static void test_material_all_slots(void) {
    setup_();
    const char *cmds[] = {
        "{\"id\":1,\"cmd\":\"material\",\"args\":{\"sub\":\"set\",\"entity\":0,\"slot\":\"albedo\",\"path\":\"a.png\"}}",
        "{\"id\":2,\"cmd\":\"material\",\"args\":{\"sub\":\"set\",\"entity\":0,\"slot\":\"normal\",\"path\":\"n.png\"}}",
        "{\"id\":3,\"cmd\":\"material\",\"args\":{\"sub\":\"set\",\"entity\":0,\"slot\":\"roughness\",\"path\":\"r.png\"}}",
        "{\"id\":4,\"cmd\":\"material\",\"args\":{\"sub\":\"set\",\"entity\":0,\"slot\":\"metallic\",\"path\":\"m.png\"}}",
        "{\"id\":5,\"cmd\":\"material\",\"args\":{\"sub\":\"set\",\"entity\":0,\"slot\":\"emissive\",\"path\":\"e.png\"}}",
    };
    for (int i = 0; i < 5; i++) {
        ASSERT(exec_(cmds[i]) > 0);
        ASSERT(resp_ok_());
    }

    const edit_entity_t *ent = edit_entity_store_get(&g_entities, 0);
    ASSERT(strcmp(ent->materials[0], "a.png") == 0);
    ASSERT(strcmp(ent->materials[1], "n.png") == 0);
    ASSERT(strcmp(ent->materials[2], "r.png") == 0);
    ASSERT(strcmp(ent->materials[3], "m.png") == 0);
    ASSERT(strcmp(ent->materials[4], "e.png") == 0);
    teardown_();
}

/** Test: material set with empty path clears the slot. */
static void test_material_clear_slot(void) {
    setup_();
    /* Set albedo. */
    ASSERT(exec_("{\"id\":1,\"cmd\":\"material\","
                  "\"args\":{\"sub\":\"set\",\"entity\":0,"
                  "\"slot\":\"albedo\",\"path\":\"textures/brick.png\"}}") > 0);
    ASSERT(resp_ok_());
    /* Clear it. */
    ASSERT(exec_("{\"id\":2,\"cmd\":\"material\","
                  "\"args\":{\"sub\":\"set\",\"entity\":0,"
                  "\"slot\":\"albedo\",\"path\":\"\"}}") > 0);
    ASSERT(resp_ok_());

    const edit_entity_t *ent = edit_entity_store_get(&g_entities, 0);
    ASSERT(ent->materials[EDIT_MATERIAL_SLOT_ALBEDO][0] == '\0');
    teardown_();
}

int main(void) {
    RUN(test_material_set);
    RUN(test_material_get_empty);
    RUN(test_material_get_after_set);
    RUN(test_material_invalid_entity);
    RUN(test_material_invalid_slot);
    RUN(test_material_undo);
    RUN(test_material_all_slots);
    RUN(test_material_clear_slot);
    printf("\n%d / %d tests passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
