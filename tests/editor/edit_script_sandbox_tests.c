/**
 * @file edit_script_sandbox_tests.c
 * @brief Tests for Lua sandbox: library stripping and arena allocator.
 *
 * Verifies that dangerous libraries are removed, safe libraries remain,
 * pcall/xpcall work, coroutines work, and the arena allocator enforces
 * a memory limit.
 */

#ifdef LUAJIT_ENABLE

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

/* We declare the sandbox API directly since the header lives in src/. */
void script_sandbox_init(lua_State *L);

typedef struct script_arena_alloc_state {
    void    *base;
    size_t   capacity;
    size_t   used;
} script_arena_alloc_state_t;

void  script_arena_alloc_state_init(script_arena_alloc_state_t *state,
                                    void *buf, size_t capacity);
void *script_sandbox_alloc(void *ud, void *ptr, size_t osize, size_t nsize);

/* ----------------------------------------------------------------------- */
/* Test macros                                                               */
/* ----------------------------------------------------------------------- */

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

/* Helper: create sandboxed Lua state with default allocator. */
static lua_State *make_sandboxed(void) {
    lua_State *L = luaL_newstate();
    if (!L) return NULL;
    luaL_openlibs(L);
    script_sandbox_init(L);
    return L;
}

/* Helper: run Lua code, return true if it succeeds. */
static bool run_ok(lua_State *L, const char *code) {
    return luaL_dostring(L, code) == 0;
}

/* Helper: run Lua code, return true if it errors. */
static bool run_err(lua_State *L, const char *code) {
    return luaL_dostring(L, code) != 0;
}

/* ----------------------------------------------------------------------- */
/* Dangerous libraries removed                                               */
/* ----------------------------------------------------------------------- */

/** os library is removed. */
static bool test_os_removed(void) {
    lua_State *L = make_sandboxed();
    ASSERT(L);
    ASSERT(run_err(L, "os.execute('echo pwned')"));
    lua_close(L);
    return true;
}

/** io library is removed. */
static bool test_io_removed(void) {
    lua_State *L = make_sandboxed();
    ASSERT(L);
    ASSERT(run_err(L, "io.open('/etc/passwd', 'r')"));
    lua_close(L);
    return true;
}

/** package/require is removed. */
static bool test_package_removed(void) {
    lua_State *L = make_sandboxed();
    ASSERT(L);
    ASSERT(run_err(L, "require('os')"));
    lua_close(L);
    return true;
}

/** debug library is removed. */
static bool test_debug_removed(void) {
    lua_State *L = make_sandboxed();
    ASSERT(L);
    ASSERT(run_err(L, "debug.getinfo(1)"));
    lua_close(L);
    return true;
}

/** ffi library is removed. */
static bool test_ffi_removed(void) {
    lua_State *L = make_sandboxed();
    ASSERT(L);
    ASSERT(run_err(L, "local ffi = require('ffi')"));
    lua_close(L);
    return true;
}

/** loadfile is removed. */
static bool test_loadfile_removed(void) {
    lua_State *L = make_sandboxed();
    ASSERT(L);
    ASSERT(run_err(L, "loadfile('/etc/passwd')()"));
    lua_close(L);
    return true;
}

