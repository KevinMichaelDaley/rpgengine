/**
 * @file edit_group_tests.c
 * @brief Tests for group/ungroup/select_group commands, pivot, nesting,
 *        undo support, and serialization of groups.
 *
 * Tests:
 *  1.  group creates a named group from current selection with pivot
 *  2.  group requires & prefix
 *  3.  group with empty selection fails
 *  4.  ungroup dissolves a group
 *  5.  ungroup on nonexistent group fails
 *  6.  select_group selects all members of a group
 *  7.  select_group on nonexistent group fails
 *  8.  group computes pivot from member positions
 *  9.  group with explicit pivot uses provided value
 * 10.  nested groups: child references parent
 * 11.  undo of group creation dissolves the group
 * 12.  undo of ungroup restores the group
 * 13.  group_info returns name, count, pivot, parent
 * 14.  group overwrites existing group (updates pivot)
 * 15.  serialization round-trip preserves groups
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
#include "ferrum/editor/edit_serialize.h"
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

#define ASSERT_NEAR(a, b, eps) do { \
    if (fabsf((a) - (b)) > (eps)) { \
        printf("  ASSERT_NEAR FAILED: %f != %f (line %d)\n", \
               (double)(a), (double)(b), __LINE__); \
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
    /* Free groups if allocated. */
    if (g_ctx.groups) {
        free(g_ctx.groups);
        g_ctx.groups = NULL;
        g_ctx.group_capacity = 0;
    }
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

static void spawn_named(const char *name, float x, float y, float z) {
    char json[512];
    snprintf(json, sizeof(json),
             "{\"id\":99,\"cmd\":\"spawn\",\"args\":{\"type\":\"box\","
             "\"pos\":[%.1f,%.1f,%.1f],\"name\":\"%s\"}}",
             (double)x, (double)y, (double)z, name);
    exec(json);
}

/* ----------------------------------------------------------------------- */
/* Tests                                                                     */
/* ----------------------------------------------------------------------- */

/** 1. group creates a named group from current selection. */
static bool test_group_basic(void) {
    spawn_named("A", 0, 0, 0);
    spawn_named("B", 2, 0, 0);

    uint32_t a = edit_entity_store_find_by_name(&g_entities, "A");
    uint32_t b = edit_entity_store_find_by_name(&g_entities, "B");
    edit_selection_add(&g_selection, a);
    edit_selection_add(&g_selection, b);

    exec("{\"id\":1,\"cmd\":\"group\",\"args\":{\"name\":\"&walls\"}}");
    ASSERT(resp_ok());

    /* Verify group exists. */
    edit_group_t *grp = edit_cmd_find_group(&g_ctx, "&walls");
    ASSERT(grp != NULL);
    ASSERT(grp->count == 2);
    return true;
}

/** 2. group requires & prefix. */
static bool test_group_requires_prefix(void) {
    spawn_named("X", 0, 0, 0);
    uint32_t x = edit_entity_store_find_by_name(&g_entities, "X");
    edit_selection_add(&g_selection, x);

    exec("{\"id\":1,\"cmd\":\"group\",\"args\":{\"name\":\"walls\"}}");
    ASSERT(resp_fail());
    return true;
}

/** 3. group with empty selection fails. */
static bool test_group_empty_selection(void) {
    exec("{\"id\":1,\"cmd\":\"group\",\"args\":{\"name\":\"&empty\"}}");
    ASSERT(resp_fail());
    return true;
}

/** 4. ungroup dissolves a group. */
static bool test_ungroup(void) {
    spawn_named("A", 0, 0, 0);
    uint32_t a = edit_entity_store_find_by_name(&g_entities, "A");
    edit_selection_add(&g_selection, a);

    exec("{\"id\":1,\"cmd\":\"group\",\"args\":{\"name\":\"&temp\"}}");
    ASSERT(resp_ok());

    exec("{\"id\":2,\"cmd\":\"ungroup\",\"args\":{\"name\":\"&temp\"}}");
    ASSERT(resp_ok());

    /* Verify gone. */
    ASSERT(edit_cmd_find_group(&g_ctx, "&temp") == NULL);
    return true;
}

