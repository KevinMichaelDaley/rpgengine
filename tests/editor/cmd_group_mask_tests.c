/**
 * @file cmd_group_mask_tests.c
 * @brief Tests for group_mask filtering on select commands.
 *
 * Tests:
 *  1. select_all with group_mask only selects group members
 *  2. select_regex with group_mask filters to group members
 *  3. select_near with group_mask iterates only group members
 *  4. select_touching with group_mask only adds group members
 *  5. select_fill with group_mask stops at group boundary
 *  6. select_all without group_mask selects everything (no regression)
 *  7. group_mask with nonexistent group fails
 *  8. select_near with group_mask skips non-members even if close
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
/* Mock touching callback                                                    */
/* ----------------------------------------------------------------------- */

#define MOCK_MAX_ADJ 8
#define MOCK_MAX_ENTITIES 16

static uint32_t g_mock_adj[MOCK_MAX_ENTITIES][MOCK_MAX_ADJ];
static uint32_t g_mock_adj_count[MOCK_MAX_ENTITIES];

static void mock_clear(void) {
    memset(g_mock_adj, 0, sizeof(g_mock_adj));
    memset(g_mock_adj_count, 0, sizeof(g_mock_adj_count));
}

static void mock_touch(uint32_t a, uint32_t b) {
    if (a < MOCK_MAX_ENTITIES && g_mock_adj_count[a] < MOCK_MAX_ADJ) {
        g_mock_adj[a][g_mock_adj_count[a]++] = b;
    }
    if (b < MOCK_MAX_ENTITIES && g_mock_adj_count[b] < MOCK_MAX_ADJ) {
        g_mock_adj[b][g_mock_adj_count[b]++] = a;
    }
}

static uint32_t mock_query_touching_(void *user_data,
                                      uint32_t entity_id,
                                      const uint32_t *candidates,
                                      uint32_t candidate_count,
                                      uint32_t *out_entity_ids,
                                      uint32_t max_results) {
    (void)user_data;
    if (entity_id >= MOCK_MAX_ENTITIES) return 0;

    uint32_t found = 0;
    if (candidates && candidate_count > 0) {
        /* Only check candidates for touching. */
        for (uint32_t c = 0; c < candidate_count && found < max_results; c++) {
            uint32_t cand = candidates[c];
            for (uint32_t j = 0; j < g_mock_adj_count[entity_id]; j++) {
                if (g_mock_adj[entity_id][j] == cand) {
                    out_entity_ids[found++] = cand;
                    break;
                }
            }
        }
    } else {
        /* No candidate filter — return all touching. */
        uint32_t count = g_mock_adj_count[entity_id];
        if (count > max_results) count = max_results;
        for (uint32_t i = 0; i < count; i++) {
            out_entity_ids[i] = g_mock_adj[entity_id][i];
        }
        found = count;
    }
    return found;
}

/* ----------------------------------------------------------------------- */
/* Test context                                                              */
/* ----------------------------------------------------------------------- */

static edit_dispatch_t       g_dispatch;
static edit_entity_store_t   g_entities;
static edit_selection_t      g_selection;
static edit_undo_stack_t     g_undo;
static edit_cmd_ctx_t        g_ctx;
static edit_physics_bridge_t g_bridge;
static char g_resp[8192];

static void setup(void) {
    mock_clear();
    memset(&g_ctx, 0, sizeof(g_ctx));
    memset(&g_bridge, 0, sizeof(g_bridge));
    edit_dispatch_init(&g_dispatch, 8192, &g_ctx);
    edit_entity_store_init(&g_entities, 256);
    edit_selection_init(&g_selection);
    edit_undo_init(&g_undo, 256, 1024 * 1024);

    g_bridge.on_query_touching = mock_query_touching_;
    g_bridge.user_data = NULL;

    g_ctx.entities = &g_entities;
    g_ctx.selection = &g_selection;
    g_ctx.undo = &g_undo;
    g_ctx.bridge = &g_bridge;

    edit_commands_register_all(&g_dispatch);
}

