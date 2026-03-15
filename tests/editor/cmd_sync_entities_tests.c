/**
 * @file cmd_sync_entities_tests.c
 * @brief Tests for sync_entities command — delta and full sync responses.
 *
 * Tests:
 *   1. full sync when since_version=0
 *   2. delta sync returns only changed entities
 *   3. delta sync includes tombstones
 *   4. full sync after tombstone ring wraps
 *   5. empty delta (no changes since version)
 *   6. delta with offset entities (correct IDs returned)
 *   7. full sync pagination (offset/limit)
 *   8. version field present and correct in response
 *   9. null args returns full sync
 *  10. missing since_version returns full sync
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_entity_version.h"
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
static edit_version_state_t  g_version;
static edit_cmd_ctx_t        g_ctx;
static char g_resp[65536];

static void setup(void) {
    memset(&g_ctx, 0, sizeof(g_ctx));
    edit_dispatch_init(&g_dispatch, 65536, &g_ctx);
    edit_entity_store_init(&g_entities, 256);
    edit_selection_init(&g_selection);
    edit_undo_init(&g_undo, 256, 1024 * 1024);
    edit_version_init(&g_version, 256);

    g_ctx.entities  = &g_entities;
    g_ctx.selection = &g_selection;
    g_ctx.undo      = &g_undo;
    g_ctx.version   = &g_version;

    edit_commands_register_all(&g_dispatch);
}

static void teardown(void) {
    edit_dispatch_destroy(&g_dispatch);
    edit_entity_store_destroy(&g_entities);
    edit_selection_destroy(&g_selection);
    edit_undo_destroy(&g_undo);
    edit_version_destroy(&g_version);
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

/** @brief Parse the response and extract the result object. */
static bool parse_result(json_value_t *out, uint8_t *arena_buf,
                          size_t arena_sz) {
    json_arena_t arena;
    json_arena_init(&arena, arena_buf, arena_sz);
    json_value_t root;
    if (!json_parse(g_resp, strlen(g_resp), &arena, &root)) return false;
    const json_value_t *result = json_object_get(&root, "result");
    if (!result) return false;
    *out = *result;
    return true;
}

/** @brief Get a number field from a JSON object. Returns -1 on failure. */
static double get_num(const json_value_t *obj, const char *key) {
    const json_value_t *v = json_object_get(obj, key);
    if (!v || v->type != JSON_NUMBER) return -1.0;
    return v->number;
}

/** @brief Get a bool field from a JSON object. Returns -1 on failure. */
static int get_bool(const json_value_t *obj, const char *key) {
    const json_value_t *v = json_object_get(obj, key);
    if (!v || v->type != JSON_BOOL) return -1;
    return v->boolean ? 1 : 0;
}

/** @brief Get array count for a field. Returns UINT32_MAX on failure. */
static uint32_t get_array_count(const json_value_t *obj, const char *key) {
    const json_value_t *v = json_object_get(obj, key);
    if (!v || v->type != JSON_ARRAY) return UINT32_MAX;
    return v->array.count;
}

/** @brief Spawn a named entity via command. */
static void spawn_named(const char *type, const char *name,
                         float x, float y, float z) {
    char json[512];
    snprintf(json, sizeof(json),
             "{\"id\":99,\"cmd\":\"spawn\",\"args\":{\"type\":\"%s\","
             "\"pos\":[%.1f,%.1f,%.1f],\"name\":\"%s\"}}",
             type, (double)x, (double)y, (double)z, name);
    exec(json);
}

/** @brief Delete entity by ID via command. */
static void delete_entity(uint32_t eid) {
    char json[256];
    snprintf(json, sizeof(json),
             "{\"id\":99,\"cmd\":\"delete_id\",\"args\":{\"entity_id\":%u}}",
             (unsigned)eid);
    exec(json);
}

/* ----------------------------------------------------------------------- */
/* Tests                                                                     */
/* ----------------------------------------------------------------------- */

