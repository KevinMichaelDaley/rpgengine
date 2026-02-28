/**
 * @file cmd_select_advanced_tests.c
 * @brief Tests for advanced selection: regex select, rotate_id, scale_id,
 *        select_near, and 3D cursor entity.
 *
 * Tests:
 *  1. select with regex pattern selects matching entities
 *  2. select with regex excludes non-matching
 *  3. rotate_id rotates a specific entity by ID or name
 *  4. scale_id scales a specific entity by ID or name
 *  5. select_near selects entities within distance of a point
 *  6. select_near excludes entities outside range
 *  7. cursor entity (id 0) is auto-created and named "@cursor"
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

/** 1. Select with regex pattern selects matching entities. */
static bool test_select_regex_matches(void) {
    spawn_named("box", "wall_north", 0, 0, 0);
    spawn_named("box", "wall_south", 1, 0, 0);
    spawn_named("sphere", "ball", 2, 0, 0);

    uint32_t n = exec(
        "{\"id\":1,\"cmd\":\"select_regex\",\"args\":"
        "{\"pattern\":\"wall\"}}");
    ASSERT(n > 0);
    ASSERT(resp_ok());

    /* Both wall_ entities should be selected. */
    ASSERT(edit_selection_count(&g_selection) == 2);
    uint32_t wall_n = edit_entity_store_find_by_name(&g_entities, "wall_north");
    uint32_t wall_s = edit_entity_store_find_by_name(&g_entities, "wall_south");
    ASSERT(edit_selection_contains(&g_selection, wall_n));
    ASSERT(edit_selection_contains(&g_selection, wall_s));

    /* ball should NOT be selected. */
    uint32_t ball = edit_entity_store_find_by_name(&g_entities, "ball");
    ASSERT(!edit_selection_contains(&g_selection, ball));
    return true;
}

/** 2. Select regex with no matches returns ok with count 0. */
static bool test_select_regex_no_match(void) {
    spawn_named("box", "platform", 0, 0, 0);

    uint32_t n = exec(
        "{\"id\":2,\"cmd\":\"select_regex\",\"args\":"
        "{\"pattern\":\"zzz\"}}");
    ASSERT(n > 0);
    ASSERT(resp_ok());
    ASSERT(edit_selection_count(&g_selection) == 0);
    return true;
}

/** 3. rotate_id rotates a specific entity by name. */
static bool test_rotate_id(void) {
    spawn_named("box", "pillar", 0, 5, 0);

    uint32_t n = exec(
        "{\"id\":3,\"cmd\":\"rotate_id\",\"args\":"
        "{\"entity_id\":\"pillar\",\"delta\":[10,20,30]}}");
    ASSERT(n > 0);
    ASSERT(resp_ok());

    uint32_t eid = edit_entity_store_find_by_name(&g_entities, "pillar");
    const edit_entity_t *e = edit_entity_store_get(&g_entities, eid);
    ASSERT(e);
    ASSERT_NEAR(e->rot[0], 10.0f, 0.01);
    ASSERT_NEAR(e->rot[1], 20.0f, 0.01);
    ASSERT_NEAR(e->rot[2], 30.0f, 0.01);
    return true;
}

/** 4. scale_id scales a specific entity by name. */
static bool test_scale_id(void) {
    spawn_named("sphere", "orb", 0, 0, 0);

    uint32_t n = exec(
        "{\"id\":4,\"cmd\":\"scale_id\",\"args\":"
        "{\"entity_id\":\"orb\",\"factor\":[2,3,4]}}");
    ASSERT(n > 0);
    ASSERT(resp_ok());

    uint32_t eid = edit_entity_store_find_by_name(&g_entities, "orb");
    const edit_entity_t *e = edit_entity_store_get(&g_entities, eid);
    ASSERT(e);
    ASSERT_NEAR(e->scale[0], 2.0f, 0.01);
    ASSERT_NEAR(e->scale[1], 3.0f, 0.01);
    ASSERT_NEAR(e->scale[2], 4.0f, 0.01);
    return true;
}