static void teardown(void) {
    if (g_ctx.groups) { free(g_ctx.groups); g_ctx.groups = NULL; }
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

/** Create group &name from entity IDs. */
static void create_group(const char *name, const uint32_t *ids, uint32_t n) {
    edit_selection_clear(&g_selection);
    for (uint32_t i = 0; i < n; i++) {
        edit_selection_add(&g_selection, ids[i]);
    }
    char json[256];
    snprintf(json, sizeof(json),
             "{\"id\":50,\"cmd\":\"group_save\",\"args\":{\"name\":\"%s\"}}",
             name);
    exec(json);
    edit_selection_clear(&g_selection);
}

/* ----------------------------------------------------------------------- */
/* Tests                                                                     */
/* ----------------------------------------------------------------------- */

/** 1. select_all with group_mask only selects group members. */
static bool test_select_all_with_mask(void) {
    spawn_named("box", "A", 0, 0, 0);
    spawn_named("box", "B", 1, 0, 0);
    spawn_named("box", "C", 2, 0, 0);

    uint32_t a = edit_entity_store_find_by_name(&g_entities, "A");
    uint32_t b = edit_entity_store_find_by_name(&g_entities, "B");
    uint32_t c = edit_entity_store_find_by_name(&g_entities, "C");

    /* Group &pair = {A, B}. */
    uint32_t pair[] = {a, b};
    create_group("&pair", pair, 2);

    exec("{\"id\":1,\"cmd\":\"select_all\",\"args\":"
         "{\"group_mask\":\"&pair\"}}");
    ASSERT(resp_ok());

    ASSERT(edit_selection_contains(&g_selection, a));
    ASSERT(edit_selection_contains(&g_selection, b));
    ASSERT(!edit_selection_contains(&g_selection, c));
    return true;
}

/** 2. select_regex with group_mask filters to group members. */
static bool test_select_regex_with_mask(void) {
    spawn_named("box", "wall_1", 0, 0, 0);
    spawn_named("box", "wall_2", 1, 0, 0);
    spawn_named("box", "wall_3", 2, 0, 0);

    uint32_t w1 = edit_entity_store_find_by_name(&g_entities, "wall_1");
    uint32_t w2 = edit_entity_store_find_by_name(&g_entities, "wall_2");
    uint32_t w3 = edit_entity_store_find_by_name(&g_entities, "wall_3");

    /* Group &subset = {wall_1, wall_3}. */
    uint32_t subset[] = {w1, w3};
    create_group("&subset", subset, 2);

    /* Regex matches all wall_*, but mask limits to &subset. */
    exec("{\"id\":1,\"cmd\":\"select_regex\",\"args\":"
         "{\"pattern\":\"wall_.*\",\"group_mask\":\"&subset\"}}");
    ASSERT(resp_ok());

    ASSERT(edit_selection_contains(&g_selection, w1));
    ASSERT(!edit_selection_contains(&g_selection, w2));
    ASSERT(edit_selection_contains(&g_selection, w3));
    return true;
}

/** 3. select_near with group_mask iterates only group members. */
static bool test_select_near_with_mask(void) {
    spawn_named("box", "close1", 0, 0, 0);
    spawn_named("box", "close2", 0.5, 0, 0);
    spawn_named("box", "close3", 0.3, 0, 0);

    uint32_t c1 = edit_entity_store_find_by_name(&g_entities, "close1");
    uint32_t c2 = edit_entity_store_find_by_name(&g_entities, "close2");
    uint32_t c3 = edit_entity_store_find_by_name(&g_entities, "close3");

    /* Group &two = {close1, close2}. close3 is NOT in group. */
    uint32_t two[] = {c1, c2};
    create_group("&two", two, 2);

    /* All three are within dist 1 of origin, but mask limits to &two. */
    exec("{\"id\":1,\"cmd\":\"select_near\",\"args\":"
         "{\"pos\":[0,0,0],\"dist\":1.0,\"group_mask\":\"&two\"}}");
    ASSERT(resp_ok());

    ASSERT(edit_selection_contains(&g_selection, c1));
    ASSERT(edit_selection_contains(&g_selection, c2));
    ASSERT(!edit_selection_contains(&g_selection, c3));
    return true;
}

/** 4. select_touching with group_mask only adds group members. */
static bool test_select_touching_with_mask(void) {
    spawn_named("box", "A", 0, 0, 0);
    spawn_named("box", "B", 1, 0, 0);
    spawn_named("box", "C", 2, 0, 0);

    uint32_t a = edit_entity_store_find_by_name(&g_entities, "A");
    uint32_t b = edit_entity_store_find_by_name(&g_entities, "B");
    uint32_t c = edit_entity_store_find_by_name(&g_entities, "C");

    /* A touches B and C. */
    mock_touch(a, b);
    mock_touch(a, c);

    /* Group &only_b = {A, B}. C is NOT in group. */
    uint32_t only_b[] = {a, b};
    create_group("&only_b", only_b, 2);

    edit_selection_add(&g_selection, a);

    exec("{\"id\":1,\"cmd\":\"select_touching\",\"args\":"
         "{\"group_mask\":\"&only_b\"}}");
    ASSERT(resp_ok());

    ASSERT(edit_selection_contains(&g_selection, b));
    ASSERT(!edit_selection_contains(&g_selection, c));
    return true;
}

/** 5. select_fill with group_mask stops at group boundary. */
static bool test_select_fill_with_mask(void) {
    spawn_named("box", "A", 0, 0, 0);
    spawn_named("box", "B", 1, 0, 0);
    spawn_named("box", "C", 2, 0, 0);
    spawn_named("box", "D", 3, 0, 0);

    uint32_t a = edit_entity_store_find_by_name(&g_entities, "A");
    uint32_t b = edit_entity_store_find_by_name(&g_entities, "B");
    uint32_t c = edit_entity_store_find_by_name(&g_entities, "C");
    uint32_t d = edit_entity_store_find_by_name(&g_entities, "D");

    /* Chain: A→B→C→D. */
    mock_touch(a, b);
    mock_touch(b, c);
    mock_touch(c, d);

    /* Group &abc = {A, B, C}. D is NOT in group. */
    uint32_t abc[] = {a, b, c};
    create_group("&abc", abc, 3);

    edit_selection_add(&g_selection, a);

    exec("{\"id\":1,\"cmd\":\"select_fill\",\"args\":"
         "{\"group_mask\":\"&abc\"}}");
    ASSERT(resp_ok());

    ASSERT(edit_selection_contains(&g_selection, a));
    ASSERT(edit_selection_contains(&g_selection, b));
    ASSERT(edit_selection_contains(&g_selection, c));
    ASSERT(!edit_selection_contains(&g_selection, d));
    return true;
}

/** 6. select_all without group_mask selects everything (no regression). */
static bool test_select_all_no_mask(void) {
    spawn_named("box", "A", 0, 0, 0);
    spawn_named("box", "B", 1, 0, 0);

    uint32_t a = edit_entity_store_find_by_name(&g_entities, "A");
    uint32_t b = edit_entity_store_find_by_name(&g_entities, "B");

    exec("{\"id\":1,\"cmd\":\"select_all\",\"args\":{}}");
    ASSERT(resp_ok());

    ASSERT(edit_selection_contains(&g_selection, a));
    ASSERT(edit_selection_contains(&g_selection, b));
    return true;
}

/** 7. group_mask with nonexistent group fails. */
static bool test_mask_nonexistent_group(void) {
    spawn_named("box", "A", 0, 0, 0);

    exec("{\"id\":1,\"cmd\":\"select_all\",\"args\":"
         "{\"group_mask\":\"&nope\"}}");
    ASSERT(resp_fail());
    return true;
}

/** 8. select_near with group_mask skips non-members even if close. */
static bool test_select_near_mask_exclusion(void) {
    spawn_named("box", "in_group", 0.1, 0, 0);
    spawn_named("box", "not_in_group", 0.2, 0, 0);

    uint32_t ig = edit_entity_store_find_by_name(&g_entities, "in_group");
    uint32_t nig = edit_entity_store_find_by_name(&g_entities, "not_in_group");

    uint32_t one[] = {ig};
    create_group("&one", one, 1);

    exec("{\"id\":1,\"cmd\":\"select_near\",\"args\":"
         "{\"pos\":[0,0,0],\"dist\":10.0,\"group_mask\":\"&one\"}}");
    ASSERT(resp_ok());

    ASSERT(edit_selection_contains(&g_selection, ig));
    ASSERT(!edit_selection_contains(&g_selection, nig));
    return true;
}

/* ----------------------------------------------------------------------- */
/* Main                                                                      */
/* ----------------------------------------------------------------------- */

int main(void) {
    RUN(test_select_all_with_mask);
    RUN(test_select_regex_with_mask);
    RUN(test_select_near_with_mask);
    RUN(test_select_touching_with_mask);
    RUN(test_select_fill_with_mask);
    RUN(test_select_all_no_mask);
    RUN(test_mask_nonexistent_group);
    RUN(test_select_near_mask_exclusion);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
