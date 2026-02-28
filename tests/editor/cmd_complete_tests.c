/**
 * @file cmd_complete_tests.c
 * @brief Tests for the general tab-completion server command.
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_dispatch.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/editor/edit_undo.h"
#include "ferrum/editor/edit_asset_registry.h"

/* ----------------------------------------------------------------------- */
/* Test macros                                                               */
/* ----------------------------------------------------------------------- */

static int g_pass, g_fail;

#define RUN(fn) do { \
    setup_(); \
    printf("RUN  %s\n", #fn); \
    if (fn()) { printf("OK   %s\n", #fn); g_pass++; } \
    else       { printf("FAIL %s\n", #fn); g_fail++; } \
    teardown_(); \
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

static edit_dispatch_t      g_dispatch;
static edit_entity_store_t  g_entities;
static edit_selection_t     g_selection;
static edit_undo_stack_t    g_undo;
static edit_cmd_ctx_t       g_ctx;
static edit_asset_registry_t g_registry;
static char                 g_resp[8192];

static void setup_(void) {
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

    edit_asset_registry_add(&g_registry, "meshes/box.glb",
                             EDIT_ASSET_MESH, 100, 0x1111);
    edit_asset_registry_add(&g_registry, "meshes/barrel.glb",
                             EDIT_ASSET_MESH, 200, 0x2222);
    edit_asset_registry_add(&g_registry, "textures/brick.png",
                             EDIT_ASSET_TEXTURE, 300, 0x3333);
}

static void teardown_(void) {
    edit_dispatch_destroy(&g_dispatch);
    edit_entity_store_destroy(&g_entities);
    edit_undo_destroy(&g_undo);
    edit_asset_registry_destroy(&g_registry);
}

static uint32_t exec_(const char *json) {
    return edit_dispatch_exec(&g_dispatch, json, (uint32_t)strlen(json),
                               g_resp, sizeof(g_resp));
}

static bool resp_ok_(void) {
    return strstr(g_resp, "\"ok\":true") != NULL;
}

/* ----------------------------------------------------------------------- */
/* Tests                                                                     */
/* ----------------------------------------------------------------------- */

/** Complete with mesh prefix returns matching paths. */
static bool test_complete_asset_prefix(void) {
    ASSERT(exec_("{\"id\":1,\"cmd\":\"complete\","
                  "\"args\":{\"context\":\"spawn mesh meshes/b\"}}") > 0);
    ASSERT(resp_ok_());
    ASSERT(strstr(g_resp, "meshes/box.glb") != NULL);
    ASSERT(strstr(g_resp, "meshes/barrel.glb") != NULL);
    return true;
}

/** Complete with unique prefix resolves to single match. */
static bool test_complete_unique(void) {
    ASSERT(exec_("{\"id\":1,\"cmd\":\"complete\","
                  "\"args\":{\"context\":\"spawn mesh textures/\"}}") > 0);
    ASSERT(resp_ok_());
    ASSERT(strstr(g_resp, "textures/brick.png") != NULL);
    /* Should NOT have mesh paths. */
    ASSERT(strstr(g_resp, "meshes/") == NULL);
    return true;
}

/** Complete with no matches returns empty. */
static bool test_complete_no_match(void) {
    ASSERT(exec_("{\"id\":1,\"cmd\":\"complete\","
                  "\"args\":{\"context\":\"spawn mesh zzz\"}}") > 0);
    ASSERT(resp_ok_());
    /* Result should be empty array. */
    ASSERT(strstr(g_resp, "[]") != NULL);
    return true;
}

/** Complete with no context arg. */
static bool test_complete_empty_context(void) {
    ASSERT(exec_("{\"id\":1,\"cmd\":\"complete\","
                  "\"args\":{\"context\":\"\"}}") > 0);
    ASSERT(resp_ok_());
    return true;
}

int main(void) {
    RUN(test_complete_asset_prefix);
    RUN(test_complete_unique);
    RUN(test_complete_no_match);
    RUN(test_complete_empty_context);

    printf("\n%d / %d tests passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}

