/**
 * @file cmd_list_entities_tests.c
 * @brief Tests for list_entities command: returns active entity info with
 *        optional regex pattern filtering on entity name.
 *
 * Tests:
 *   1. empty store returns empty array
 *   2. returns all active entities (happy path)
 *   3. pattern filter matches named entities
 *   4. pattern filter excludes non-matching entities
 *   5. unnamed entities included when no pattern
 *   6. unnamed entities excluded when pattern given
 *   7. invalid regex pattern returns error
 */

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

/** @brief Parse the response JSON result as an array. Returns count. */
static uint32_t resp_result_array_count(void) {
    uint8_t arena_buf[8192];
    json_arena_t arena;
    json_arena_init(&arena, arena_buf, sizeof(arena_buf));
    json_value_t root;
    if (!json_parse(g_resp, strlen(g_resp), &arena, &root)) return UINT32_MAX;
    const json_value_t *result = json_object_get(&root, "result");
    if (!result || result->type != JSON_ARRAY) return UINT32_MAX;
    return result->array.count;
}

/** @brief Spawn a named entity. */
static uint32_t spawn_named(const char *type, const char *name,
                             float x, float y, float z) {
    char json[512];
    snprintf(json, sizeof(json),
             "{\"id\":99,\"cmd\":\"spawn\",\"args\":{\"type\":\"%s\","
             "\"pos\":[%.1f,%.1f,%.1f],\"name\":\"%s\"}}",
             type, (double)x, (double)y, (double)z, name);
    exec(json);
    /* Return entity ID from store (just count-1 since store is sequential). */
    return edit_entity_store_find_by_name(&g_entities, name);
}

/** @brief Spawn an unnamed entity. */
static void spawn_unnamed(const char *type, float x, float y, float z) {
    char json[512];
    snprintf(json, sizeof(json),
             "{\"id\":99,\"cmd\":\"spawn\",\"args\":{\"type\":\"%s\","
             "\"pos\":[%.1f,%.1f,%.1f]}}",
             type, (double)x, (double)y, (double)z);
    exec(json);
}

/* ----------------------------------------------------------------------- */
/* Tests                                                                     */
/* ----------------------------------------------------------------------- */

/** 1. Empty store returns empty array. */
static bool test_empty_store(void) {
    uint32_t n = exec(
        "{\"id\":1,\"cmd\":\"list_entities\",\"args\":{}}");
    ASSERT(n > 0);
    ASSERT(resp_ok());
    ASSERT(resp_result_array_count() == 0);
    return true;
}

/** 2. Returns all active entities (happy path). */
static bool test_returns_all(void) {
    spawn_named("box", "player", 0, 0, 0);
    spawn_named("sphere", "enemy", 3, 0, 0);
    spawn_unnamed("capsule", 5, 0, 0);

    uint32_t n = exec(
        "{\"id\":2,\"cmd\":\"list_entities\",\"args\":{}}");
    ASSERT(n > 0);
    ASSERT(resp_ok());
    ASSERT(resp_result_array_count() == 3);

    /* Verify named entities appear with names in the response. */
    ASSERT(strstr(g_resp, "\"player\"") != NULL);
    ASSERT(strstr(g_resp, "\"enemy\"") != NULL);
    return true;
}

/** 3. Pattern filter matches named entities. */
static bool test_pattern_matches(void) {
    spawn_named("box", "platform_a", 0, 0, 0);
    spawn_named("box", "platform_b", 1, 0, 0);
    spawn_named("sphere", "enemy_01", 2, 0, 0);

    uint32_t n = exec(
        "{\"id\":3,\"cmd\":\"list_entities\",\"args\":"
        "{\"pattern\":\"platform\"}}");
    ASSERT(n > 0);
    ASSERT(resp_ok());
    ASSERT(resp_result_array_count() == 2);

    ASSERT(strstr(g_resp, "\"platform_a\"") != NULL);
    ASSERT(strstr(g_resp, "\"platform_b\"") != NULL);
    ASSERT(strstr(g_resp, "\"enemy_01\"") == NULL);
    return true;
}

/** 4. Pattern filter excludes non-matching entities. */
static bool test_pattern_excludes(void) {
    spawn_named("box", "wall_north", 0, 0, 0);
    spawn_named("box", "wall_south", 1, 0, 0);
    spawn_named("sphere", "ball", 2, 0, 0);

    uint32_t n = exec(
        "{\"id\":4,\"cmd\":\"list_entities\",\"args\":"
        "{\"pattern\":\"^ball$\"}}");
    ASSERT(n > 0);
    ASSERT(resp_ok());
    ASSERT(resp_result_array_count() == 1);
    ASSERT(strstr(g_resp, "\"ball\"") != NULL);
    return true;
}

/** 5. Unnamed entities included when no pattern. */
static bool test_unnamed_no_pattern(void) {
    spawn_unnamed("box", 0, 0, 0);
    spawn_unnamed("sphere", 1, 0, 0);

    uint32_t n = exec(
        "{\"id\":5,\"cmd\":\"list_entities\",\"args\":{}}");
    ASSERT(n > 0);
    ASSERT(resp_ok());
    ASSERT(resp_result_array_count() == 2);
    return true;
}

/** 6. Unnamed entities excluded when pattern given. */
static bool test_unnamed_with_pattern(void) {
    spawn_named("box", "named_one", 0, 0, 0);
    spawn_unnamed("sphere", 1, 0, 0);

    uint32_t n = exec(
        "{\"id\":6,\"cmd\":\"list_entities\",\"args\":"
        "{\"pattern\":\"named\"}}");
    ASSERT(n > 0);
    ASSERT(resp_ok());
    ASSERT(resp_result_array_count() == 1);
    ASSERT(strstr(g_resp, "\"named_one\"") != NULL);
    return true;
}

/** 7. Invalid regex pattern returns error. */
static bool test_invalid_pattern(void) {
    spawn_named("box", "test", 0, 0, 0);

    uint32_t n = exec(
        "{\"id\":7,\"cmd\":\"list_entities\",\"args\":"
        "{\"pattern\":\"[invalid\"}}");
    ASSERT(n > 0);
    ASSERT(strstr(g_resp, "\"ok\":false") != NULL);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Main                                                                      */
/* ----------------------------------------------------------------------- */

int main(void) {
    RUN(test_empty_store);
    RUN(test_returns_all);
    RUN(test_pattern_matches);
    RUN(test_pattern_excludes);
    RUN(test_unnamed_no_pattern);
    RUN(test_unnamed_with_pattern);
    RUN(test_invalid_pattern);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