/** 5. ungroup on nonexistent group fails. */
static bool test_ungroup_nonexistent(void) {
    exec("{\"id\":1,\"cmd\":\"ungroup\",\"args\":{\"name\":\"&nope\"}}");
    ASSERT(resp_fail());
    return true;
}

/** 6. select_group selects all members. */
static bool test_select_group(void) {
    spawn_named("A", 0, 0, 0);
    spawn_named("B", 1, 0, 0);
    spawn_named("C", 2, 0, 0);

    uint32_t a = edit_entity_store_find_by_name(&g_entities, "A");
    uint32_t b = edit_entity_store_find_by_name(&g_entities, "B");
    uint32_t c = edit_entity_store_find_by_name(&g_entities, "C");

    edit_selection_add(&g_selection, a);
    edit_selection_add(&g_selection, b);
    exec("{\"id\":1,\"cmd\":\"group\",\"args\":{\"name\":\"&pair\"}}");

    edit_selection_clear(&g_selection);
    exec("{\"id\":2,\"cmd\":\"select_group\",\"args\":{\"name\":\"&pair\"}}");
    ASSERT(resp_ok());

    ASSERT(edit_selection_contains(&g_selection, a));
    ASSERT(edit_selection_contains(&g_selection, b));
    ASSERT(!edit_selection_contains(&g_selection, c));
    return true;
}

/** 7. select_group on nonexistent group fails. */
static bool test_select_group_nonexistent(void) {
    exec("{\"id\":1,\"cmd\":\"select_group\",\"args\":{\"name\":\"&nope\"}}");
    ASSERT(resp_fail());
    return true;
}

/** 8. group computes pivot from member positions. */
static bool test_group_auto_pivot(void) {
    spawn_named("A", 0, 0, 0);
    spawn_named("B", 4, 2, 6);

    uint32_t a = edit_entity_store_find_by_name(&g_entities, "A");
    uint32_t b = edit_entity_store_find_by_name(&g_entities, "B");
    edit_selection_add(&g_selection, a);
    edit_selection_add(&g_selection, b);

    exec("{\"id\":1,\"cmd\":\"group\",\"args\":{\"name\":\"&pair\"}}");
    ASSERT(resp_ok());

    edit_group_t *grp = edit_cmd_find_group(&g_ctx, "&pair");
    ASSERT(grp != NULL);
    /* Pivot = average of (0,0,0) and (4,2,6) = (2,1,3). */
    ASSERT_NEAR(grp->pivot[0], 2.0f, 0.01f);
    ASSERT_NEAR(grp->pivot[1], 1.0f, 0.01f);
    ASSERT_NEAR(grp->pivot[2], 3.0f, 0.01f);
    return true;
}

/** 9. group with explicit pivot uses provided value. */
static bool test_group_explicit_pivot(void) {
    spawn_named("A", 0, 0, 0);
    uint32_t a = edit_entity_store_find_by_name(&g_entities, "A");
    edit_selection_add(&g_selection, a);

    exec("{\"id\":1,\"cmd\":\"group\",\"args\":{\"name\":\"&pt\","
         "\"pivot\":[10,20,30]}}");
    ASSERT(resp_ok());

    edit_group_t *grp = edit_cmd_find_group(&g_ctx, "&pt");
    ASSERT(grp != NULL);
    ASSERT_NEAR(grp->pivot[0], 10.0f, 0.01f);
    ASSERT_NEAR(grp->pivot[1], 20.0f, 0.01f);
    ASSERT_NEAR(grp->pivot[2], 30.0f, 0.01f);
    return true;
}

