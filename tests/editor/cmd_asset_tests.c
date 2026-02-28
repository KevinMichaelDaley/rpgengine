/**
 * @file cmd_asset_tests.c
 * @brief Tests for asset_list, asset_search, and asset_complete commands.
 *
 * Uses dispatch_exec with JSON strings, following the project test pattern.
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
#include "ferrum/editor/edit_asset_registry.h"
#include "ferrum/editor/json_parse.h"

/* ── Test harness ─────────────────────────────────────────────────── */

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

/* ── Test context ─────────────────────────────────────────────────── */

static edit_dispatch_t      g_dispatch;
static edit_entity_store_t  g_entities;
static edit_selection_t     g_selection;
static edit_undo_stack_t    g_undo;
static edit_cmd_ctx_t       g_ctx;
static edit_asset_registry_t g_registry;

static char g_resp[8192];

static void setup(void) {
    memset(&g_ctx, 0, sizeof(g_ctx));
    edit_dispatch_init(&g_dispatch, 8192, &g_ctx);
    edit_entity_store_init(&g_entities, 64);
    edit_selection_init(&g_selection);
    edit_undo_init(&g_undo, 64, 4096);
    edit_asset_registry_init(&g_registry, 64);

    g_ctx.entities = &g_entities;
    g_ctx.selection = &g_selection;
    g_ctx.undo = &g_undo;
    g_ctx.asset_registry = &g_registry;

    edit_commands_register_all(&g_dispatch);

    /* Populate registry with test assets. */
    edit_asset_registry_add(&g_registry, "meshes/pillar.glb",
                            EDIT_ASSET_MESH, 1024, 0xAA);
    edit_asset_registry_add(&g_registry, "meshes/wall.obj",
                            EDIT_ASSET_MESH, 2048, 0xBB);
    edit_asset_registry_add(&g_registry, "textures/brick.png",
                            EDIT_ASSET_TEXTURE, 4096, 0xCC);
    edit_asset_registry_add(&g_registry, "textures/stone.ktx2",
                            EDIT_ASSET_TEXTURE, 8192, 0xDD);
    edit_asset_registry_add(&g_registry, "prefabs/tower.prefab",
                            EDIT_ASSET_PREFAB, 512, 0xEE);
}

static void teardown(void) {
    edit_dispatch_destroy(&g_dispatch);
    edit_entity_store_destroy(&g_entities);
    edit_selection_destroy(&g_selection);
    edit_undo_destroy(&g_undo);
    edit_asset_registry_destroy(&g_registry);
}

/* ── Helpers ──────────────────────────────────────────────────────── */

static uint32_t exec(const char *json) {
    memset(g_resp, 0, sizeof(g_resp));
    return edit_dispatch_exec(&g_dispatch, json, (uint32_t)strlen(json),
                              g_resp, sizeof(g_resp));
}

static bool resp_ok(void) {
    return strstr(g_resp, "\"ok\":true") != NULL;
}

/** Count occurrences of a substring in g_resp. */
static int count_substr(const char *sub) {
    int c = 0;
    const char *p = g_resp;
    size_t len = strlen(sub);
    while ((p = strstr(p, sub)) != NULL) { c++; p += len; }
    return c;
}

/* ── Tests ────────────────────────────────────────────────────────── */

/** asset_list with no args returns all 5 assets. */
static bool test_asset_list_all(void) {
    exec("{\"id\":1,\"cmd\":\"asset_list\",\"args\":{}}");
    ASSERT(resp_ok());
    /* Each asset path appears in result. */
    ASSERT(strstr(g_resp, "pillar") != NULL);
    ASSERT(strstr(g_resp, "wall") != NULL);
    ASSERT(strstr(g_resp, "brick") != NULL);
    ASSERT(strstr(g_resp, "stone") != NULL);
    ASSERT(strstr(g_resp, "tower") != NULL);
    return true;
}

/** asset_list with prefix filters to meshes/ only. */
static bool test_asset_list_prefix(void) {
    exec("{\"id\":2,\"cmd\":\"asset_list\",\"args\":{\"prefix\":\"meshes/\"}}");
    ASSERT(resp_ok());
    ASSERT(strstr(g_resp, "pillar") != NULL);
    ASSERT(strstr(g_resp, "wall") != NULL);
    /* Textures and prefabs should not appear. */
    ASSERT(strstr(g_resp, "brick") == NULL);
    ASSERT(strstr(g_resp, "tower") == NULL);
    return true;
}

/** asset_list with type filter returns only textures. */
static bool test_asset_list_type_filter(void) {
    exec("{\"id\":3,\"cmd\":\"asset_list\",\"args\":{\"type\":\"texture\"}}");
    ASSERT(resp_ok());
    ASSERT(strstr(g_resp, "brick") != NULL);
    ASSERT(strstr(g_resp, "stone") != NULL);
    /* Meshes should not appear. */
    ASSERT(strstr(g_resp, "pillar") == NULL);
    return true;
}

/** asset_list with no registry fails gracefully. */
static bool test_asset_list_no_registry(void) {
    g_ctx.asset_registry = NULL;
    exec("{\"id\":4,\"cmd\":\"asset_list\",\"args\":{}}");
    ASSERT(!resp_ok());
    return true;
}

/** asset_search finds matching assets. */
static bool test_asset_search(void) {
    exec("{\"id\":5,\"cmd\":\"asset_search\",\"args\":{\"pattern\":\"pil\"}}");
    ASSERT(resp_ok());
    ASSERT(strstr(g_resp, "pillar") != NULL);
    ASSERT(count_substr("meshes/") == 1);
    return true;
}

/** asset_search with no pattern fails. */
static bool test_asset_search_no_pattern(void) {
    exec("{\"id\":6,\"cmd\":\"asset_search\",\"args\":{}}");
    ASSERT(!resp_ok());
    return true;
}

/** asset_search regex matches multiple assets. */
static bool test_asset_search_regex(void) {
    exec("{\"id\":7,\"cmd\":\"asset_search\",\"args\":{\"pattern\":\"^textures/\"}}");
    ASSERT(resp_ok());
    ASSERT(strstr(g_resp, "brick") != NULL);
    ASSERT(strstr(g_resp, "stone") != NULL);
    return true;
}

/** asset_complete returns matching paths. */
static bool test_asset_complete(void) {
    exec("{\"id\":8,\"cmd\":\"asset_complete\",\"args\":{\"prefix\":\"tex\"}}");
    ASSERT(resp_ok());
    ASSERT(strstr(g_resp, "textures/brick.png") != NULL);
    ASSERT(strstr(g_resp, "textures/stone.ktx2") != NULL);
    return true;
}

/** asset_complete with empty prefix returns all assets. */
static bool test_asset_complete_empty(void) {
    exec("{\"id\":9,\"cmd\":\"asset_complete\",\"args\":{}}");
    ASSERT(resp_ok());
    ASSERT(strstr(g_resp, "pillar") != NULL);
    ASSERT(strstr(g_resp, "tower") != NULL);
    return true;
}

int main(void) {
    RUN(test_asset_list_all);
    RUN(test_asset_list_prefix);
    RUN(test_asset_list_type_filter);
    RUN(test_asset_list_no_registry);
    RUN(test_asset_search);
    RUN(test_asset_search_no_pattern);
    RUN(test_asset_search_regex);
    RUN(test_asset_complete);
    RUN(test_asset_complete_empty);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
