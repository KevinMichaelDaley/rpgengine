/**
 * @file luajit_smoke_tests.c
 * @brief Smoke tests for LuaJIT 2.1 integration.
 *
 * Verifies that LuaJIT can be initialized, execute Lua code, return
 * values to C, handle errors, and shut down cleanly.
 *
 * Tests:
 *  1. Create and close a Lua state (no crash, no leak)
 *  2. luaL_dostring evaluates arithmetic
 *  3. luaL_dostring evaluates string concatenation
 *  4. lua_pcall reports syntax error without crashing
 *  5. lua_pcall reports runtime error without crashing
 *  6. luaL_openlibs opens standard libraries
 *  7. C function registered and callable from Lua
 *  8. Instruction count hook fires (budget mechanism)
 *  9. Multiple states are independent
 * 10. LuaJIT version string is present
 */

#ifdef LUAJIT_ENABLE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

/* ---------- test harness ---------- */

static int g_pass = 0;
static int g_fail = 0;

#define TEST(name) \
    do { printf("  TEST %-50s ", #name); } while (0)

#define PASS() \
    do { printf("PASS\n"); g_pass++; return; } while (0)

#define FAIL(msg) \
    do { printf("FAIL: %s\n", msg); g_fail++; return; } while (0)

#define ASSERT(cond, msg) \
    do { if (!(cond)) { FAIL(msg); } } while (0)

/* ---------- tests ---------- */

/**
 * Test 1: Create and close a Lua state without crashing.
 */
static void test_create_close(void) {
    TEST(create_close);
    lua_State *L = luaL_newstate();
    ASSERT(L != NULL, "luaL_newstate returned NULL");
    lua_close(L);
    PASS();
}

/**
 * Test 2: luaL_dostring evaluates arithmetic and returns result.
 */
static void test_eval_arithmetic(void) {
    TEST(eval_arithmetic);
    lua_State *L = luaL_newstate();
    ASSERT(L != NULL, "luaL_newstate returned NULL");

    int err = luaL_dostring(L, "return 2 + 3");
    ASSERT(err == 0, "luaL_dostring failed");
    ASSERT(lua_isnumber(L, -1), "result is not a number");
    double result = lua_tonumber(L, -1);
    ASSERT(result == 5.0, "expected 5.0");
    lua_pop(L, 1);

    lua_close(L);
    PASS();
}

/**
 * Test 3: luaL_dostring evaluates string concatenation.
 */
static void test_eval_string_concat(void) {
    TEST(eval_string_concat);
    lua_State *L = luaL_newstate();
    ASSERT(L != NULL, "luaL_newstate returned NULL");
    luaL_openlibs(L);

    int err = luaL_dostring(L, "return 'hello' .. ' ' .. 'world'");
    ASSERT(err == 0, "luaL_dostring failed");
    ASSERT(lua_isstring(L, -1), "result is not a string");
    const char *s = lua_tostring(L, -1);
    ASSERT(strcmp(s, "hello world") == 0, "expected 'hello world'");
    lua_pop(L, 1);

    lua_close(L);
    PASS();
}

/**
 * Test 4: Syntax error is caught by lua_pcall without crashing.
 */
static void test_syntax_error(void) {
    TEST(syntax_error);
    lua_State *L = luaL_newstate();
    ASSERT(L != NULL, "luaL_newstate returned NULL");

    int err = luaL_dostring(L, "this is not valid lua !@#$");
    ASSERT(err != 0, "expected syntax error");

    /* Error message should be on the stack. */
    ASSERT(lua_isstring(L, -1), "error message not a string");
    const char *msg = lua_tostring(L, -1);
    ASSERT(msg != NULL, "error message is NULL");
    ASSERT(strlen(msg) > 0, "error message is empty");
    lua_pop(L, 1);

    lua_close(L);
    PASS();
}

/**
 * Test 5: Runtime error is caught by lua_pcall without crashing.
 */
static void test_runtime_error(void) {
    TEST(runtime_error);
    lua_State *L = luaL_newstate();
    ASSERT(L != NULL, "luaL_newstate returned NULL");
    luaL_openlibs(L);

    int err = luaL_dostring(L, "error('boom')");
    ASSERT(err != 0, "expected runtime error");

    const char *msg = lua_tostring(L, -1);
    ASSERT(msg != NULL, "error message is NULL");
    ASSERT(strstr(msg, "boom") != NULL, "expected 'boom' in error message");
    lua_pop(L, 1);

    lua_close(L);
    PASS();
}

/**
 * Test 6: luaL_openlibs opens standard libraries (math.pi accessible).
 */
static void test_openlibs(void) {
    TEST(openlibs);
    lua_State *L = luaL_newstate();
    ASSERT(L != NULL, "luaL_newstate returned NULL");
    luaL_openlibs(L);

    int err = luaL_dostring(L, "return math.pi");
    ASSERT(err == 0, "luaL_dostring failed");
    ASSERT(lua_isnumber(L, -1), "math.pi is not a number");
    double pi = lua_tonumber(L, -1);
    ASSERT(pi > 3.14 && pi < 3.15, "math.pi out of range");
    lua_pop(L, 1);

    lua_close(L);
    PASS();
}

/**
 * Test 7: C function registered and callable from Lua.
 */
static int c_add_func(lua_State *L) {
    double a = luaL_checknumber(L, 1);
    double b = luaL_checknumber(L, 2);
    lua_pushnumber(L, a + b);
    return 1;
}

static void test_c_function(void) {
    TEST(c_function);
    lua_State *L = luaL_newstate();
    ASSERT(L != NULL, "luaL_newstate returned NULL");
    luaL_openlibs(L);

    lua_pushcfunction(L, c_add_func);
    lua_setglobal(L, "c_add");

    int err = luaL_dostring(L, "return c_add(10, 20)");
    ASSERT(err == 0, "luaL_dostring failed");
    ASSERT(lua_isnumber(L, -1), "result not a number");
    double result = lua_tonumber(L, -1);
    ASSERT(result == 30.0, "expected 30.0");
    lua_pop(L, 1);

    lua_close(L);
    PASS();
}

/**
 * Test 8: Instruction count hook fires (budget mechanism for sandboxing).
 */
static int g_hook_fired = 0;

static void instruction_hook(lua_State *L, lua_Debug *ar) {
    (void)ar;
    g_hook_fired = 1;
    /* Abort execution by raising an error. */
    luaL_error(L, "instruction budget exceeded");
}

static void test_instruction_hook(void) {
    TEST(instruction_hook);
    g_hook_fired = 0;

    lua_State *L = luaL_newstate();
    ASSERT(L != NULL, "luaL_newstate returned NULL");

    /* Set hook to fire after 10 instructions. */
    lua_sethook(L, instruction_hook, LUA_MASKCOUNT, 10);

    /* Run an infinite loop — hook should abort it. */
    int err = luaL_dostring(L, "local x = 0; while true do x = x + 1 end");
    ASSERT(err != 0, "expected error from hook");
    ASSERT(g_hook_fired == 1, "hook did not fire");

    lua_close(L);
    PASS();
}

/**
 * Test 9: Multiple Lua states are independent.
 */
static void test_multiple_states(void) {
    TEST(multiple_states);
    lua_State *L1 = luaL_newstate();
    lua_State *L2 = luaL_newstate();
    ASSERT(L1 != NULL && L2 != NULL, "luaL_newstate returned NULL");

    /* Set a global in L1 only. */
    (void)luaL_dostring(L1, "my_var = 42");

    /* L2 should not see it. */
    int err = luaL_dostring(L2, "return my_var");
    ASSERT(err == 0, "luaL_dostring failed in L2");
    ASSERT(lua_isnil(L2, -1), "L2 should not see L1's global");
    lua_pop(L2, 1);

    /* L1 should still have it. */
    err = luaL_dostring(L1, "return my_var");
    ASSERT(err == 0, "luaL_dostring failed in L1");
    ASSERT(lua_isnumber(L1, -1), "my_var should be a number in L1");
    double val = lua_tonumber(L1, -1);
    ASSERT(val == 42.0, "expected 42.0");
    lua_pop(L1, 1);

    lua_close(L1);
    lua_close(L2);
    PASS();
}

/**
 * Test 10: LuaJIT version string is present and contains "LuaJIT".
 */
static void test_version_string(void) {
    TEST(version_string);
    lua_State *L = luaL_newstate();
    ASSERT(L != NULL, "luaL_newstate returned NULL");
    luaL_openlibs(L);

    int err = luaL_dostring(L, "return jit.version");
    ASSERT(err == 0, "luaL_dostring failed");
    ASSERT(lua_isstring(L, -1), "jit.version is not a string");
    const char *ver = lua_tostring(L, -1);
    ASSERT(strstr(ver, "LuaJIT") != NULL, "version should contain 'LuaJIT'");
    lua_pop(L, 1);

    lua_close(L);
    PASS();
}

/* ---------- main ---------- */

int main(void) {
    printf("=== LuaJIT Smoke Tests ===\n");

    test_create_close();
    test_eval_arithmetic();
    test_eval_string_concat();
    test_syntax_error();
    test_runtime_error();
    test_openlibs();
    test_c_function();
    test_instruction_hook();
    test_multiple_states();
    test_version_string();

    printf("\n  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}

#else /* !LUAJIT_ENABLE */

#include <stdio.h>

int main(void) {
    printf("=== LuaJIT Smoke Tests ===\n");
    printf("  SKIPPED (LUAJIT_ENABLE not defined)\n");
    return 0;
}

#endif /* LUAJIT_ENABLE */
