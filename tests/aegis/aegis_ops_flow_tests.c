/**
 * @file aegis_ops_flow_tests.c
 * @brief Tests for control flow instructions.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/aegis/aegis_ops_flow.h"

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

#define ASSERT_INT_EQ(a, b) do { \
    if ((int)(a) != (int)(b)) { \
        printf("  ASSERT FAILED: %d != %d (line %d)\n", \
               (int)(a), (int)(b), __LINE__); \
        return false; \
    } \
} while (0)

/* -- jmp -- */

static bool test_jmp_valid(void) {
    uint32_t pc;
    ASSERT(aegis_op_jmp(5, 100, &pc));
    ASSERT_INT_EQ(5, (int)pc);
    return true;
}

static bool test_jmp_oob(void) {
    uint32_t pc;
    ASSERT(!aegis_op_jmp(100, 100, &pc));
    return true;
}

static bool test_jmp_zero(void) {
    uint32_t pc;
    ASSERT(aegis_op_jmp(0, 10, &pc));
    ASSERT_INT_EQ(0, (int)pc);
    return true;
}

/* -- jmp_if -- */

static bool test_jmp_if_truthy(void) {
    aegis_register_t cond;
    memset(&cond, 0, sizeof(cond));
    cond.u32 = 1;
    uint32_t pc;
    ASSERT(aegis_op_jmp_if(&cond, 10, 5, 100, &pc));
    ASSERT_INT_EQ(10, (int)pc);
    return true;
}

static bool test_jmp_if_falsy(void) {
    aegis_register_t cond;
    memset(&cond, 0, sizeof(cond));
    cond.u32 = 0;
    uint32_t pc;
    ASSERT(aegis_op_jmp_if(&cond, 10, 5, 100, &pc));
    /* Should fall through: pc = current + 1 */
    ASSERT_INT_EQ(6, (int)pc);
    return true;
}

static bool test_jmp_if_oob(void) {
    aegis_register_t cond;
    memset(&cond, 0, sizeof(cond));
    cond.u32 = 1;
    uint32_t pc;
    ASSERT(!aegis_op_jmp_if(&cond, 100, 5, 100, &pc));
    return true;
}

/* -- jmp_if_not -- */

static bool test_jmp_if_not_falsy(void) {
    aegis_register_t cond;
    memset(&cond, 0, sizeof(cond));
    cond.u32 = 0;
    uint32_t pc;
    ASSERT(aegis_op_jmp_if_not(&cond, 20, 5, 100, &pc));
    ASSERT_INT_EQ(20, (int)pc);
    return true;
}

static bool test_jmp_if_not_truthy(void) {
    aegis_register_t cond;
    memset(&cond, 0, sizeof(cond));
    cond.u32 = 42;
    uint32_t pc;
    ASSERT(aegis_op_jmp_if_not(&cond, 20, 5, 100, &pc));
    ASSERT_INT_EQ(6, (int)pc);
    return true;
}

/* -- call/ret -- */

static bool test_call_ret(void) {
    uint8_t buf[1024];
    aegis_memory_t mem;
    memset(buf, 0, sizeof(buf));
    aegis_memory_init(&mem, buf, 1024, 0, 512);

    uint32_t pc;
    ASSERT(aegis_op_call(&mem, 10, 50, 100, &pc));
    ASSERT_INT_EQ(50, (int)pc);
    ASSERT_INT_EQ(1, (int)aegis_memory_call_depth(&mem));

    ASSERT(aegis_op_ret(&mem, &pc));
    ASSERT_INT_EQ(11, (int)pc); /* return to call + 1 */
    ASSERT_INT_EQ(0, (int)aegis_memory_call_depth(&mem));
    return true;
}

static bool test_call_oob(void) {
    uint8_t buf[1024];
    aegis_memory_t mem;
    memset(buf, 0, sizeof(buf));
    aegis_memory_init(&mem, buf, 1024, 0, 512);

    uint32_t pc;
    ASSERT(!aegis_op_call(&mem, 10, 200, 100, &pc));
    return true;
}

static bool test_nested_call_ret(void) {
    uint8_t buf[1024];
    aegis_memory_t mem;
    memset(buf, 0, sizeof(buf));
    aegis_memory_init(&mem, buf, 1024, 0, 512);

    uint32_t pc;
    ASSERT(aegis_op_call(&mem, 5, 20, 100, &pc));
    ASSERT_INT_EQ(20, (int)pc);
    ASSERT(aegis_op_call(&mem, 25, 40, 100, &pc));
    ASSERT_INT_EQ(40, (int)pc);
    ASSERT_INT_EQ(2, (int)aegis_memory_call_depth(&mem));

    ASSERT(aegis_op_ret(&mem, &pc));
    ASSERT_INT_EQ(26, (int)pc);
    ASSERT(aegis_op_ret(&mem, &pc));
    ASSERT_INT_EQ(6, (int)pc);
    return true;
}

static bool test_ret_underflow(void) {
    uint8_t buf[1024];
    aegis_memory_t mem;
    memset(buf, 0, sizeof(buf));
    aegis_memory_init(&mem, buf, 1024, 0, 512);

    uint32_t pc;
    ASSERT(!aegis_op_ret(&mem, &pc));
    return true;
}

int main(void) {
    g_pass = 0;
    g_fail = 0;

    printf("=== Aegis Control Flow Tests ===\n\n");

    RUN(test_jmp_valid);
    RUN(test_jmp_oob);
    RUN(test_jmp_zero);
    RUN(test_jmp_if_truthy);
    RUN(test_jmp_if_falsy);
    RUN(test_jmp_if_oob);
    RUN(test_jmp_if_not_falsy);
    RUN(test_jmp_if_not_truthy);
    RUN(test_call_ret);
    RUN(test_call_oob);
    RUN(test_nested_call_ret);
    RUN(test_ret_underflow);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
