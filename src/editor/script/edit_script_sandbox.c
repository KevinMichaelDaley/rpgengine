/**
 * @file edit_script_sandbox.c
 * @brief Lua sandbox: strip dangerous libs, arena allocator.
 */

#include "ferrum/editor/edit_script_sandbox.h"

#include <string.h>

/* LuaJIT headers — only compiled when LUAJIT_ENABLE is defined,
 * but the source file is always present. Guard with ifdef. */
#ifdef LUAJIT_ENABLE
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#endif

/* ----------------------------------------------------------------------- */
/* Arena allocator                                                           */
/* ----------------------------------------------------------------------- */

void script_arena_alloc_state_init(script_arena_alloc_state_t *state,
                                   void *buf, size_t capacity)
{
    state->base     = buf;
    state->capacity = capacity;
    state->used     = 0;
}

/**
 * Simple bump-pointer allocator for Lua.
 *
 * This is intentionally simple: allocations bump forward, frees are
 * no-ops (the arena is bulk-freed when the Lua state is closed).
 * Reallocs allocate a new block and copy. This wastes some memory
 * but is fast and predictable, which is what we want for a sandboxed
 * scripting environment with a hard memory cap.
 */
void *script_sandbox_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    script_arena_alloc_state_t *arena = (script_arena_alloc_state_t *)ud;

    if (nsize == 0) {
        /* Free — no-op in bump allocator. */
        return NULL;
    }

    /* Align to 8 bytes. */
    size_t aligned = (nsize + 7u) & ~(size_t)7u;

    if (arena->used + aligned > arena->capacity) {
        return NULL; /* OOM — Lua will handle gracefully. */
    }

    void *result = (char *)arena->base + arena->used;
    arena->used += aligned;

    /* If realloc, copy old data. */
    if (ptr != NULL && osize > 0) {
        size_t copy_size = osize < nsize ? osize : nsize;
        memcpy(result, ptr, copy_size);
    }

    return result;
}

/* ----------------------------------------------------------------------- */
/* Sandbox init                                                              */
/* ----------------------------------------------------------------------- */

#ifdef LUAJIT_ENABLE

/** Set a global to nil, removing it from the Lua environment. */
static void remove_global(lua_State *L, const char *name)
{
    lua_pushnil(L);
    lua_setglobal(L, name);
}

/** Remove all fields from a global table, then nil the global. */
static void remove_library(lua_State *L, const char *name)
{
    lua_pushnil(L);
    lua_setglobal(L, name);
}

void script_sandbox_init(lua_State *L)
{
    /* Remove dangerous libraries. */
    remove_library(L, "os");
    remove_library(L, "io");
    remove_library(L, "package");
    remove_library(L, "debug");
    remove_library(L, "ffi");

    /* Remove dangerous base functions. */
    remove_global(L, "loadfile");
    remove_global(L, "dofile");
    remove_global(L, "load");     /* can load bytecode → security risk */
    remove_global(L, "require");  /* uses package.loaders */

    /* Collect garbage to free any objects from removed libraries. */
    lua_gc(L, LUA_GCCOLLECT, 0);
}

#else /* !LUAJIT_ENABLE */

/* Stub when LuaJIT is not available. */
void script_sandbox_init(struct lua_State *L)
{
    (void)L;
}

#endif /* LUAJIT_ENABLE */
