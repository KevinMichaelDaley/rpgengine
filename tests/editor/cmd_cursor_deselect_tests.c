/**
 * @file cmd_cursor_deselect_tests.c
 * @brief Tests for cursor stash/pop/snap and deselect_near/deselect_regex.
 *
 * Tests:
 *  1. cursor_push saves @cursor position onto stack
 *  2. cursor_pop restores last stashed position
 *  3. cursor_pop with empty stack fails
 *  4. cursor_snap to named entity moves @cursor to entity pos
 *  5. cursor_snap with no arg snaps to selection centroid
 *  6. cursor_snap with empty selection fails
 *  7. deselect_near removes entities within distance from selection
 *  8. deselect_near defaults to @cursor when pos omitted
 *  9. deselect_regex removes matching entities from selection
 * 10. deselect_regex with no matches is a no-op (still ok)
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

static const edit_entity_t *get_cursor(void) {
    uint32_t cid = edit_entity_store_find_by_name(&g_entities, "@cursor");
    if (cid == EDIT_ENTITY_INVALID_ID) return NULL;
    return edit_entity_store_get(&g_entities, cid);
}

/* ----------------------------------------------------------------------- */
/* Tests                                                                     */
/* ----------------------------------------------------------------------- */

/** 1. cursor_push saves @cursor position onto stack. */
static bool test_cursor_push(void) {
    spawn_named("box", "@cursor", 10, 20, 30);

    uint32_t n = exec(
        "{\"id\":1,\"cmd\":\"cursor_push\",\"args\":{}}");
    ASSERT(n > 0);
    ASSERT(resp_ok());
    ASSERT(g_ctx.cursor_stack_count == 1);
    ASSERT_NEAR(g_ctx.cursor_stack[0][0], 10.0f, 0.01);
    ASSERT_NEAR(g_ctx.cursor_stack[0][1], 20.0f, 0.01);
    ASSERT_NEAR(g_ctx.cursor_stack[0][2], 30.0f, 0.01);
    return true;
}

/** 2. cursor_pop restores last stashed position. */
static bool test_cursor_pop(void) {
    spawn_named("box", "@cursor", 10, 20, 30);

    /* Push original position. */
    exec("{\"id\":1,\"cmd\":\"cursor_push\",\"args\":{}}");

    /* Move cursor away. */
    exec("{\"id\":2,\"cmd\":\"move_id\",\"args\":"
         "{\"entity_id\":\"@cursor\",\"delta\":[100,100,100]}}");
    const edit_entity_t *c = get_cursor();
    ASSERT(c);
    ASSERT_NEAR(c->pos[0], 110.0f, 0.01);

    /* Pop — should restore to (10, 20, 30). */
    uint32_t n = exec(
        "{\"id\":3,\"cmd\":\"cursor_pop\",\"args\":{}}");
    ASSERT(n > 0);
    ASSERT(resp_ok());

    c = get_cursor();
    ASSERT(c);
    ASSERT_NEAR(c->pos[0], 10.0f, 0.01);
    ASSERT_NEAR(c->pos[1], 20.0f, 0.01);
    ASSERT_NEAR(c->pos[2], 30.0f, 0.01);
    ASSERT(g_ctx.cursor_stack_count == 0);
    return true;
}

/** 3. cursor_pop with empty stack fails. */
static bool test_cursor_pop_empty(void) {
    spawn_named("box", "@cursor", 0, 0, 0);

    uint32_t n = exec(
        "{\"id\":1,\"cmd\":\"cursor_pop\",\"args\":{}}");
    ASSERT(n > 0);
    ASSERT(!resp_ok());
    return true;
}

/** 4. cursor_snap to named entity moves @cursor to its position. */
static bool test_cursor_snap_named(void) {
    spawn_named("box", "@cursor", 0, 0, 0);
    spawn_named("sphere", "target", 50, 60, 70);

    uint32_t n = exec(
        "{\"id\":1,\"cmd\":\"cursor_snap\",\"args\":"
        "{\"entity_id\":\"target\"}}");
    ASSERT(n > 0);
    ASSERT(resp_ok());

    const edit_entity_t *c = get_cursor();
    ASSERT(c);
    ASSERT_NEAR(c->pos[0], 50.0f, 0.01);
    ASSERT_NEAR(c->pos[1], 60.0f, 0.01);
    ASSERT_NEAR(c->pos[2], 70.0f, 0.01);
    return true;
}

/** 5. cursor_snap with no entity_id snaps to selection centroid. */
static bool test_cursor_snap_selection(void) {
    spawn_named("box", "@cursor", 0, 0, 0);
    spawn_named("box", "a", 10, 0, 0);
    spawn_named("box", "b", 20, 0, 0);

    uint32_t a_id = edit_entity_store_find_by_name(&g_entities, "a");
    uint32_t b_id = edit_entity_store_find_by_name(&g_entities, "b");
    edit_selection_add(&g_selection, a_id);
    edit_selection_add(&g_selection, b_id);

    uint32_t n = exec(
        "{\"id\":1,\"cmd\":\"cursor_snap\",\"args\":{}}");
    ASSERT(n > 0);
    ASSERT(resp_ok());

    /* Centroid of (10,0,0) and (20,0,0) = (15,0,0). */
    const edit_entity_t *c = get_cursor();
    ASSERT(c);
    ASSERT_NEAR(c->pos[0], 15.0f, 0.01);
    ASSERT_NEAR(c->pos[1], 0.0f, 0.01);
    return true;
}