/** 1. Full sync when since_version=0. */
static bool test_full_sync_since_zero(void) {
    spawn_named("box", "a", 0, 0, 0);
    spawn_named("sphere", "b", 1, 0, 0);

    exec("{\"id\":1,\"cmd\":\"sync_entities\",\"args\":{\"since_version\":0}}");
    ASSERT(resp_ok());

    uint8_t buf[32768];
    json_value_t result;
    ASSERT(parse_result(&result, buf, sizeof(buf)));

    /* Must be a full sync. */
    ASSERT(get_bool(&result, "full") == 1);
    /* Should include both entities. */
    ASSERT(get_array_count(&result, "entities") == 2);
    /* Tombstones should be empty for full sync. */
    ASSERT(get_array_count(&result, "tombstones") == 0);
    /* Version should be > 0. */
    ASSERT(get_num(&result, "version") > 0);
    /* Total should equal 2. */
    ASSERT((uint32_t)get_num(&result, "total") == 2);
    return true;
}

/** 2. Delta sync returns only changed entities. */
static bool test_delta_changed_only(void) {
    spawn_named("box", "a", 0, 0, 0);
    spawn_named("sphere", "b", 1, 0, 0);

    /* Record version after initial spawns. */
    uint64_t v1 = g_version.version;

    /* Modify one entity (move "a"). */
    uint32_t aid = edit_entity_store_find_by_name(&g_entities, "a");
    ASSERT(aid != EDIT_ENTITY_INVALID_ID);
    char json[256];
    snprintf(json, sizeof(json),
             "{\"id\":99,\"cmd\":\"move_id\",\"args\":"
             "{\"entity_id\":%u,\"delta\":[5,0,0]}}",
             (unsigned)aid);
    exec(json);

    /* Request delta since v1. */
    snprintf(json, sizeof(json),
             "{\"id\":2,\"cmd\":\"sync_entities\",\"args\":"
             "{\"since_version\":%llu}}",
             (unsigned long long)v1);
    exec(json);
    ASSERT(resp_ok());

    uint8_t buf[32768];
    json_value_t result;
    ASSERT(parse_result(&result, buf, sizeof(buf)));

    /* Should be a delta (not full). */
    ASSERT(get_bool(&result, "full") == 0);
    /* Only 1 entity changed (the moved one). */
    ASSERT(get_array_count(&result, "entities") == 1);
    /* No tombstones. */
    ASSERT(get_array_count(&result, "tombstones") == 0);
    return true;
}

/** 3. Delta sync includes tombstones. */
static bool test_delta_tombstones(void) {
    spawn_named("box", "a", 0, 0, 0);
    spawn_named("sphere", "b", 1, 0, 0);
    spawn_named("capsule", "c", 2, 0, 0);

    uint64_t v1 = g_version.version;

    /* Delete entity "b". */
    uint32_t bid = edit_entity_store_find_by_name(&g_entities, "b");
    ASSERT(bid != EDIT_ENTITY_INVALID_ID);
    delete_entity(bid);

    /* Request delta since v1. */
    char json[256];
    snprintf(json, sizeof(json),
             "{\"id\":3,\"cmd\":\"sync_entities\",\"args\":"
             "{\"since_version\":%llu}}",
             (unsigned long long)v1);
    exec(json);
    ASSERT(resp_ok());

    uint8_t buf[32768];
    json_value_t result;
    ASSERT(parse_result(&result, buf, sizeof(buf)));

    ASSERT(get_bool(&result, "full") == 0);
    /* No entities changed (only deletion). */
    ASSERT(get_array_count(&result, "entities") == 0);
    /* One tombstone. */
    ASSERT(get_array_count(&result, "tombstones") == 1);

    /* Tombstone should contain the deleted entity ID. */
    const json_value_t *ts = json_object_get(&result, "tombstones");
    ASSERT(ts->array.items[0].type == JSON_NUMBER);
    ASSERT((uint32_t)ts->array.items[0].number == bid);
    return true;
}

