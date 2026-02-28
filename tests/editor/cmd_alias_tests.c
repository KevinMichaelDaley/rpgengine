/**
 * @file cmd_alias_tests.c
 * @brief Tests for @alias system: create, delete, list, and cursor_snap
 *        with orientation support.
 *
 * Tests:
 *  1. alias_create makes a marker entity with @ name and position
 *  2. alias_create with rotation stores both pos and rot
 *  3. alias_create without @ prefix fails
 *  4. alias_create defaults pos to @cursor when omitted
 *  5. alias_delete removes an alias entity
 *  6. alias_delete of non-existent alias fails
 *  7. alias_list returns all @ entities
 *  8. alias_list with pattern filters by regex
 *  9. cursor_snap to alias copies both position AND rotation
 * 10. select_near skips all @ entities (not just @cursor)
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

/** 1. alias_create makes a marker entity with @ name and position. */
static bool test_alias_create(void) {
    uint32_t n = exec(
        "{\"id\":1,\"cmd\":\"alias_create\",\"args\":"
        "{\"name\":\"@spawn_point\",\"pos\":[10,20,30]}}");
    ASSERT(n > 0);
    ASSERT(resp_ok());

    uint32_t eid = edit_entity_store_find_by_name(&g_entities, "@spawn_point");
    ASSERT(eid != EDIT_ENTITY_INVALID_ID);
    const edit_entity_t *e = edit_entity_store_get(&g_entities, eid);
    ASSERT(e);
    ASSERT(e->type == EDIT_ENTITY_TYPE_MARKER);
    ASSERT_NEAR(e->pos[0], 10.0f, 0.01);
    ASSERT_NEAR(e->pos[1], 20.0f, 0.01);
    ASSERT_NEAR(e->pos[2], 30.0f, 0.01);
    return true;
}

/** 2. alias_create with rotation stores both pos and rot. */
static bool test_alias_create_with_rot(void) {
    uint32_t n = exec(
        "{\"id\":2,\"cmd\":\"alias_create\",\"args\":"
        "{\"name\":\"@lookout\",\"pos\":[5,5,5],\"rot\":[45,90,0]}}");
    ASSERT(n > 0);
    ASSERT(resp_ok());

    uint32_t eid = edit_entity_store_find_by_name(&g_entities, "@lookout");
    const edit_entity_t *e = edit_entity_store_get(&g_entities, eid);
    ASSERT(e);
    ASSERT_NEAR(e->rot[0], 45.0f, 0.01);
    ASSERT_NEAR(e->rot[1], 90.0f, 0.01);
    ASSERT_NEAR(e->rot[2], 0.0f, 0.01);
    return true;
}

/** 3. alias_create without @ prefix fails. */
static bool test_alias_create_no_at(void) {
    uint32_t n = exec(
        "{\"id\":3,\"cmd\":\"alias_create\",\"args\":"
        "{\"name\":\"bad_name\",\"pos\":[0,0,0]}}");
    ASSERT(n > 0);
    ASSERT(!resp_ok());
    return true;
}

/** 4. alias_create defaults pos to @cursor when omitted. */
static bool test_alias_create_cursor_default(void) {
    spawn_named("box", "@cursor", 100, 200, 300);

    uint32_t n = exec(
        "{\"id\":4,\"cmd\":\"alias_create\",\"args\":"
        "{\"name\":\"@here\"}}");
    ASSERT(n > 0);
    ASSERT(resp_ok());

    uint32_t eid = edit_entity_store_find_by_name(&g_entities, "@here");
    const edit_entity_t *e = edit_entity_store_get(&g_entities, eid);
    ASSERT(e);
    ASSERT_NEAR(e->pos[0], 100.0f, 0.01);
    ASSERT_NEAR(e->pos[1], 200.0f, 0.01);
    ASSERT_NEAR(e->pos[2], 300.0f, 0.01);
    return true;
}

/** 5. alias_delete removes an alias entity. */
static bool test_alias_delete(void) {
    exec("{\"id\":1,\"cmd\":\"alias_create\",\"args\":"
         "{\"name\":\"@temp\",\"pos\":[0,0,0]}}");
    uint32_t eid = edit_entity_store_find_by_name(&g_entities, "@temp");
    ASSERT(eid != EDIT_ENTITY_INVALID_ID);

    uint32_t n = exec(
        "{\"id\":2,\"cmd\":\"alias_delete\",\"args\":"
        "{\"name\":\"@temp\"}}");
    ASSERT(n > 0);
    ASSERT(resp_ok());

    eid = edit_entity_store_find_by_name(&g_entities, "@temp");
    ASSERT(eid == EDIT_ENTITY_INVALID_ID);
    return true;
}

/** 6. alias_delete of non-existent alias fails. */
static bool test_alias_delete_nonexistent(void) {
    uint32_t n = exec(
        "{\"id\":1,\"cmd\":\"alias_delete\",\"args\":"
        "{\"name\":\"@ghost\"}}");
    ASSERT(n > 0);
    ASSERT(!resp_ok());
    return true;
}

