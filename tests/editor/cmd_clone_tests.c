/**
 * @file cmd_clone_tests.c
 * @brief Tests for cmd_clone — entity duplication.
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

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
}

static void teardown_(void) {
    edit_dispatch_destroy(&g_dispatch);
    edit_selection_destroy(&g_selection);
    edit_undo_destroy(&g_undo);
    edit_entity_store_destroy(&g_entities);
}

/* ── Tests ────────────────────────────────────────────────────────── */

/** Test: clone a single selected entity. */
static void test_clone_single(void) {
    setup_();
    /* Create and select an entity. */
    uint32_t id = edit_entity_store_create(&g_entities, EDIT_ENTITY_TYPE_BOX);
    edit_entity_t *ent = edit_entity_store_get_mut(&g_entities, id);
    ent->pos[0] = 5.0f; ent->pos[1] = 10.0f; ent->pos[2] = 15.0f;
    ent->scale[0] = 2.0f; ent->scale[1] = 2.0f; ent->scale[2] = 2.0f;
    edit_selection_add(&g_selection, id);

    ASSERT(exec_("{\"id\":1,\"cmd\":\"clone\",\"args\":{}}") > 0);
    ASSERT(resp_ok_());

    /* Should now have 2 entities. */
    ASSERT(edit_entity_store_count(&g_entities) == 2);

    /* Clone should be at same position (no offset). */
    const edit_entity_t *clone = edit_entity_store_get(&g_entities, 1);
    ASSERT(clone != NULL);
    ASSERT(fabsf(clone->pos[0] - 5.0f) < 0.001f);
    ASSERT(fabsf(clone->pos[1] - 10.0f) < 0.001f);
    ASSERT(fabsf(clone->scale[0] - 2.0f) < 0.001f);

    /* Selection should now contain only the clone. */
    ASSERT(edit_selection_count(&g_selection) == 1);
    ASSERT(edit_selection_contains(&g_selection, 1));
    ASSERT(!edit_selection_contains(&g_selection, 0));
    teardown_();
}

/** Test: clone with offset. */
static void test_clone_with_offset(void) {
    setup_();
    uint32_t id = edit_entity_store_create(&g_entities, EDIT_ENTITY_TYPE_SPHERE);
    edit_entity_t *ent = edit_entity_store_get_mut(&g_entities, id);
    ent->pos[0] = 1.0f; ent->pos[1] = 2.0f; ent->pos[2] = 3.0f;
    edit_selection_add(&g_selection, id);

    ASSERT(exec_("{\"id\":1,\"cmd\":\"clone\","
                  "\"args\":{\"offset\":[10,20,30]}}") > 0);
    ASSERT(resp_ok_());

    const edit_entity_t *clone = edit_entity_store_get(&g_entities, 1);
    ASSERT(clone != NULL);
    ASSERT(fabsf(clone->pos[0] - 11.0f) < 0.001f);
    ASSERT(fabsf(clone->pos[1] - 22.0f) < 0.001f);
    ASSERT(fabsf(clone->pos[2] - 33.0f) < 0.001f);
    teardown_();
}

/** Test: clone multiple selected entities. */
static void test_clone_multiple(void) {
    setup_();
    uint32_t id0 = edit_entity_store_create(&g_entities, EDIT_ENTITY_TYPE_BOX);
    uint32_t id1 = edit_entity_store_create(&g_entities, EDIT_ENTITY_TYPE_SPHERE);
    edit_selection_add(&g_selection, id0);
    edit_selection_add(&g_selection, id1);

    ASSERT(exec_("{\"id\":1,\"cmd\":\"clone\",\"args\":{}}") > 0);
    ASSERT(resp_ok_());

    /* Should have 4 entities total. */
    ASSERT(edit_entity_store_count(&g_entities) == 4);

    /* Selection should contain only clones. */
    ASSERT(edit_selection_count(&g_selection) == 2);
    ASSERT(!edit_selection_contains(&g_selection, id0));
    ASSERT(!edit_selection_contains(&g_selection, id1));
    teardown_();
}

/** Test: clone with empty selection fails. */
static void test_clone_empty_selection(void) {
    setup_();
    ASSERT(exec_("{\"id\":1,\"cmd\":\"clone\",\"args\":{}}") > 0);
    ASSERT(strstr(g_resp, "\"ok\":false") != NULL);
    teardown_();
}

/** Test: clone preserves materials. */
static void test_clone_preserves_materials(void) {
    setup_();
    uint32_t id = edit_entity_store_create(&g_entities, EDIT_ENTITY_TYPE_BOX);
    edit_entity_t *ent = edit_entity_store_get_mut(&g_entities, id);
    strncpy(ent->materials[EDIT_MATERIAL_SLOT_ALBEDO], "textures/brick.png",
            EDIT_MATERIAL_PATH_MAX - 1);
    edit_selection_add(&g_selection, id);

    ASSERT(exec_("{\"id\":1,\"cmd\":\"clone\",\"args\":{}}") > 0);
    ASSERT(resp_ok_());

    const edit_entity_t *clone = edit_entity_store_get(&g_entities, 1);
    ASSERT(clone != NULL);
    ASSERT(strcmp(clone->materials[EDIT_MATERIAL_SLOT_ALBEDO],
                 "textures/brick.png") == 0);
    teardown_();
}

/** Test: clone preserves entity type. */
static void test_clone_preserves_type(void) {
    setup_();
    uint32_t id = edit_entity_store_create(&g_entities, EDIT_ENTITY_TYPE_CAPSULE);
    edit_selection_add(&g_selection, id);

    ASSERT(exec_("{\"id\":1,\"cmd\":\"clone\",\"args\":{}}") > 0);
    ASSERT(resp_ok_());

    const edit_entity_t *clone = edit_entity_store_get(&g_entities, 1);
    ASSERT(clone != NULL);
    ASSERT(clone->type == EDIT_ENTITY_TYPE_CAPSULE);
    teardown_();
}

int main(void) {
    RUN(test_clone_single);
    RUN(test_clone_with_offset);
    RUN(test_clone_multiple);
    RUN(test_clone_empty_selection);
    RUN(test_clone_preserves_materials);
    RUN(test_clone_preserves_type);
    printf("\n%d / %d tests passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