/** 4. Full sync after tombstone ring wraps. */
static bool test_full_sync_tombstone_wrap(void) {
    /* Create and delete many entities to fill the tombstone ring.
     * Our version state has capacity 256 entities but the tombstone
     * ring uses EDIT_VERSION_TOMBSTONE_CAP (4096). We need to wrap it.
     * Instead, create a small version state manually. */

    /* Reset version state with small tombstone capacity for testing. */
    edit_version_destroy(&g_version);
    memset(&g_version, 0, sizeof(g_version));
    /* Manually init with tiny tombstone ring. */
    g_version.entity_capacity = 256;
    g_version.entity_version = calloc(256, sizeof(uint64_t));
    g_version.tombstone_capacity = 4;
    g_version.tombstones = calloc(4, sizeof(edit_version_tombstone_t));

    spawn_named("box", "keeper", 0, 0, 0);

    uint64_t v1 = g_version.version;

    /* Create and delete enough entities to wrap the tiny ring. */
    for (int i = 0; i < 6; i++) {
        char nm[32];
        snprintf(nm, sizeof(nm), "tmp_%d", i);
        spawn_named("box", nm, (float)i, 0, 0);
        uint32_t eid = edit_entity_store_find_by_name(&g_entities, nm);
        if (eid != EDIT_ENTITY_INVALID_ID) {
            delete_entity(eid);
        }
    }

    /* Request delta since v1 — should require full sync because
     * tombstone ring wrapped past v1. */
    char json[256];
    snprintf(json, sizeof(json),
             "{\"id\":4,\"cmd\":\"sync_entities\",\"args\":"
             "{\"since_version\":%llu}}",
             (unsigned long long)v1);
    exec(json);
    ASSERT(resp_ok());

    uint8_t buf[32768];
    json_value_t result;
    ASSERT(parse_result(&result, buf, sizeof(buf)));

    /* Must be a full sync due to tombstone ring wrap. */
    ASSERT(get_bool(&result, "full") == 1);

    /* Clean up manual allocs. */
    free(g_version.entity_version);
    free(g_version.tombstones);
    memset(&g_version, 0, sizeof(g_version));
    edit_version_init(&g_version, 256);

    return true;
}

/** 5. Empty delta (no changes since version). */
static bool test_empty_delta(void) {
    spawn_named("box", "a", 0, 0, 0);

    uint64_t v1 = g_version.version;

    /* No changes — request delta. */
    char json[256];
    snprintf(json, sizeof(json),
             "{\"id\":5,\"cmd\":\"sync_entities\",\"args\":"
             "{\"since_version\":%llu}}",
             (unsigned long long)v1);
    exec(json);
    ASSERT(resp_ok());

    uint8_t buf[32768];
    json_value_t result;
    ASSERT(parse_result(&result, buf, sizeof(buf)));

    ASSERT(get_bool(&result, "full") == 0);
    ASSERT(get_array_count(&result, "entities") == 0);
    ASSERT(get_array_count(&result, "tombstones") == 0);
    ASSERT(get_num(&result, "version") == (double)v1);
    return true;
}

/** 6. Delta with multiple changed entities has correct IDs. */
static bool test_delta_correct_ids(void) {
    spawn_named("box", "a", 0, 0, 0);
    spawn_named("sphere", "b", 1, 0, 0);
    spawn_named("capsule", "c", 2, 0, 0);

    uint64_t v1 = g_version.version;

    /* Move entities a and c. */
    uint32_t aid = edit_entity_store_find_by_name(&g_entities, "a");
    uint32_t cid = edit_entity_store_find_by_name(&g_entities, "c");

    char json[256];
    snprintf(json, sizeof(json),
             "{\"id\":99,\"cmd\":\"move_id\",\"args\":"
             "{\"entity_id\":%u,\"delta\":[10,0,0]}}",
             (unsigned)aid);
    exec(json);
    snprintf(json, sizeof(json),
             "{\"id\":99,\"cmd\":\"move_id\",\"args\":"
             "{\"entity_id\":%u,\"delta\":[20,0,0]}}",
             (unsigned)cid);
    exec(json);

    /* Request delta. */
    snprintf(json, sizeof(json),
             "{\"id\":6,\"cmd\":\"sync_entities\",\"args\":"
             "{\"since_version\":%llu}}",
             (unsigned long long)v1);
    exec(json);
    ASSERT(resp_ok());

    uint8_t buf[32768];
    json_value_t result;
    ASSERT(parse_result(&result, buf, sizeof(buf)));

    ASSERT(get_bool(&result, "full") == 0);
    ASSERT(get_array_count(&result, "entities") == 2);

    /* Verify both changed entity IDs are present. */
    const json_value_t *ents = json_object_get(&result, "entities");
    bool found_a = false, found_c = false;
    for (uint32_t i = 0; i < ents->array.count; i++) {
        const json_value_t *id_val = json_object_get(&ents->array.items[i], "id");
        if (id_val && id_val->type == JSON_NUMBER) {
            uint32_t eid = (uint32_t)id_val->number;
            if (eid == aid) found_a = true;
            if (eid == cid) found_c = true;
        }
    }
    ASSERT(found_a);
    ASSERT(found_c);
    return true;
}

