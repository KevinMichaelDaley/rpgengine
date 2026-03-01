/**
 * @file edit_script_api_log.c
 * @brief engine.log / engine.warn / engine.err — rate-limited logging.
 *
 * Each log call decrements the rate limiter. When exhausted, returns
 * (false, "rate limit exceeded") instead of printing.
 */

#ifdef LUAJIT_ENABLE

#include "ferrum/editor/edit_script_api.h"

#include <stdio.h>
#include "lua.h"
#include "lauxlib.h"

/* Maximum message length accepted from Lua. */
#define SCRIPT_LOG_MAX_MSG 1024

/* Upvalue indices for the log C closures. */
#define UV_LOG_STATE 1

/**
 * Shared implementation for log/warn/err.
 * Checks rate limit, prints with prefix to the given stream.
 */
static int log_impl(lua_State *L, FILE *stream, const char *prefix)
{
    script_log_state_t *state =
        (script_log_state_t *)lua_touserdata(L, lua_upvalueindex(UV_LOG_STATE));

    /* Check rate limit. */
    if (state->remaining == 0) {
        lua_pushboolean(L, 0);
        lua_pushliteral(L, "rate limit exceeded");
        return 2;
    }
    state->remaining--;

    /* Get message (coerce to string). */
    const char *msg = luaL_optstring(L, 1, "");

    /* Length guard. */
    size_t len = 0;
    msg = lua_tolstring(L, 1, &len);
    if (!msg) msg = "";
    if (len > SCRIPT_LOG_MAX_MSG) len = SCRIPT_LOG_MAX_MSG;

    fprintf(stream, "%s%.*s\n", prefix, (int)len, msg);

    lua_pushboolean(L, 1);
    return 1;
}

/** engine.log(msg) → stdout. */
static int l_engine_log(lua_State *L)
{
    return log_impl(L, stdout, "[SCRIPT] ");
}

/** engine.warn(msg) → stderr. */
static int l_engine_warn(lua_State *L)
{
    return log_impl(L, stderr, "[SCRIPT WARN] ");
}

/** engine.err(msg) → stderr. */
static int l_engine_err(lua_State *L)
{
    return log_impl(L, stderr, "[SCRIPT ERR] ");
}

void script_api_register_log(lua_State *L, script_log_state_t *log_state)
{
    /* The engine table is on top of the stack.
     * Each function is a closure with log_state as upvalue. */

    lua_pushlightuserdata(L, log_state);
    lua_pushcclosure(L, l_engine_log, 1);
    lua_setfield(L, -2, "log");

    lua_pushlightuserdata(L, log_state);
    lua_pushcclosure(L, l_engine_warn, 1);
    lua_setfield(L, -2, "warn");

    lua_pushlightuserdata(L, log_state);
    lua_pushcclosure(L, l_engine_err, 1);
    lua_setfield(L, -2, "err");
}

#endif /* LUAJIT_ENABLE */
