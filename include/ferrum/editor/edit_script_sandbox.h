/**
 * @file edit_script_sandbox.h
 * @brief Lua sandbox: library stripping and arena allocator.
 *
 * Call script_sandbox_init(L) after luaL_openlibs() to remove dangerous
 * libraries (os, io, package, debug, ffi) and unsafe base functions
 * (loadfile, dofile). Safe libraries remain: base, string, table, math,
 * coroutine, bit.
 *
 * The arena allocator (script_sandbox_alloc) provides a bump-pointer
 * allocator backed by a fixed buffer, enforcing a memory limit for
 * the Lua state.
 *
 * Thread safety: Lua states are single-threaded. The arena is not
 * shared across states.
 */
#ifndef FERRUM_EDITOR_EDIT_SCRIPT_SANDBOX_H
#define FERRUM_EDITOR_EDIT_SCRIPT_SANDBOX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/* Forward declare lua_State to avoid requiring lua.h in callers. */
struct lua_State;

/**
 * @brief Arena allocator state for Lua.
 *
 * Ownership: the caller owns the backing buffer (base).
 * The state just tracks usage within it.
 */
typedef struct script_arena_alloc_state {
    void   *base;      /**< Start of arena buffer. */
    size_t  capacity;  /**< Total bytes available. */
    size_t  used;      /**< Bytes currently allocated. */
} script_arena_alloc_state_t;

/**
 * @brief Initialize arena allocator state.
 * @param state     State to initialize.
 * @param buf       Backing buffer (caller-owned, must outlive Lua state).
 * @param capacity  Size of buffer in bytes.
 */
void script_arena_alloc_state_init(script_arena_alloc_state_t *state,
                                   void *buf, size_t capacity);

/**
 * @brief Lua-compatible allocator backed by a fixed arena.
 *
 * Conforms to the lua_Alloc signature:
 *   void *(*lua_Alloc)(void *ud, void *ptr, size_t osize, size_t nsize)
 *
 * - nsize == 0: free (returns NULL)
 * - ptr == NULL: allocate nsize bytes
 * - otherwise: realloc from osize to nsize
 *
 * Returns NULL if the arena is exhausted (Lua treats this as OOM).
 *
 * @param ud     Pointer to script_arena_alloc_state_t.
 * @param ptr    Previous allocation (NULL for new alloc).
 * @param osize  Old size.
 * @param nsize  New size (0 to free).
 * @return Pointer to allocation, or NULL on free/OOM.
 */
void *script_sandbox_alloc(void *ud, void *ptr, size_t osize, size_t nsize);

/**
 * @brief Strip dangerous libraries from a Lua state.
 *
 * Must be called AFTER luaL_openlibs(). Removes: os, io, package,
 * debug, ffi, and unsafe base functions (loadfile, dofile).
 *
 * Keeps: base (print, pcall, xpcall, type, tostring, tonumber, error,
 * assert, pairs, ipairs, select, rawget, rawset, rawequal, rawlen,
 * setmetatable, getmetatable, next, unpack), string, table, math,
 * coroutine, bit.
 *
 * @param L  Lua state (must have libs opened).
 *
 * Side effects: sets global variables to nil.
 */
void script_sandbox_init(struct lua_State *L);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_EDIT_SCRIPT_SANDBOX_H */
