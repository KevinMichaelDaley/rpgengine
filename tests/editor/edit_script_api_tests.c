/**
 * @file edit_script_api_tests.c
 * @brief Tests for safe engine API: rate-limited logging, validated entity writes.
 *
 * Verifies that dangerous globals are removed, engine.log/warn/err work with
 * rate limiting, and engine.write_entity validates all inputs and writes to
 * the update blob.
 */

#ifdef LUAJIT_ENABLE

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "ferrum/editor/edit_script_sandbox.h"
#include "ferrum/editor/edit_script_api.h"
#include "ferrum/editor/edit_script_env.h"
#include "ferrum/entity/entity_attrs.h"

/* ------------------------------------------------------------------ */
/* Test harness                                                        */
/* ------------------------------------------------------------------ */

static int g_pass, g_fail;

#define RUN(fn) do { \
    printf("RUN  %s\n", #fn); \
    if (fn()) { printf("OK   %s\n", #fn); g_pass++; } \
    else       { printf("FAIL %s\n", #fn); g_fail++; } \
} while (0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  ASSERT FAILED: %s (line %d)\n", #cond, __LINE__); \
        return false; \
    } \
} while (0)

/* Helper: create a sandboxed Lua state with engine API registered. */
static lua_State *make_state_with_api(script_log_state_t *log_state,
                                      script_env_t *env)
{
    lua_State *L = luaL_newstate();
    if (!L) return NULL;
    luaL_openlibs(L);
    script_sandbox_init(L);
    script_api_register(L, env, log_state);
    return L;
}

static bool run_ok(lua_State *L, const char *code) {
    return luaL_dostring(L, code) == 0;
}

static bool run_err(lua_State *L, const char *code) {
    return luaL_dostring(L, code) != 0;
}

/* ------------------------------------------------------------------ */
/* Dangerous globals removed                                           */
/* ------------------------------------------------------------------ */

/** print() is removed after API registration. */
static bool test_print_removed(void) {
    script_log_state_t log_state;
    script_log_state_init(&log_state, 100);
    script_env_t env;
    memset(&env, 0, sizeof(env));

    lua_State *L = make_state_with_api(&log_state, &env);
    ASSERT(L);
    ASSERT(run_err(L, "print('hello')"));
    lua_close(L);
    return true;
}

/** rawset is removed. */
static bool test_rawset_removed(void) {
    script_log_state_t log_state;
    script_log_state_init(&log_state, 100);
    script_env_t env;
    memset(&env, 0, sizeof(env));

    lua_State *L = make_state_with_api(&log_state, &env);
    ASSERT(L);
    ASSERT(run_err(L, "rawset({}, 'a', 1)"));
    lua_close(L);
    return true;
}

/** rawget is removed. */
static bool test_rawget_removed(void) {
    script_log_state_t log_state;
    script_log_state_init(&log_state, 100);
    script_env_t env;
    memset(&env, 0, sizeof(env));

    lua_State *L = make_state_with_api(&log_state, &env);
    ASSERT(L);
    ASSERT(run_err(L, "rawget({}, 'a')"));
    lua_close(L);
    return true;
}

/** setmetatable is removed. */
static bool test_setmetatable_removed(void) {
    script_log_state_t log_state;
    script_log_state_init(&log_state, 100);
    script_env_t env;
    memset(&env, 0, sizeof(env));

    lua_State *L = make_state_with_api(&log_state, &env);
    ASSERT(L);
    ASSERT(run_err(L, "setmetatable({}, {})"));
    lua_close(L);
    return true;
}

/** getmetatable is removed. */
static bool test_getmetatable_removed(void) {
    script_log_state_t log_state;
    script_log_state_init(&log_state, 100);
    script_env_t env;
    memset(&env, 0, sizeof(env));

    lua_State *L = make_state_with_api(&log_state, &env);
    ASSERT(L);
    ASSERT(run_err(L, "getmetatable('')"));
    lua_close(L);
    return true;
}

/* ------------------------------------------------------------------ */
/* engine.log / engine.warn / engine.err                               */
/* ------------------------------------------------------------------ */

/** engine.log works and returns true. */
static bool test_engine_log_works(void) {
    script_log_state_t log_state;
    script_log_state_init(&log_state, 100);
    script_env_t env;
    memset(&env, 0, sizeof(env));

    lua_State *L = make_state_with_api(&log_state, &env);
    ASSERT(L);
    ASSERT(run_ok(L, "local ok = engine.log('hello from script'); assert(ok == true)"));
    lua_close(L);
    return true;
}

/** engine.warn works. */
static bool test_engine_warn_works(void) {
    script_log_state_t log_state;
    script_log_state_init(&log_state, 100);
    script_env_t env;
    memset(&env, 0, sizeof(env));

    lua_State *L = make_state_with_api(&log_state, &env);
    ASSERT(L);
    ASSERT(run_ok(L, "local ok = engine.warn('warning msg'); assert(ok == true)"));
    lua_close(L);
    return true;
}

/** engine.err works. */
static bool test_engine_err_works(void) {
    script_log_state_t log_state;
    script_log_state_init(&log_state, 100);
    script_env_t env;
    memset(&env, 0, sizeof(env));

    lua_State *L = make_state_with_api(&log_state, &env);
    ASSERT(L);
    ASSERT(run_ok(L, "local ok = engine.err('error msg'); assert(ok == true)"));
    lua_close(L);
    return true;
}

/** Rate limit enforced: 101st call returns false. */
static bool test_rate_limit_enforced(void) {
    script_log_state_t log_state;
    script_log_state_init(&log_state, 10); /* only 10 per tick */
    script_env_t env;
    memset(&env, 0, sizeof(env));

    lua_State *L = make_state_with_api(&log_state, &env);
    ASSERT(L);
    /* First 10 succeed. */
    ASSERT(run_ok(L,
        "for i = 1, 10 do\n"
        "  local ok = engine.log('msg ' .. i)\n"
        "  assert(ok == true, 'call ' .. i .. ' should succeed')\n"
        "end"));
    /* 11th fails. */
    ASSERT(run_ok(L,
        "local ok, msg = engine.log('overflow')\n"
        "assert(ok == false, 'should be rate-limited')\n"
        "assert(type(msg) == 'string')"));
    lua_close(L);
    return true;
}

/** Rate limit resets between ticks. */
static bool test_rate_limit_resets(void) {
    script_log_state_t log_state;
    script_log_state_init(&log_state, 5);
    script_env_t env;
    memset(&env, 0, sizeof(env));

    lua_State *L = make_state_with_api(&log_state, &env);
    ASSERT(L);
    /* Exhaust limit. */
    ASSERT(run_ok(L, "for i=1,5 do engine.log('x') end"));
    ASSERT(run_ok(L,
        "local ok = engine.log('over')\n"
        "assert(ok == false)"));
    /* Reset (simulates new tick). */
    script_log_state_reset(&log_state);
    /* Now works again. */
    ASSERT(run_ok(L, "local ok = engine.log('after reset'); assert(ok == true)"));
    lua_close(L);
    return true;
}

/* ------------------------------------------------------------------ */
/* engine.write_entity                                                 */
/* ------------------------------------------------------------------ */

/** engine.write_entity with valid args writes to update blob. */
static bool test_write_entity_valid(void) {
    script_log_state_t log_state;
    script_log_state_init(&log_state, 100);

    /* Set up env with a real blob. */
    uint8_t blob[4096];
    script_env_t env;
    script_env_init_blob(&env, blob, sizeof(blob));

    /* Set up a minimal snapshot so entity_id=0 is valid. */
    script_entity_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    snap.entity_id = 0;
    snap.active = 1;
    env.entities.entities = &snap;
    env.entities.count = 1;
    env.entities.capacity = 1;

    lua_State *L = make_state_with_api(&log_state, &env);
    ASSERT(L);
    /* Write a float attribute. */
    ASSERT(run_ok(L,
        "local ok = engine.write_entity(0, 256, 'f32', 3.14)\n"
        "assert(ok == true, 'write should succeed')"));
    /* Verify blob has data. */
    ASSERT(env.update_blob_used > 0);
    lua_close(L);
    return true;
}

/** engine.write_entity with invalid entity_id returns false. */
static bool test_write_entity_bad_id(void) {
    script_log_state_t log_state;
    script_log_state_init(&log_state, 100);

    uint8_t blob[4096];
    script_env_t env;
    script_env_init_blob(&env, blob, sizeof(blob));
    env.entities.entities = NULL;
    env.entities.count = 0;
    env.entities.capacity = 0;

    lua_State *L = make_state_with_api(&log_state, &env);
    ASSERT(L);
    ASSERT(run_ok(L,
        "local ok, msg = engine.write_entity(999, 0, 'f32', 1.0)\n"
        "assert(ok == false)\n"
        "assert(type(msg) == 'string')"));
    /* Blob should be empty. */
    ASSERT(env.update_blob_used == 0);
    lua_close(L);
    return true;
}

/** engine.write_entity with invalid type returns false. */
static bool test_write_entity_bad_type(void) {
    script_log_state_t log_state;
    script_log_state_init(&log_state, 100);

    uint8_t blob[4096];
    script_env_t env;
    script_env_init_blob(&env, blob, sizeof(blob));

    script_entity_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    snap.entity_id = 0;
    snap.active = 1;
    env.entities.entities = &snap;
    env.entities.count = 1;
    env.entities.capacity = 1;

    lua_State *L = make_state_with_api(&log_state, &env);
    ASSERT(L);
    ASSERT(run_ok(L,
        "local ok, msg = engine.write_entity(0, 0, 'invalid_type', 1.0)\n"
        "assert(ok == false)"));
    lua_close(L);
    return true;
}

/** engine.write_entity with oversized string returns false. */
static bool test_write_entity_oversized_string(void) {
    script_log_state_t log_state;
    script_log_state_init(&log_state, 100);

    uint8_t blob[4096];
    script_env_t env;
    script_env_init_blob(&env, blob, sizeof(blob));

    script_entity_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    snap.entity_id = 0;
    snap.active = 1;
    env.entities.entities = &snap;
    env.entities.count = 1;
    env.entities.capacity = 1;

    lua_State *L = make_state_with_api(&log_state, &env);
    ASSERT(L);
    ASSERT(run_ok(L,
        "local big = string.rep('x', 300)\n"
        "local ok, msg = engine.write_entity(0, 3, 'str', big)\n"
        "assert(ok == false)"));
    lua_close(L);
    return true;
}

/** engine.write_entity with out-of-range key returns false. */
static bool test_write_entity_bad_key(void) {
    script_log_state_t log_state;
    script_log_state_init(&log_state, 100);

    uint8_t blob[4096];
    script_env_t env;
    script_env_init_blob(&env, blob, sizeof(blob));

    script_entity_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    snap.entity_id = 0;
    snap.active = 1;
    env.entities.entities = &snap;
    env.entities.count = 1;
    env.entities.capacity = 1;

    lua_State *L = make_state_with_api(&log_state, &env);
    ASSERT(L);
    /* Key > 65535 should fail. */
    ASSERT(run_ok(L,
        "local ok, msg = engine.write_entity(0, 70000, 'f32', 1.0)\n"
        "assert(ok == false)"));
    lua_close(L);
    return true;
}

/** engine.write_entity vec3 write appears in blob correctly. */
static bool test_write_entity_vec3(void) {
    script_log_state_t log_state;
    script_log_state_init(&log_state, 100);

    uint8_t blob[4096];
    script_env_t env;
    script_env_init_blob(&env, blob, sizeof(blob));

    script_entity_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    snap.entity_id = 0;
    snap.active = 1;
    env.entities.entities = &snap;
    env.entities.count = 1;
    env.entities.capacity = 1;

    lua_State *L = make_state_with_api(&log_state, &env);
    ASSERT(L);
    ASSERT(run_ok(L,
        "local ok = engine.write_entity(0, 0, 'vec3', {1.0, 2.0, 3.0})\n"
        "assert(ok == true)"));
    ASSERT(env.update_blob_used > 0);
    lua_close(L);
    return true;
}

/** engine.write_entity bool write succeeds. */
static bool test_write_entity_bool(void) {
    script_log_state_t log_state;
    script_log_state_init(&log_state, 100);

    uint8_t blob[4096];
    script_env_t env;
    script_env_init_blob(&env, blob, sizeof(blob));

    script_entity_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    snap.entity_id = 0;
    snap.active = 1;
    env.entities.entities = &snap;
    env.entities.count = 1;
    env.entities.capacity = 1;

    lua_State *L = make_state_with_api(&log_state, &env);
    ASSERT(L);
    ASSERT(run_ok(L,
        "local ok = engine.write_entity(0, 256, 'bool', true)\n"
        "assert(ok == true)"));
    ASSERT(env.update_blob_used > 0);
    lua_close(L);
    return true;
}

/** engine.write_entity i32 write succeeds. */
static bool test_write_entity_i32(void) {
    script_log_state_t log_state;
    script_log_state_init(&log_state, 100);

    uint8_t blob[4096];
    script_env_t env;
    script_env_init_blob(&env, blob, sizeof(blob));

    script_entity_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    snap.entity_id = 0;
    snap.active = 1;
    env.entities.entities = &snap;
    env.entities.count = 1;
    env.entities.capacity = 1;

    lua_State *L = make_state_with_api(&log_state, &env);
    ASSERT(L);
    ASSERT(run_ok(L,
        "local ok = engine.write_entity(0, 256, 'i32', -42)\n"
        "assert(ok == true)"));
    ASSERT(env.update_blob_used > 0);
    lua_close(L);
    return true;
}

/** engine table exists and is a table. */
static bool test_engine_table_exists(void) {
    script_log_state_t log_state;
    script_log_state_init(&log_state, 100);
    script_env_t env;
    memset(&env, 0, sizeof(env));

    lua_State *L = make_state_with_api(&log_state, &env);
    ASSERT(L);
    ASSERT(run_ok(L, "assert(type(engine) == 'table')"));
    ASSERT(run_ok(L, "assert(type(engine.log) == 'function')"));
    ASSERT(run_ok(L, "assert(type(engine.warn) == 'function')"));
    ASSERT(run_ok(L, "assert(type(engine.err) == 'function')"));
    ASSERT(run_ok(L, "assert(type(engine.write_entity) == 'function')"));
    lua_close(L);
    return true;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(void) {
    /* Dangerous globals removed */
    RUN(test_print_removed);
    RUN(test_rawset_removed);
    RUN(test_rawget_removed);
    RUN(test_setmetatable_removed);
    RUN(test_getmetatable_removed);

    /* engine.log / warn / err */
    RUN(test_engine_log_works);
    RUN(test_engine_warn_works);
    RUN(test_engine_err_works);
    RUN(test_rate_limit_enforced);
    RUN(test_rate_limit_resets);

    /* engine.write_entity */
    RUN(test_write_entity_valid);
    RUN(test_write_entity_bad_id);
    RUN(test_write_entity_bad_type);
    RUN(test_write_entity_oversized_string);
    RUN(test_write_entity_bad_key);
    RUN(test_write_entity_vec3);
    RUN(test_write_entity_bool);
    RUN(test_write_entity_i32);

    /* engine table structure */
    RUN(test_engine_table_exists);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

#else /* !LUAJIT_ENABLE */

#include <stdio.h>
int main(void) {
    printf("SKIPPED: API tests require LUAJIT=1\n");
    return 0;
}

#endif /* LUAJIT_ENABLE */