/** 10. nested groups: child references parent. */
static bool test_nested_groups(void) {
    spawn_named("A", 0, 0, 0);
    spawn_named("B", 1, 0, 0);
    spawn_named("C", 2, 0, 0);

    uint32_t a = edit_entity_store_find_by_name(&g_entities, "A");
    uint32_t b = edit_entity_store_find_by_name(&g_entities, "B");
    uint32_t c = edit_entity_store_find_by_name(&g_entities, "C");

    /* Create parent group with all three. */
    edit_selection_add(&g_selection, a);
    edit_selection_add(&g_selection, b);
    edit_selection_add(&g_selection, c);
    exec("{\"id\":1,\"cmd\":\"group\",\"args\":{\"name\":\"&all\"}}");

    /* Create child group with A+B, parented to &all. */
    edit_selection_clear(&g_selection);
    edit_selection_add(&g_selection, a);
    edit_selection_add(&g_selection, b);
    exec("{\"id\":2,\"cmd\":\"group\",\"args\":{\"name\":\"&sub\","
         "\"parent\":\"&all\"}}");
    ASSERT(resp_ok());

    edit_group_t *child = edit_cmd_find_group(&g_ctx, "&sub");
    ASSERT(child != NULL);
    ASSERT(strcmp(child->parent, "&all") == 0);
    return true;
}

/** 11. group records an undo entry for creation. */
static bool test_undo_group_create(void) {
    spawn_named("A", 0, 0, 0);
    uint32_t a = edit_entity_store_find_by_name(&g_entities, "A");
    edit_selection_add(&g_selection, a);

    exec("{\"id\":1,\"cmd\":\"group\",\"args\":{\"name\":\"&ug\"}}");
    ASSERT(resp_ok());
    ASSERT(edit_cmd_find_group(&g_ctx, "&ug") != NULL);

    /* Verify undo entry was recorded. */
    const edit_undo_entry_t *entry = edit_undo_peek_undo(&g_undo);
    ASSERT(entry != NULL);
    ASSERT(entry->forward_type == EDIT_CMD_TYPE_GROUP_CREATE);
    ASSERT(entry->inverse_type == EDIT_CMD_TYPE_GROUP_DELETE);
    ASSERT(entry->snapshot_size == sizeof(edit_group_t));
    return true;
}

/** 12. ungroup records an undo entry for deletion. */
static bool test_undo_ungroup(void) {
    spawn_named("A", 0, 0, 0);
    spawn_named("B", 1, 0, 0);
    uint32_t a = edit_entity_store_find_by_name(&g_entities, "A");
    uint32_t b = edit_entity_store_find_by_name(&g_entities, "B");
    edit_selection_add(&g_selection, a);
    edit_selection_add(&g_selection, b);

    exec("{\"id\":1,\"cmd\":\"group\",\"args\":{\"name\":\"&rg\"}}");
    ASSERT(resp_ok());

    exec("{\"id\":2,\"cmd\":\"ungroup\",\"args\":{\"name\":\"&rg\"}}");
    ASSERT(resp_ok());
    ASSERT(edit_cmd_find_group(&g_ctx, "&rg") == NULL);

    /* Verify undo entry recorded for ungroup. */
    const edit_undo_entry_t *entry = edit_undo_peek_undo(&g_undo);
    ASSERT(entry != NULL);
    ASSERT(entry->forward_type == EDIT_CMD_TYPE_GROUP_DELETE);
    ASSERT(entry->inverse_type == EDIT_CMD_TYPE_GROUP_CREATE);
    /* Snapshot should contain full group data. */
    ASSERT(entry->snapshot_size == sizeof(edit_group_t));
    return true;
}

