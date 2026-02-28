/**
 * @file cmd_select_touching_tests.c
 * @brief Tests for select_touching and select_fill commands.
 *
 * Since full narrowphase collision requires physics world data, these tests
 * use a mock bridge callback that simulates collision results. The actual
 * collision dispatch is tested in the physics narrowphase tests.
 *
 * Tests:
 *  1. select_touching adds entities that touch current selection
 *  2. select_touching with no selection is a no-op
 *  3. select_touching with no bridge fails gracefully
 *  4. select_fill repeats select_touching until stable
 *  5. select_fill from single entity fills connected cluster
 *  6. select_fill stops at disconnected groups
 *  7. select_touching skips @ entities
 *  8. select_touching returns count of newly selected
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

/**
 * @brief Mock touching adjacency table.
 *
 * mock_adj[i] is a list of entity IDs that entity i is "touching".
 * The callback returns these when queried for entity i's body_index.
 */
#define MOCK_MAX_ADJ 8
#define MOCK_MAX_ENTITIES 16

static uint32_t g_mock_adj[MOCK_MAX_ENTITIES][MOCK_MAX_ADJ];
static uint32_t g_mock_adj_count[MOCK_MAX_ENTITIES];

static void mock_clear(void) {
    memset(g_mock_adj, 0, sizeof(g_mock_adj));
    memset(g_mock_adj_count, 0, sizeof(g_mock_adj_count));
}

/** Make entities a and b "touch" each other (symmetric). */
static void mock_touch(uint32_t a, uint32_t b) {
    if (a < MOCK_MAX_ENTITIES && g_mock_adj_count[a] < MOCK_MAX_ADJ) {
        g_mock_adj[a][g_mock_adj_count[a]++] = b;
    }
    if (b < MOCK_MAX_ENTITIES && g_mock_adj_count[b] < MOCK_MAX_ADJ) {
        g_mock_adj[b][g_mock_adj_count[b]++] = a;
    }
}

/**
 * @brief Mock bridge callback: query what entities touch a given entity.
 *
 * The entity_id maps directly to the mock adjacency table.
 * Returns entity IDs (not body indices) in out_entity_ids.
 */