/** dofile is removed. */
static bool test_dofile_removed(void) {
    lua_State *L = make_sandboxed();
    ASSERT(L);
    ASSERT(run_err(L, "dofile('/etc/passwd')"));
    lua_close(L);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Safe libraries remain                                                     */
/* ----------------------------------------------------------------------- */

/** string library works. */
static bool test_string_works(void) {
    lua_State *L = make_sandboxed();
    ASSERT(L);
    ASSERT(run_ok(L, "assert(string.len('hello') == 5)"));
    lua_close(L);
    return true;
}

/** table library works. */
static bool test_table_works(void) {
    lua_State *L = make_sandboxed();
    ASSERT(L);
    ASSERT(run_ok(L, "local t = {3,1,2}; table.sort(t); assert(t[1] == 1)"));
    lua_close(L);
    return true;
}

/** math library works. */
static bool test_math_works(void) {
    lua_State *L = make_sandboxed();
    ASSERT(L);
    ASSERT(run_ok(L, "assert(math.abs(-5) == 5)"));
    lua_close(L);
    return true;
}

/** coroutine library works: create/resume/yield. */
static bool test_coroutine_works(void) {
    lua_State *L = make_sandboxed();
    ASSERT(L);
    ASSERT(run_ok(L,
        "local co = coroutine.create(function() "
        "  coroutine.yield(42) "
        "end) "
        "local ok, val = coroutine.resume(co) "
        "assert(ok and val == 42)"));
    lua_close(L);
    return true;
}

/** bit library works (LuaJIT bit ops). */
static bool test_bit_works(void) {
    lua_State *L = make_sandboxed();
    ASSERT(L);
    ASSERT(run_ok(L, "assert(bit.band(0xFF, 0x0F) == 0x0F)"));
    lua_close(L);
    return true;
}

/** pcall works (needed for error handling). */
static bool test_pcall_works(void) {
    lua_State *L = make_sandboxed();
    ASSERT(L);
    ASSERT(run_ok(L,
        "local ok, err = pcall(function() error('test') end) "
        "assert(not ok) "
        "assert(string.find(err, 'test'))"));
    lua_close(L);
    return true;
}

/** xpcall works. */
static bool test_xpcall_works(void) {
    lua_State *L = make_sandboxed();
    ASSERT(L);
    ASSERT(run_ok(L,
        "local handler_called = false "
        "xpcall(function() error('boom') end, "
        "  function(e) handler_called = true end) "
        "assert(handler_called)"));
    lua_close(L);
    return true;
}

/** Base functions: type, tostring, tonumber, pairs, ipairs, select. */
static bool test_base_functions(void) {
    lua_State *L = make_sandboxed();
    ASSERT(L);
    ASSERT(run_ok(L,
        "assert(type(42) == 'number') "
        "assert(tostring(42) == '42') "
        "assert(tonumber('42') == 42) "
        "local t = {a=1, b=2}; for k,v in pairs(t) do end "
        "for i,v in ipairs({10,20}) do end "
        "assert(select(2, 'a', 'b', 'c') == 'b')"));
    lua_close(L);
    return true;
}

/** print still works (useful for debugging scripts). */
static bool test_print_works(void) {
    lua_State *L = make_sandboxed();
    ASSERT(L);
    ASSERT(run_ok(L, "print('sandbox test')"));
    lua_close(L);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Arena allocator tests                                                     */
/* ----------------------------------------------------------------------- */

/** Arena allocator: basic alloc and free cycle. */
static bool test_arena_basic(void) {
    char buf[4096];
    script_arena_alloc_state_t state;
    script_arena_alloc_state_init(&state, buf, sizeof(buf));

    /* Allocate some memory. */
    void *p1 = script_sandbox_alloc(&state, NULL, 0, 64);
    ASSERT(p1 != NULL);
    ASSERT(state.used >= 64);

    /* Free it (nsize=0). */
    void *p2 = script_sandbox_alloc(&state, p1, 64, 0);
    ASSERT(p2 == NULL); /* free returns NULL */

    return true;
}

/** Arena allocator: realloc grows allocation. */
static bool test_arena_realloc(void) {
    char buf[8192];
    script_arena_alloc_state_t state;
    script_arena_alloc_state_init(&state, buf, sizeof(buf));

    void *p1 = script_sandbox_alloc(&state, NULL, 0, 32);
    ASSERT(p1 != NULL);
    memset(p1, 0xAA, 32);

    /* Realloc to larger size. */
    void *p2 = script_sandbox_alloc(&state, p1, 32, 128);
    ASSERT(p2 != NULL);
    /* First 32 bytes should be preserved. */
    ASSERT(((unsigned char *)p2)[0] == 0xAA);
    ASSERT(((unsigned char *)p2)[31] == 0xAA);

    return true;
}

/** Arena allocator: capacity limit enforced. */
static bool test_arena_limit(void) {
    char buf[256];
    script_arena_alloc_state_t state;
    script_arena_alloc_state_init(&state, buf, sizeof(buf));

    /* Try to allocate more than capacity. */
    void *p = script_sandbox_alloc(&state, NULL, 0, 512);
    ASSERT(p == NULL);

    return true;
}

/** Arena allocator works with a real Lua state. */
static bool test_arena_with_lua(void) {
    /* 8 MB arena — realistic size. */
    size_t cap = 8 * 1024 * 1024;
    void *arena = malloc(cap);
    ASSERT(arena != NULL);

    script_arena_alloc_state_t state;
    script_arena_alloc_state_init(&state, arena, cap);

    lua_State *L = lua_newstate(script_sandbox_alloc, &state);
    ASSERT(L != NULL);
    luaL_openlibs(L);
    script_sandbox_init(L);

    /* Run some Lua code. */
    ASSERT(run_ok(L, "local t = {}; for i=1,100 do t[i] = i*i end; assert(t[10] == 100)"));

    lua_close(L);
    free(arena);
    return true;
}

/* ----------------------------------------------------------------------- */
/* main                                                                      */
/* ----------------------------------------------------------------------- */

int main(void) {
    /* Dangerous libraries removed */
    RUN(test_os_removed);
    RUN(test_io_removed);
    RUN(test_package_removed);
    RUN(test_debug_removed);
    RUN(test_ffi_removed);
    RUN(test_loadfile_removed);
    RUN(test_dofile_removed);

    /* Safe libraries remain */
    RUN(test_string_works);
    RUN(test_table_works);
    RUN(test_math_works);
    RUN(test_coroutine_works);
    RUN(test_bit_works);
    RUN(test_pcall_works);
    RUN(test_xpcall_works);
    RUN(test_base_functions);
    RUN(test_print_works);

    /* Arena allocator */
    RUN(test_arena_basic);
    RUN(test_arena_realloc);
    RUN(test_arena_limit);
    RUN(test_arena_with_lua);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

#else /* !LUAJIT_ENABLE */

#include <stdio.h>
int main(void) {
    printf("SKIPPED: sandbox tests require LUAJIT=1\n");
    return 0;
}

#endif /* LUAJIT_ENABLE */
