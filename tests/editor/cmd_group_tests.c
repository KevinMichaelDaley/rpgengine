/**
 * @file cmd_group_tests.c
 * @brief Tests for selection group commands and &-group select/deselect.
 *
 * Tests:
 *  1. group_save creates a group from current selection
 *  2. group_save requires & prefix
 *  3. group_save with empty selection fails
 *  4. group_delete removes a group
 *  5. group_delete on nonexistent group fails
 *  6. group_list returns all groups
 *  7. select &group selects all entities in the group
 *  8. deselect &group deselects all entities in the group
 *  9. group_save overwrites existing group
 * 10. group persists after deselecting entities
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

/* ----------------------------------------------------------------------- */
/* Test context                                                              */
/* ----------------------------------------------------------------------- */

static edit_dispatch_t       g_dispatch;
static edit_entity_store_t   g_entities;
static edit_selection_t      g_selection;
static edit_undo_stack_t     g_undo;
static edit_cmd_ctx_t        g_ctx;
static char g_resp[8192];

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

static uint32_t exec(const char *json) {
    memset(g_resp, 0, sizeof(g_resp));
    return edit_dispatch_exec(&g_dispatch, json, (uint32_t)strlen(json),
                              g_resp, sizeof(g_resp));
}

static bool resp_ok(void) {
    return strstr(g_resp, "\"ok\":true") != NULL;
}

static bool resp_fail(void) {
    return strstr(g_resp, "\"ok\":false") != NULL;
}

static void spawn_named(const char *type, const char *name,
                        float x, float y, float z) {
    char json[512];
    snprintf(json, sizeof(json),
             "{\"id\":99,\"cmd\":\"spawn\",\"args\":{\"type\":\"%s\","
             "\"pos\":[%.1f,%.1f,%.1f],\"name\":\"%s\"}}",
             type, (double)x, (double)y, (double)z, name);
    exec(json);
}

/* ----------------------------------------------------------------------- */
/* Tests                                                                     */
/* ----------------------------------------------------------------------- */

/** 1. group_save creates a group from current selection. */
static bool test_group_save_basic(void) {
    spawn_named("box", "A", 0, 0, 0);
    spawn_named("box", "B", 1, 0, 0);

    uint32_t a = edit_entity_store_find_by_name(&g_entities, "A");
    uint32_t b = edit_entity_store_find_by_name(&g_entities, "B");
    edit_selection_add(&g_selection, a);
    edit_selection_add(&g_selection, b);

    exec("{\"id\":1,\"cmd\":\"group_save\",\"args\":{\"name\":\"&walls\"}}");
    ASSERT(resp_ok());

    /* Verify group exists via group_list. */
    exec("{\"id\":2,\"cmd\":\"group_list\",\"args\":{}}");
    ASSERT(resp_ok());
    ASSERT(strstr(g_resp, "&walls") != NULL);
    return true;
}

/** 2. group_save requires & prefix. */
static bool test_group_save_requires_prefix(void) {
    spawn_named("box", "X", 0, 0, 0);
    uint32_t x = edit_entity_store_find_by_name(&g_entities, "X");
    edit_selection_add(&g_selection, x);

    exec("{\"id\":1,\"cmd\":\"group_save\",\"args\":{\"name\":\"walls\"}}");
    ASSERT(resp_fail());
    return true;
}

/** 3. group_save with empty selection fails. */
static bool test_group_save_empty_selection(void) {
    exec("{\"id\":1,\"cmd\":\"group_save\",\"args\":{\"name\":\"&empty\"}}");
    ASSERT(resp_fail());
    return true;
}

/** 4. group_delete removes a group. */
static bool test_group_delete(void) {
    spawn_named("box", "A", 0, 0, 0);
    uint32_t a = edit_entity_store_find_by_name(&g_entities, "A");
    edit_selection_add(&g_selection, a);

    exec("{\"id\":1,\"cmd\":\"group_save\",\"args\":{\"name\":\"&temp\"}}");
    ASSERT(resp_ok());

    exec("{\"id\":2,\"cmd\":\"group_delete\",\"args\":{\"name\":\"&temp\"}}");
    ASSERT(resp_ok());

    /* Verify gone from group_list. */
    exec("{\"id\":3,\"cmd\":\"group_list\",\"args\":{}}");
    ASSERT(strstr(g_resp, "&temp") == NULL);
    return true;
}

/** 5. group_delete on nonexistent group fails. */
static bool test_group_delete_nonexistent(void) {
    exec("{\"id\":1,\"cmd\":\"group_delete\",\"args\":{\"name\":\"&nope\"}}");
    ASSERT(resp_fail());
    return true;
}

/** 6. group_list returns all groups. */
static bool test_group_list(void) {
    spawn_named("box", "A", 0, 0, 0);
    spawn_named("box", "B", 1, 0, 0);

    uint32_t a = edit_entity_store_find_by_name(&g_entities, "A");
    uint32_t b = edit_entity_store_find_by_name(&g_entities, "B");

    edit_selection_add(&g_selection, a);
    exec("{\"id\":1,\"cmd\":\"group_save\",\"args\":{\"name\":\"&alpha\"}}");

    edit_selection_clear(&g_selection);
    edit_selection_add(&g_selection, b);
    exec("{\"id\":2,\"cmd\":\"group_save\",\"args\":{\"name\":\"&beta\"}}");

    exec("{\"id\":3,\"cmd\":\"group_list\",\"args\":{}}");
    ASSERT(resp_ok());
    ASSERT(strstr(g_resp, "&alpha") != NULL);
    ASSERT(strstr(g_resp, "&beta") != NULL);
    return true;
}