static uint32_t mock_query_touching_(void *user_data,
                                      uint32_t entity_id,
                                      uint32_t *out_entity_ids,
                                      uint32_t max_results) {
    (void)user_data;
    if (entity_id >= MOCK_MAX_ENTITIES) return 0;
    uint32_t count = g_mock_adj_count[entity_id];
    if (count > max_results) count = max_results;
    for (uint32_t i = 0; i < count; i++) {
        out_entity_ids[i] = g_mock_adj[entity_id][i];
    }
    return count;
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

/** 1. select_touching adds entities that touch current selection. */
static bool test_select_touching_basic(void) {
    spawn_named("box", "A", 0, 0, 0);
    spawn_named("box", "B", 1, 0, 0);
    spawn_named("box", "C", 5, 0, 0);

    uint32_t a = edit_entity_store_find_by_name(&g_entities, "A");
    uint32_t b = edit_entity_store_find_by_name(&g_entities, "B");
    uint32_t c = edit_entity_store_find_by_name(&g_entities, "C");

    /* A touches B, but not C. */
    mock_touch(a, b);

    /* Select A. */
    edit_selection_add(&g_selection, a);

    uint32_t n = exec(
        "{\"id\":1,\"cmd\":\"select_touching\",\"args\":{}}");
    ASSERT(n > 0);
    ASSERT(resp_ok());

    /* B should now be selected (touches A). */
    ASSERT(edit_selection_contains(&g_selection, b));
    /* C should NOT be selected. */
    ASSERT(!edit_selection_contains(&g_selection, c));
    /* A still selected. */
    ASSERT(edit_selection_contains(&g_selection, a));
    return true;
}

/** 2. select_touching with no selection is a no-op. */
static bool test_select_touching_empty_selection(void) {
    spawn_named("box", "lone", 0, 0, 0);

    uint32_t n = exec(
        "{\"id\":1,\"cmd\":\"select_touching\",\"args\":{}}");
    ASSERT(n > 0);
    ASSERT(resp_ok());
    ASSERT(edit_selection_count(&g_selection) == 0);
    return true;
}

/** 3. select_touching with no bridge fails gracefully. */
static bool test_select_touching_no_bridge(void) {
    g_ctx.bridge = NULL;  /* Remove bridge. */

    spawn_named("box", "X", 0, 0, 0);
    uint32_t x = edit_entity_store_find_by_name(&g_entities, "X");
    edit_selection_add(&g_selection, x);

    uint32_t n = exec(
        "{\"id\":1,\"cmd\":\"select_touching\",\"args\":{}}");
    ASSERT(n > 0);
    ASSERT(!resp_ok());
    return true;
}

/** 4. select_fill repeats select_touching until stable. */
static bool test_select_fill_chain(void) {
    spawn_named("box", "A", 0, 0, 0);
    spawn_named("box", "B", 1, 0, 0);
    spawn_named("box", "C", 2, 0, 0);

    uint32_t a = edit_entity_store_find_by_name(&g_entities, "A");
    uint32_t b = edit_entity_store_find_by_name(&g_entities, "B");
    uint32_t c = edit_entity_store_find_by_name(&g_entities, "C");

    /* A→B→C chain. */
    mock_touch(a, b);
    mock_touch(b, c);

    /* Select only A. */
    edit_selection_add(&g_selection, a);

    uint32_t n = exec(
        "{\"id\":1,\"cmd\":\"select_fill\",\"args\":{}}");
    ASSERT(n > 0);
    ASSERT(resp_ok());

    /* All three should be selected. */
    ASSERT(edit_selection_contains(&g_selection, a));
    ASSERT(edit_selection_contains(&g_selection, b));
    ASSERT(edit_selection_contains(&g_selection, c));
    return true;
}

/** 5. select_fill from single entity fills connected cluster. */
static bool test_select_fill_cluster(void) {
    spawn_named("box", "A", 0, 0, 0);
    spawn_named("box", "B", 1, 0, 0);
    spawn_named("box", "C", 2, 0, 0);
    spawn_named("box", "D", 3, 0, 0);

    uint32_t a = edit_entity_store_find_by_name(&g_entities, "A");
    uint32_t b = edit_entity_store_find_by_name(&g_entities, "B");
    uint32_t c = edit_entity_store_find_by_name(&g_entities, "C");
    uint32_t d = edit_entity_store_find_by_name(&g_entities, "D");

    /* Full mesh: A↔B, A↔C, B↔C, C↔D. */
    mock_touch(a, b);
    mock_touch(a, c);
    mock_touch(b, c);
    mock_touch(c, d);

    edit_selection_add(&g_selection, a);

    exec("{\"id\":1,\"cmd\":\"select_fill\",\"args\":{}}");
    ASSERT(edit_selection_contains(&g_selection, a));
    ASSERT(edit_selection_contains(&g_selection, b));
    ASSERT(edit_selection_contains(&g_selection, c));
    ASSERT(edit_selection_contains(&g_selection, d));
    ASSERT(edit_selection_count(&g_selection) == 4);
    return true;
}

/** 6. select_fill stops at disconnected groups. */
static bool test_select_fill_disconnected(void) {
    spawn_named("box", "A", 0, 0, 0);
    spawn_named("box", "B", 1, 0, 0);
    spawn_named("box", "X", 100, 0, 0);
    spawn_named("box", "Y", 101, 0, 0);

    uint32_t a = edit_entity_store_find_by_name(&g_entities, "A");
    uint32_t b = edit_entity_store_find_by_name(&g_entities, "B");
    uint32_t x = edit_entity_store_find_by_name(&g_entities, "X");
    uint32_t y = edit_entity_store_find_by_name(&g_entities, "Y");

    /* Group 1: A↔B. Group 2: X↔Y. No connection between groups. */
    mock_touch(a, b);
    mock_touch(x, y);

    edit_selection_add(&g_selection, a);

    exec("{\"id\":1,\"cmd\":\"select_fill\",\"args\":{}}");
    ASSERT(edit_selection_contains(&g_selection, a));
    ASSERT(edit_selection_contains(&g_selection, b));
    ASSERT(!edit_selection_contains(&g_selection, x));
    ASSERT(!edit_selection_contains(&g_selection, y));
    return true;
}

/** 7. select_touching skips @ entities. */
static bool test_select_touching_skips_aliases(void) {
    spawn_named("box", "A", 0, 0, 0);
    spawn_named("box", "B", 1, 0, 0);

    /* Spawn an alias near A. */
    exec("{\"id\":50,\"cmd\":\"alias_create\",\"args\":"
         "{\"name\":\"@mark\",\"pos\":[0,0,0]}}");

    uint32_t a = edit_entity_store_find_by_name(&g_entities, "A");
    uint32_t b = edit_entity_store_find_by_name(&g_entities, "B");
    uint32_t mark = edit_entity_store_find_by_name(&g_entities, "@mark");

    /* A touches B and @mark. */
    mock_touch(a, b);
    mock_touch(a, mark);

    edit_selection_add(&g_selection, a);

    exec("{\"id\":1,\"cmd\":\"select_touching\",\"args\":{}}");
    ASSERT(edit_selection_contains(&g_selection, b));
    ASSERT(!edit_selection_contains(&g_selection, mark));
    return true;
}

/** 8. select_touching returns count of newly selected entities. */
static bool test_select_touching_returns_count(void) {
    spawn_named("box", "A", 0, 0, 0);
    spawn_named("box", "B", 1, 0, 0);
    spawn_named("box", "C", 2, 0, 0);

    uint32_t a = edit_entity_store_find_by_name(&g_entities, "A");
    uint32_t b = edit_entity_store_find_by_name(&g_entities, "B");
    uint32_t c = edit_entity_store_find_by_name(&g_entities, "C");

    mock_touch(a, b);
    mock_touch(a, c);

    edit_selection_add(&g_selection, a);

    exec("{\"id\":1,\"cmd\":\"select_touching\",\"args\":{}}");
    ASSERT(resp_ok());

    /* Result should be 2 (B and C were newly added). */
    ASSERT(strstr(g_resp, "\"result\":2") != NULL);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Main                                                                      */
/* ----------------------------------------------------------------------- */

int main(void) {
    RUN(test_select_touching_basic);
    RUN(test_select_touching_empty_selection);
    RUN(test_select_touching_no_bridge);
    RUN(test_select_fill_chain);
    RUN(test_select_fill_cluster);
    RUN(test_select_fill_disconnected);
    RUN(test_select_touching_skips_aliases);
    RUN(test_select_touching_returns_count);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