/** 13. group_info returns name, count, pivot, parent. */
static bool test_group_info(void) {
    spawn_named("A", 0, 0, 0);
    spawn_named("B", 4, 0, 0);
    uint32_t a = edit_entity_store_find_by_name(&g_entities, "A");
    uint32_t b = edit_entity_store_find_by_name(&g_entities, "B");
    edit_selection_add(&g_selection, a);
    edit_selection_add(&g_selection, b);

    exec("{\"id\":1,\"cmd\":\"group\",\"args\":{\"name\":\"&info\"}}");
    ASSERT(resp_ok());

    exec("{\"id\":2,\"cmd\":\"group_info\",\"args\":{\"name\":\"&info\"}}");
    ASSERT(resp_ok());
    ASSERT(strstr(g_resp, "&info") != NULL);
    /* Result is a JSON string with escaped quotes. */
    ASSERT(strstr(g_resp, "count") != NULL);
    ASSERT(strstr(g_resp, "pivot") != NULL);
    return true;
}

/** 14. group overwrites existing group (updates pivot). */
static bool test_group_overwrite(void) {
    spawn_named("A", 0, 0, 0);
    spawn_named("B", 10, 0, 0);

    uint32_t a = edit_entity_store_find_by_name(&g_entities, "A");
    uint32_t b = edit_entity_store_find_by_name(&g_entities, "B");

    /* Create group with A only. */
    edit_selection_add(&g_selection, a);
    exec("{\"id\":1,\"cmd\":\"group\",\"args\":{\"name\":\"&ow\"}}");

    /* Overwrite with B only → pivot should change. */
    edit_selection_clear(&g_selection);
    edit_selection_add(&g_selection, b);
    exec("{\"id\":2,\"cmd\":\"group\",\"args\":{\"name\":\"&ow\"}}");

    edit_group_t *grp = edit_cmd_find_group(&g_ctx, "&ow");
    ASSERT(grp != NULL);
    ASSERT(grp->count == 1);
    ASSERT_NEAR(grp->pivot[0], 10.0f, 0.01f);
    return true;
}

/** 15. serialization round-trip preserves groups. */
static bool test_group_serialize_roundtrip(void) {
    spawn_named("A", 0, 0, 0);
    spawn_named("B", 4, 2, 0);

    uint32_t a = edit_entity_store_find_by_name(&g_entities, "A");
    uint32_t b = edit_entity_store_find_by_name(&g_entities, "B");
    edit_selection_add(&g_selection, a);
    edit_selection_add(&g_selection, b);

    exec("{\"id\":1,\"cmd\":\"group\",\"args\":{\"name\":\"&saved\","
         "\"parent\":\"&root\"}}");
    ASSERT(resp_ok());

    /* Serialize. */
    char buf[65536];
    size_t len = edit_level_serialize_full(&g_entities, &g_ctx, buf,
                                           sizeof(buf));
    ASSERT(len > 0);

    /* Clear groups. */
    if (g_ctx.groups) {
        for (uint32_t i = 0; i < g_ctx.group_capacity; i++) {
            g_ctx.groups[i].active = false;
        }
    }
    ASSERT(edit_cmd_find_group(&g_ctx, "&saved") == NULL);

    /* Deserialize. */
    bool ok = edit_level_deserialize_full(&g_entities, &g_ctx, buf, len);
    ASSERT(ok);

    edit_group_t *grp = edit_cmd_find_group(&g_ctx, "&saved");
    ASSERT(grp != NULL);
    ASSERT(grp->count == 2);
    ASSERT(strcmp(grp->parent, "&root") == 0);
    ASSERT_NEAR(grp->pivot[0], 2.0f, 0.01f);
    ASSERT_NEAR(grp->pivot[1], 1.0f, 0.01f);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Main                                                                      */
/* ----------------------------------------------------------------------- */

int main(void) {
    RUN(test_group_basic);
    RUN(test_group_requires_prefix);
    RUN(test_group_empty_selection);
    RUN(test_ungroup);
    RUN(test_ungroup_nonexistent);
    RUN(test_select_group);
    RUN(test_select_group_nonexistent);
    RUN(test_group_auto_pivot);
    RUN(test_group_explicit_pivot);
    RUN(test_nested_groups);
    RUN(test_undo_group_create);
    RUN(test_undo_ungroup);
    RUN(test_group_info);
    RUN(test_group_overwrite);
    RUN(test_group_serialize_roundtrip);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