/** 5. select_near selects entities within distance of a point. */
static bool test_select_near(void) {
    spawn_named("box", "close_a", 1, 0, 0);    /* dist from origin = 1 */
    spawn_named("box", "close_b", 0, 1, 0);    /* dist from origin = 1 */
    spawn_named("box", "far_one", 10, 10, 10);  /* dist = ~17.3 */

    uint32_t n = exec(
        "{\"id\":5,\"cmd\":\"select_near\",\"args\":"
        "{\"pos\":[0,0,0],\"dist\":5.0}}");
    ASSERT(n > 0);
    ASSERT(resp_ok());

    ASSERT(edit_selection_count(&g_selection) == 2);
    uint32_t ca = edit_entity_store_find_by_name(&g_entities, "close_a");
    uint32_t cb = edit_entity_store_find_by_name(&g_entities, "close_b");
    ASSERT(edit_selection_contains(&g_selection, ca));
    ASSERT(edit_selection_contains(&g_selection, cb));
    return true;
}

/** 6. select_near with no entities in range selects nothing. */
static bool test_select_near_none(void) {
    spawn_named("box", "distant", 100, 0, 0);

    uint32_t n = exec(
        "{\"id\":6,\"cmd\":\"select_near\",\"args\":"
        "{\"pos\":[0,0,0],\"dist\":1.0}}");
    ASSERT(n > 0);
    ASSERT(resp_ok());
    ASSERT(edit_selection_count(&g_selection) == 0);
    return true;
}

/** 7. select_near defaults to @cursor position when pos absent. */
static bool test_select_near_cursor(void) {
    /* Create cursor entity at (5,0,0). */
    exec("{\"id\":90,\"cmd\":\"spawn\",\"args\":{\"type\":\"box\","
         "\"pos\":[5,0,0],\"name\":\"@cursor\"}}");

    /* Entity near cursor. */
    spawn_named("box", "nearby", 6, 0, 0);
    /* Entity far from cursor. */
    spawn_named("box", "faraway", 50, 0, 0);

    uint32_t n = exec(
        "{\"id\":7,\"cmd\":\"select_near\",\"args\":"
        "{\"dist\":3.0}}");
    ASSERT(n > 0);
    ASSERT(resp_ok());

    /* Only "nearby" should be selected (not @cursor itself). */
    uint32_t nb = edit_entity_store_find_by_name(&g_entities, "nearby");
    ASSERT(edit_selection_contains(&g_selection, nb));

    uint32_t cur = edit_entity_store_find_by_name(&g_entities, "@cursor");
    ASSERT(!edit_selection_contains(&g_selection, cur));
    return true;
}

/** 8. rotate_id by numeric ID works. */
static bool test_rotate_id_numeric(void) {
    spawn_named("box", "r_box", 0, 0, 0);
    uint32_t eid = edit_entity_store_find_by_name(&g_entities, "r_box");

    char json[256];
    snprintf(json, sizeof(json),
             "{\"id\":8,\"cmd\":\"rotate_id\",\"args\":"
             "{\"entity_id\":%u,\"delta\":[45,0,0]}}", eid);
    uint32_t n = exec(json);
    ASSERT(n > 0);
    ASSERT(resp_ok());

    const edit_entity_t *e = edit_entity_store_get(&g_entities, eid);
    ASSERT(e);
    ASSERT_NEAR(e->rot[0], 45.0f, 0.01);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Main                                                                      */
/* ----------------------------------------------------------------------- */

int main(void) {
    RUN(test_select_regex_matches);
    RUN(test_select_regex_no_match);
    RUN(test_rotate_id);
    RUN(test_scale_id);
    RUN(test_select_near);
    RUN(test_select_near_none);
    RUN(test_select_near_cursor);
    RUN(test_rotate_id_numeric);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