/** 7. Full sync pagination. */
static bool test_full_sync_pagination(void) {
    /* Spawn 5 entities. */
    for (int i = 0; i < 5; i++) {
        char nm[32];
        snprintf(nm, sizeof(nm), "ent_%d", i);
        spawn_named("box", nm, (float)i, 0, 0);
    }

    /* Request full sync with limit=2. */
    exec("{\"id\":7,\"cmd\":\"sync_entities\",\"args\":"
         "{\"since_version\":0,\"limit\":2}}");
    ASSERT(resp_ok());

    uint8_t buf[32768];
    json_value_t result;
    ASSERT(parse_result(&result, buf, sizeof(buf)));

    ASSERT(get_bool(&result, "full") == 1);
    ASSERT(get_array_count(&result, "entities") == 2);
    ASSERT((uint32_t)get_num(&result, "total") == 5);
    ASSERT((uint32_t)get_num(&result, "offset") == 0);

    /* Request second page. */
    exec("{\"id\":8,\"cmd\":\"sync_entities\",\"args\":"
         "{\"since_version\":0,\"offset\":2,\"limit\":2}}");
    ASSERT(resp_ok());
    ASSERT(parse_result(&result, buf, sizeof(buf)));
    ASSERT(get_array_count(&result, "entities") == 2);
    ASSERT((uint32_t)get_num(&result, "offset") == 2);
    return true;
}

/** 8. Version field present and correct. */
static bool test_version_field(void) {
    spawn_named("box", "a", 0, 0, 0);
    uint64_t expected = g_version.version;

    exec("{\"id\":9,\"cmd\":\"sync_entities\",\"args\":{\"since_version\":0}}");
    ASSERT(resp_ok());

    uint8_t buf[32768];
    json_value_t result;
    ASSERT(parse_result(&result, buf, sizeof(buf)));
    ASSERT((uint64_t)get_num(&result, "version") == expected);
    return true;
}

/** 9. Null args returns full sync. */
static bool test_null_args(void) {
    spawn_named("box", "a", 0, 0, 0);

    exec("{\"id\":10,\"cmd\":\"sync_entities\"}");
    ASSERT(resp_ok());

    uint8_t buf[32768];
    json_value_t result;
    ASSERT(parse_result(&result, buf, sizeof(buf)));
    ASSERT(get_bool(&result, "full") == 1);
    return true;
}

/** 10. Missing since_version returns full sync. */
static bool test_missing_since_version(void) {
    spawn_named("box", "a", 0, 0, 0);

    exec("{\"id\":11,\"cmd\":\"sync_entities\",\"args\":{}}");
    ASSERT(resp_ok());

    uint8_t buf[32768];
    json_value_t result;
    ASSERT(parse_result(&result, buf, sizeof(buf)));
    ASSERT(get_bool(&result, "full") == 1);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Main                                                                      */
/* ----------------------------------------------------------------------- */

int main(void) {
    RUN(test_full_sync_since_zero);
    RUN(test_delta_changed_only);
    RUN(test_delta_tombstones);
    RUN(test_full_sync_tombstone_wrap);
    RUN(test_empty_delta);
    RUN(test_delta_correct_ids);
    RUN(test_full_sync_pagination);
    RUN(test_version_field);
    RUN(test_null_args);
    RUN(test_missing_since_version);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