/** 7. alias_list returns all @ entities. */
static bool test_alias_list(void) {
    spawn_named("box", "@cursor", 0, 0, 0);
    exec("{\"id\":1,\"cmd\":\"alias_create\",\"args\":"
         "{\"name\":\"@mark1\",\"pos\":[1,0,0]}}");
    exec("{\"id\":2,\"cmd\":\"alias_create\",\"args\":"
         "{\"name\":\"@mark2\",\"pos\":[2,0,0]}}");
    /* Also a normal entity that should NOT appear. */
    spawn_named("box", "normal_box", 5, 0, 0);

    uint32_t n = exec(
        "{\"id\":3,\"cmd\":\"alias_list\",\"args\":{}}");
    ASSERT(n > 0);
    ASSERT(resp_ok());
    /* Response should mention @cursor, @mark1, @mark2. */
    ASSERT(strstr(g_resp, "@cursor") != NULL);
    ASSERT(strstr(g_resp, "@mark1") != NULL);
    ASSERT(strstr(g_resp, "@mark2") != NULL);
    /* Should NOT mention normal_box. */
    ASSERT(strstr(g_resp, "normal_box") == NULL);
    return true;
}

/** 8. alias_list with pattern filters by regex. */
static bool test_alias_list_pattern(void) {
    exec("{\"id\":1,\"cmd\":\"alias_create\",\"args\":"
         "{\"name\":\"@spawn_a\",\"pos\":[0,0,0]}}");
    exec("{\"id\":2,\"cmd\":\"alias_create\",\"args\":"
         "{\"name\":\"@spawn_b\",\"pos\":[0,0,0]}}");
    exec("{\"id\":3,\"cmd\":\"alias_create\",\"args\":"
         "{\"name\":\"@look\",\"pos\":[0,0,0]}}");

    uint32_t n = exec(
        "{\"id\":4,\"cmd\":\"alias_list\",\"args\":"
        "{\"pattern\":\"spawn\"}}");
    ASSERT(n > 0);
    ASSERT(resp_ok());
    ASSERT(strstr(g_resp, "@spawn_a") != NULL);
    ASSERT(strstr(g_resp, "@spawn_b") != NULL);
    ASSERT(strstr(g_resp, "@look") == NULL);
    return true;
}

/** 9. cursor_snap to alias copies both position AND rotation. */
static bool test_cursor_snap_copies_rot(void) {
    spawn_named("box", "@cursor", 0, 0, 0);

    /* Create alias with specific rotation. */
    exec("{\"id\":1,\"cmd\":\"alias_create\",\"args\":"
         "{\"name\":\"@viewpoint\",\"pos\":[10,20,30],\"rot\":[45,90,135]}}");

    uint32_t n = exec(
        "{\"id\":2,\"cmd\":\"cursor_snap\",\"args\":"
        "{\"entity_id\":\"@viewpoint\"}}");
    ASSERT(n > 0);
    ASSERT(resp_ok());

    uint32_t cid = edit_entity_store_find_by_name(&g_entities, "@cursor");
    const edit_entity_t *cur = edit_entity_store_get(&g_entities, cid);
    ASSERT(cur);
    ASSERT_NEAR(cur->pos[0], 10.0f, 0.01);
    ASSERT_NEAR(cur->pos[1], 20.0f, 0.01);
    ASSERT_NEAR(cur->pos[2], 30.0f, 0.01);
    ASSERT_NEAR(cur->rot[0], 45.0f, 0.01);
    ASSERT_NEAR(cur->rot[1], 90.0f, 0.01);
    ASSERT_NEAR(cur->rot[2], 135.0f, 0.01);
    return true;
}

/** 10. select_near skips ALL @ entities, not just @cursor. */
static bool test_select_near_skips_aliases(void) {
    spawn_named("box", "@cursor", 0, 0, 0);
    exec("{\"id\":1,\"cmd\":\"alias_create\",\"args\":"
         "{\"name\":\"@mark\",\"pos\":[1,0,0]}}");
    spawn_named("box", "real_box", 2, 0, 0);

    uint32_t n = exec(
        "{\"id\":2,\"cmd\":\"select_near\",\"args\":"
        "{\"pos\":[0,0,0],\"dist\":100}}");
    ASSERT(n > 0);
    ASSERT(resp_ok());

    /* Only real_box should be selected, not @cursor or @mark. */
    uint32_t rb = edit_entity_store_find_by_name(&g_entities, "real_box");
    ASSERT(edit_selection_contains(&g_selection, rb));

    uint32_t mk = edit_entity_store_find_by_name(&g_entities, "@mark");
    ASSERT(!edit_selection_contains(&g_selection, mk));

    uint32_t cu = edit_entity_store_find_by_name(&g_entities, "@cursor");
    ASSERT(!edit_selection_contains(&g_selection, cu));
    return true;
}

/* ----------------------------------------------------------------------- */
/* Main                                                                      */
/* ----------------------------------------------------------------------- */

int main(void) {
    RUN(test_alias_create);
    RUN(test_alias_create_with_rot);
    RUN(test_alias_create_no_at);
    RUN(test_alias_create_cursor_default);
    RUN(test_alias_delete);
    RUN(test_alias_delete_nonexistent);
    RUN(test_alias_list);
    RUN(test_alias_list_pattern);
    RUN(test_cursor_snap_copies_rot);
    RUN(test_select_near_skips_aliases);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