/** 7. select &group selects all entities in the group. */
static bool test_select_group(void) {
    spawn_named("box", "A", 0, 0, 0);
    spawn_named("box", "B", 1, 0, 0);
    spawn_named("box", "C", 2, 0, 0);

    uint32_t a = edit_entity_store_find_by_name(&g_entities, "A");
    uint32_t b = edit_entity_store_find_by_name(&g_entities, "B");
    uint32_t c = edit_entity_store_find_by_name(&g_entities, "C");

    /* Save A+B as &duo. */
    edit_selection_add(&g_selection, a);
    edit_selection_add(&g_selection, b);
    exec("{\"id\":1,\"cmd\":\"group_save\",\"args\":{\"name\":\"&duo\"}}");

    /* Clear selection, then select &duo. */
    edit_selection_clear(&g_selection);
    exec("{\"id\":2,\"cmd\":\"select\",\"args\":{\"entity_id\":\"&duo\"}}");
    ASSERT(resp_ok());

    ASSERT(edit_selection_contains(&g_selection, a));
    ASSERT(edit_selection_contains(&g_selection, b));
    ASSERT(!edit_selection_contains(&g_selection, c));
    return true;
}

/** 8. deselect &group deselects all entities in the group. */
static bool test_deselect_group(void) {
    spawn_named("box", "A", 0, 0, 0);
    spawn_named("box", "B", 1, 0, 0);
    spawn_named("box", "C", 2, 0, 0);

    uint32_t a = edit_entity_store_find_by_name(&g_entities, "A");
    uint32_t b = edit_entity_store_find_by_name(&g_entities, "B");
    uint32_t c = edit_entity_store_find_by_name(&g_entities, "C");

    /* Save A+B as &duo, then select all three. */
    edit_selection_add(&g_selection, a);
    edit_selection_add(&g_selection, b);
    exec("{\"id\":1,\"cmd\":\"group_save\",\"args\":{\"name\":\"&duo\"}}");

    edit_selection_add(&g_selection, c);
    ASSERT(edit_selection_count(&g_selection) == 3);

    /* Deselect &duo → only C remains. */
    exec("{\"id\":2,\"cmd\":\"deselect\",\"args\":{\"entity_id\":\"&duo\"}}");
    ASSERT(resp_ok());

    ASSERT(!edit_selection_contains(&g_selection, a));
    ASSERT(!edit_selection_contains(&g_selection, b));
    ASSERT(edit_selection_contains(&g_selection, c));
    return true;
}

/** 9. group_save overwrites existing group with new selection. */
static bool test_group_save_overwrite(void) {
    spawn_named("box", "A", 0, 0, 0);
    spawn_named("box", "B", 1, 0, 0);
    spawn_named("box", "C", 2, 0, 0);

    uint32_t a = edit_entity_store_find_by_name(&g_entities, "A");
    uint32_t b = edit_entity_store_find_by_name(&g_entities, "B");
    uint32_t c = edit_entity_store_find_by_name(&g_entities, "C");

    /* Save A as &grp. */
    edit_selection_add(&g_selection, a);
    exec("{\"id\":1,\"cmd\":\"group_save\",\"args\":{\"name\":\"&grp\"}}");

    /* Overwrite &grp with B+C. */
    edit_selection_clear(&g_selection);
    edit_selection_add(&g_selection, b);
    edit_selection_add(&g_selection, c);
    exec("{\"id\":2,\"cmd\":\"group_save\",\"args\":{\"name\":\"&grp\"}}");

    /* Select &grp → should get B+C, not A. */
    edit_selection_clear(&g_selection);
    exec("{\"id\":3,\"cmd\":\"select\",\"args\":{\"entity_id\":\"&grp\"}}");

    ASSERT(!edit_selection_contains(&g_selection, a));
    ASSERT(edit_selection_contains(&g_selection, b));
    ASSERT(edit_selection_contains(&g_selection, c));
    return true;
}

/** 10. group persists after deselecting and re-selecting entities. */
static bool test_group_persists(void) {
    spawn_named("box", "A", 0, 0, 0);
    spawn_named("box", "B", 1, 0, 0);

    uint32_t a = edit_entity_store_find_by_name(&g_entities, "A");
    uint32_t b = edit_entity_store_find_by_name(&g_entities, "B");

    /* Save A+B as &pair. */
    edit_selection_add(&g_selection, a);
    edit_selection_add(&g_selection, b);
    exec("{\"id\":1,\"cmd\":\"group_save\",\"args\":{\"name\":\"&pair\"}}");

    /* Clear selection entirely. */
    edit_selection_clear(&g_selection);
    ASSERT(edit_selection_count(&g_selection) == 0);

    /* Select &pair → both back. */
    exec("{\"id\":2,\"cmd\":\"select\",\"args\":{\"entity_id\":\"&pair\"}}");
    ASSERT(edit_selection_contains(&g_selection, a));
    ASSERT(edit_selection_contains(&g_selection, b));
    return true;
}

/* ----------------------------------------------------------------------- */
/* Main                                                                      */
/* ----------------------------------------------------------------------- */

int main(void) {
    RUN(test_group_save_basic);
    RUN(test_group_save_requires_prefix);
    RUN(test_group_save_empty_selection);
    RUN(test_group_delete);
    RUN(test_group_delete_nonexistent);
    RUN(test_group_list);
    RUN(test_select_group);
    RUN(test_deselect_group);
    RUN(test_group_save_overwrite);
    RUN(test_group_persists);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
