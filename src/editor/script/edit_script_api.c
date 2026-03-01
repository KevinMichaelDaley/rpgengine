/**
 * @file edit_script_api.c
 * @brief Engine API registration and log state lifecycle.
 *
 * Creates the global "engine" table, registers log and entity write
 * functions, and strips additional dangerous Lua globals.
 */

#include "ferrum/editor/edit_script_api.h"

#ifdef LUAJIT_ENABLE

#include "lua.h"
#include "lauxlib.h"

/* Defined in edit_script_api_log.c */
extern void script_api_register_log(lua_State *L, script_log_state_t *log_state);

/* Defined in edit_script_api_entity.c */
struct script_env;
extern void script_api_register_entity(lua_State *L, struct script_env *env);

/** Remove a global by setting it to nil. */
static void strip_global(lua_State *L, const char *name)
{
    lua_pushnil(L);
    lua_setglobal(L, name);
}

void script_log_state_init(script_log_state_t *state, uint32_t max_per_tick)
{
    state->remaining    = max_per_tick;
    state->max_per_tick = max_per_tick;
}

void script_log_state_reset(script_log_state_t *state)
{
    state->remaining = state->max_per_tick;
}

void script_api_register(lua_State *L, struct script_env *env,
                         script_log_state_t *log_state)
{
    /* Strip additional dangerous globals beyond what sandbox_init removes. */
    strip_global(L, "print");
    strip_global(L, "rawset");
    strip_global(L, "rawget");
    strip_global(L, "setmetatable");
    strip_global(L, "getmetatable");

    /* Create the global "engine" table. */
    lua_newtable(L);

    /* Register log functions (engine.log, engine.warn, engine.err). */
    script_api_register_log(L, log_state);

    /* Register entity write (engine.write_entity). */
    script_api_register_entity(L, env);

    /* Set the table as the global "engine". */
    lua_setglobal(L, "engine");
}

#else /* !LUAJIT_ENABLE */

void script_log_state_init(script_log_state_t *state, uint32_t max_per_tick)
{
    state->remaining    = max_per_tick;
    state->max_per_tick = max_per_tick;
}

void script_log_state_reset(script_log_state_t *state)
{
    state->remaining = state->max_per_tick;
}

void script_api_register(struct lua_State *L, struct script_env *env,
                         script_log_state_t *log_state)
{
    (void)L; (void)env; (void)log_state;
}

#endif /* LUAJIT_ENABLE */