/** 6. cursor_snap with empty selection and no entity_id fails. */
static bool test_cursor_snap_empty_selection(void) {
    spawn_named("box", "@cursor", 0, 0, 0);

    uint32_t n = exec(
        "{\"id\":1,\"cmd\":\"cursor_snap\",\"args\":{}}");
    ASSERT(n > 0);
    ASSERT(!resp_ok());
    return true;
}

/** 7. deselect_near removes entities within distance from selection. */
static bool test_deselect_near(void) {
    spawn_named("box", "close_a", 1, 0, 0);
    spawn_named("box", "close_b", 0, 1, 0);
    spawn_named("box", "far_one", 50, 50, 50);

    uint32_t ca = edit_entity_store_find_by_name(&g_entities, "close_a");
    uint32_t cb = edit_entity_store_find_by_name(&g_entities, "close_b");
    uint32_t fo = edit_entity_store_find_by_name(&g_entities, "far_one");
    edit_selection_add(&g_selection, ca);
    edit_selection_add(&g_selection, cb);
    edit_selection_add(&g_selection, fo);
    ASSERT(edit_selection_count(&g_selection) == 3);

    uint32_t n = exec(
        "{\"id\":1,\"cmd\":\"deselect_near\",\"args\":"
        "{\"pos\":[0,0,0],\"dist\":5.0}}");
    ASSERT(n > 0);
    ASSERT(resp_ok());

    /* close_a and close_b should be deselected. */
    ASSERT(!edit_selection_contains(&g_selection, ca));
    ASSERT(!edit_selection_contains(&g_selection, cb));
    /* far_one should still be selected. */
    ASSERT(edit_selection_contains(&g_selection, fo));
    return true;
}

/** 8. deselect_near defaults to @cursor when pos omitted. */
static bool test_deselect_near_cursor(void) {
    spawn_named("box", "@cursor", 5, 0, 0);
    spawn_named("box", "nearby", 6, 0, 0);
    spawn_named("box", "faraway", 50, 0, 0);

    uint32_t nb = edit_entity_store_find_by_name(&g_entities, "nearby");
    uint32_t fa = edit_entity_store_find_by_name(&g_entities, "faraway");
    edit_selection_add(&g_selection, nb);
    edit_selection_add(&g_selection, fa);

    uint32_t n = exec(
        "{\"id\":2,\"cmd\":\"deselect_near\",\"args\":"
        "{\"dist\":3.0}}");
    ASSERT(n > 0);
    ASSERT(resp_ok());

    ASSERT(!edit_selection_contains(&g_selection, nb));
    ASSERT(edit_selection_contains(&g_selection, fa));
    return true;
}

/** 9. deselect_regex removes matching entities from selection. */
static bool test_deselect_regex(void) {
    spawn_named("box", "wall_north", 0, 0, 0);
    spawn_named("box", "wall_south", 1, 0, 0);
    spawn_named("sphere", "ball", 2, 0, 0);

    uint32_t wn = edit_entity_store_find_by_name(&g_entities, "wall_north");
    uint32_t ws = edit_entity_store_find_by_name(&g_entities, "wall_south");
    uint32_t ba = edit_entity_store_find_by_name(&g_entities, "ball");
    edit_selection_add(&g_selection, wn);
    edit_selection_add(&g_selection, ws);
    edit_selection_add(&g_selection, ba);
    ASSERT(edit_selection_count(&g_selection) == 3);

    uint32_t n = exec(
        "{\"id\":1,\"cmd\":\"deselect_regex\",\"args\":"
        "{\"pattern\":\"wall\"}}");
    ASSERT(n > 0);
    ASSERT(resp_ok());

    ASSERT(!edit_selection_contains(&g_selection, wn));
    ASSERT(!edit_selection_contains(&g_selection, ws));
    ASSERT(edit_selection_contains(&g_selection, ba));
    return true;
}

/** 10. deselect_regex with no matches is a no-op. */
static bool test_deselect_regex_no_match(void) {
    spawn_named("box", "platform", 0, 0, 0);
    uint32_t pid = edit_entity_store_find_by_name(&g_entities, "platform");
    edit_selection_add(&g_selection, pid);

    uint32_t n = exec(
        "{\"id\":2,\"cmd\":\"deselect_regex\",\"args\":"
        "{\"pattern\":\"zzz\"}}");
    ASSERT(n > 0);
    ASSERT(resp_ok());
    ASSERT(edit_selection_contains(&g_selection, pid));
    return true;
}

/* ----------------------------------------------------------------------- */
/* Main                                                                      */
/* ----------------------------------------------------------------------- */

int main(void) {
    RUN(test_cursor_push);
    RUN(test_cursor_pop);
    RUN(test_cursor_pop_empty);
    RUN(test_cursor_snap_named);
    RUN(test_cursor_snap_selection);
    RUN(test_cursor_snap_empty_selection);
    RUN(test_deselect_near);
    RUN(test_deselect_near_cursor);
    RUN(test_deselect_regex);
    RUN(test_deselect_regex_no_match);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
