/**
 * @file cmd_browse_tests.c
 * @brief Tests for cmd_browse — list directory contents from asset registry.
 */

#include "ferrum/editor/edit_dispatch.h"
#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_asset_registry.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/editor/edit_undo.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── Minimal test harness ────────────────────────────────────────── */
static int g_pass, g_fail;
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  ASSERT FAILED: %s (line %d)\n", #cond, __LINE__); \
        g_fail++; return; \
    } \
} while (0)
#define RUN(fn) do { printf("RUN  %s\n", #fn); fn(); \
    if (g_fail == prev_fail) { g_pass++; printf("OK   %s\n", #fn); } \
    else printf("FAIL %s\n", #fn); prev_fail = g_fail; } while(0)

/* ── Shared state ─────────────────────────────────────────────────── */
static edit_dispatch_t       g_dispatch;
static edit_entity_store_t   g_entities;
static edit_selection_t      g_selection;
static edit_undo_stack_t     g_undo;
static edit_cmd_ctx_t        g_ctx;
static edit_asset_registry_t g_registry;
static char                  g_resp[4096];

static int exec_(const char *json) {
    return edit_dispatch_exec(&g_dispatch, json, (uint32_t)strlen(json),
                              g_resp, sizeof(g_resp));
}

static bool resp_ok_(void) {
    return strstr(g_resp, "\"ok\":true") != NULL;
}

/* ── Setup / teardown ─────────────────────────────────────────────── */
static void setup_(void) {
    memset(&g_ctx, 0, sizeof(g_ctx));
    edit_dispatch_init(&g_dispatch, 16384, &g_ctx);
    edit_entity_store_init(&g_entities, 256);
    edit_selection_init(&g_selection);
    edit_undo_init(&g_undo, 64, 4096);
    edit_asset_registry_init(&g_registry, 64);

    g_ctx.entities       = &g_entities;
    g_ctx.selection      = &g_selection;
    g_ctx.undo           = &g_undo;
    g_ctx.asset_registry = &g_registry;

    edit_commands_register_all(&g_dispatch);

    /* Register a set of test assets. */
    edit_asset_registry_add(&g_registry, "meshes/box.glb",          EDIT_ASSET_MESH, 1000, 0xAA);
    edit_asset_registry_add(&g_registry, "meshes/sphere.glb",       EDIT_ASSET_MESH, 2000, 0xBB);
    edit_asset_registry_add(&g_registry, "meshes/wall_section.glb", EDIT_ASSET_MESH, 1500, 0xCC);
    edit_asset_registry_add(&g_registry, "meshes/pillar.glb",       EDIT_ASSET_MESH, 800, 0xDD);
    edit_asset_registry_add(&g_registry, "textures/brick.png",      EDIT_ASSET_TEXTURE, 500, 0xEE);
    edit_asset_registry_add(&g_registry, "textures/stone.png",      EDIT_ASSET_TEXTURE, 600, 0xFF);
}

static void teardown_(void) {
    edit_dispatch_destroy(&g_dispatch);
    edit_asset_registry_destroy(&g_registry);
    edit_selection_destroy(&g_selection);
    edit_undo_destroy(&g_undo);
    edit_entity_store_destroy(&g_entities);
}

/* ── Tests ────────────────────────────────────────────────────────── */

/** Test: browse with prefix returns only matching assets. */
static void test_browse_prefix(void) {
    setup_();
    ASSERT(exec_("{\"id\":1,\"cmd\":\"browse\","
                  "\"args\":{\"prefix\":\"meshes/\"}}") > 0);
    ASSERT(resp_ok_());
    ASSERT(strstr(g_resp, "box.glb") != NULL);
    ASSERT(strstr(g_resp, "sphere.glb") != NULL);
    ASSERT(strstr(g_resp, "wall_section.glb") != NULL);
    ASSERT(strstr(g_resp, "pillar.glb") != NULL);
    /* Textures should not appear. */
    ASSERT(strstr(g_resp, "brick.png") == NULL);
    teardown_();
}

/** Test: browse with prefix + filter returns only filtered results. */
static void test_browse_filter(void) {
    setup_();
    ASSERT(exec_("{\"id\":1,\"cmd\":\"browse\","
                  "\"args\":{\"prefix\":\"meshes/\","
                  "\"filter\":\"wall\"}}") > 0);
    ASSERT(resp_ok_());
    ASSERT(strstr(g_resp, "wall_section.glb") != NULL);
    /* Others should not appear. */
    ASSERT(strstr(g_resp, "box.glb") == NULL);
    ASSERT(strstr(g_resp, "sphere.glb") == NULL);
    teardown_();
}

/** Test: browse with no prefix returns all assets. */
static void test_browse_all(void) {
    setup_();
    ASSERT(exec_("{\"id\":1,\"cmd\":\"browse\","
                  "\"args\":{}}") > 0);
    ASSERT(resp_ok_());
    ASSERT(strstr(g_resp, "box.glb") != NULL);
    ASSERT(strstr(g_resp, "brick.png") != NULL);
    teardown_();
}

/** Test: browse with no-match filter returns empty array. */
static void test_browse_no_match(void) {
    setup_();
    ASSERT(exec_("{\"id\":1,\"cmd\":\"browse\","
                  "\"args\":{\"prefix\":\"meshes/\","
                  "\"filter\":\"zzzzz\"}}") > 0);
    ASSERT(resp_ok_());
    /* Should be empty result array. */
    ASSERT(strstr(g_resp, "box.glb") == NULL);
    teardown_();
}

/** Test: browse with empty registry returns empty array. */
static void test_browse_empty_registry(void) {
    edit_dispatch_t      disp;
    edit_entity_store_t  ent;
    edit_selection_t     sel;
    edit_undo_stack_t    undo;
    edit_cmd_ctx_t       ctx2;
    edit_asset_registry_t reg;

    memset(&ctx2, 0, sizeof(ctx2));
    edit_dispatch_init(&disp, 8192, &ctx2);
    edit_entity_store_init(&ent, 64);
    edit_selection_init(&sel);
    edit_undo_init(&undo, 64, 4096);
    edit_asset_registry_init(&reg, 16);

    ctx2.entities       = &ent;
    ctx2.selection      = &sel;
    ctx2.undo           = &undo;
    ctx2.asset_registry = &reg;
    edit_commands_register_all(&disp);

    char resp[2048];
    int n = edit_dispatch_exec(&disp, "{\"id\":1,\"cmd\":\"browse\","
                  "\"args\":{\"prefix\":\"meshes/\"}}",
                  strlen("{\"id\":1,\"cmd\":\"browse\","
                  "\"args\":{\"prefix\":\"meshes/\"}}"),
                  resp, sizeof(resp));
    ASSERT(n > 0);
    ASSERT(strstr(resp, "\"ok\":true") != NULL);
    ASSERT(strstr(resp, "box.glb") == NULL);

    edit_dispatch_destroy(&disp);
    edit_asset_registry_destroy(&reg);
    edit_selection_destroy(&sel);
    edit_undo_destroy(&undo);
    edit_entity_store_destroy(&ent);
}

int main(void) {
    int prev_fail = 0;
    RUN(test_browse_prefix);
    RUN(test_browse_filter);
    RUN(test_browse_all);
    RUN(test_browse_no_match);
    RUN(test_browse_empty_registry);
    printf("\n%d / %d tests passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
