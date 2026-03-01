/**
 * @file aegis_ops_data_tests.c
 * @brief Tests for data movement instructions.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/aegis/aegis_ops_data.h"

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

/* -- mov -- */

static bool test_mov(void) {
    aegis_register_t src, dst;
    memset(&src, 0, sizeof(src));
    memset(&dst, 0xFF, sizeof(dst));
    src.i32 = 42;
    src.vec3[1] = 1.5f;
    aegis_op_mov(&dst, &src);
    ASSERT_INT_EQ(42, dst.i32);
    ASSERT(dst.vec3[1] == 1.5f);
    return true;
}

static bool test_mov_full_copy(void) {
    aegis_register_t src, dst;
    memset(&src, 0xAB, sizeof(src));
    memset(&dst, 0, sizeof(dst));
    aegis_op_mov(&dst, &src);
    ASSERT(memcmp(&src, &dst, sizeof(src)) == 0);
    return true;
}

/* -- load_imm -- */

static bool test_load_imm(void) {
    aegis_register_t dst;
    memset(&dst, 0xFF, sizeof(dst));
    aegis_op_load_imm(&dst, 42);
    ASSERT_INT_EQ(42, (int)dst.u32);
    /* Upper bits should be zeroed. */
    ASSERT(dst.u64 == 42);
    return true;
}

static bool test_load_imm_zero(void) {
    aegis_register_t dst;
    memset(&dst, 0xFF, sizeof(dst));
    aegis_op_load_imm(&dst, 0);
    ASSERT_INT_EQ(0, (int)dst.u32);
    return true;
}

static bool test_load_imm_max(void) {
    aegis_register_t dst;
    aegis_op_load_imm(&dst, 0xFFFFFFFF);
    ASSERT(dst.u32 == 0xFFFFFFFF);
    return true;
}

/* -- load_imm64 -- */

static bool test_load_imm64(void) {
    aegis_register_t dst;
    memset(&dst, 0xFF, sizeof(dst));
    aegis_op_load_imm64(&dst, 0xDEADBEEF, 0x12345678);
    ASSERT(dst.u64 == 0x12345678DEADBEEFULL);
    return true;
}

static bool test_load_imm64_zero(void) {
    aegis_register_t dst;
    memset(&dst, 0xFF, sizeof(dst));
    aegis_op_load_imm64(&dst, 0, 0);
    ASSERT(dst.u64 == 0);
    return true;
}

int main(void) {
    g_pass = 0;
    g_fail = 0;

    printf("=== Aegis Data Movement Tests ===\n\n");

    RUN(test_mov);
    RUN(test_mov_full_copy);
    RUN(test_load_imm);
    RUN(test_load_imm_zero);
    RUN(test_load_imm_max);
    RUN(test_load_imm64);
    RUN(test_load_imm64_zero);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
