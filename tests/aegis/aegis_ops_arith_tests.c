/**
 * @file aegis_ops_arith_tests.c
 * @brief Tests for arithmetic, bitwise, comparison, and conversion ops.
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/aegis/aegis_ops_arith.h"

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

static aegis_register_t make_i32(int32_t v) {
    aegis_register_t r;
    memset(&r, 0, sizeof(r));
    r.i32 = v;
    return r;
}

static aegis_register_t make_f32(float v) {
    aegis_register_t r;
    memset(&r, 0, sizeof(r));
    r.f32 = v;
    return r;
}

/* -- Arithmetic -- */

static bool test_add(void) {
    aegis_register_t a = make_i32(10), b = make_i32(20), dst;
    ASSERT(aegis_op_add(&dst, &a, &b));
    ASSERT_INT_EQ(30, dst.i32);
    return true;
}

static bool test_add_negative(void) {
    aegis_register_t a = make_i32(-5), b = make_i32(3), dst;
    ASSERT(aegis_op_add(&dst, &a, &b));
    ASSERT_INT_EQ(-2, dst.i32);
    return true;
}

static bool test_sub(void) {
    aegis_register_t a = make_i32(50), b = make_i32(30), dst;
    ASSERT(aegis_op_sub(&dst, &a, &b));
    ASSERT_INT_EQ(20, dst.i32);
    return true;
}

static bool test_mul(void) {
    aegis_register_t a = make_i32(7), b = make_i32(6), dst;
    ASSERT(aegis_op_mul(&dst, &a, &b));
    ASSERT_INT_EQ(42, dst.i32);
    return true;
}

static bool test_div(void) {
    aegis_register_t a = make_i32(100), b = make_i32(7), dst;
    ASSERT(aegis_op_div(&dst, &a, &b));
    ASSERT_INT_EQ(14, dst.i32);
    return true;
}

static bool test_div_by_zero(void) {
    aegis_register_t a = make_i32(42), b = make_i32(0), dst;
    ASSERT(!aegis_op_div(&dst, &a, &b));
    return true;
}

static bool test_mod(void) {
    aegis_register_t a = make_i32(17), b = make_i32(5), dst;
    ASSERT(aegis_op_mod(&dst, &a, &b));
    ASSERT_INT_EQ(2, dst.i32);
    return true;
}

static bool test_mod_by_zero(void) {
    aegis_register_t a = make_i32(42), b = make_i32(0), dst;
    ASSERT(!aegis_op_mod(&dst, &a, &b));
    return true;
}

static bool test_neg(void) {
    aegis_register_t a = make_i32(42), dst;
    ASSERT(aegis_op_neg(&dst, &a));
    ASSERT_INT_EQ(-42, dst.i32);
    return true;
}

static bool test_neg_zero(void) {
    aegis_register_t a = make_i32(0), dst;
    ASSERT(aegis_op_neg(&dst, &a));
    ASSERT_INT_EQ(0, dst.i32);
    return true;
}

/* -- Bitwise -- */

static bool test_and(void) {
    aegis_register_t a = make_i32(0xFF00), b = make_i32(0x0FF0), dst;
    aegis_op_and(&dst, &a, &b);
    ASSERT_INT_EQ(0x0F00, (int)dst.u32);
    return true;
}

static bool test_or(void) {
    aegis_register_t a = make_i32(0xFF00), b = make_i32(0x00FF), dst;
    aegis_op_or(&dst, &a, &b);
    ASSERT_INT_EQ(0xFFFF, (int)dst.u32);
    return true;
}

static bool test_xor(void) {
    aegis_register_t a = make_i32(0xAAAA), b = make_i32(0x5555), dst;
    aegis_op_xor(&dst, &a, &b);
    ASSERT_INT_EQ(0xFFFF, (int)dst.u32);
    return true;
}

static bool test_not(void) {
    aegis_register_t a;
    memset(&a, 0, sizeof(a));
    a.u32 = 0x00000000;
    aegis_register_t dst;
    aegis_op_not(&dst, &a);
    ASSERT(dst.u32 == 0xFFFFFFFF);
    return true;
}

/* -- Comparison -- */

static bool test_eq_true(void) {
    aegis_register_t a = make_i32(42), b = make_i32(42), dst;
    aegis_op_eq(&dst, &a, &b);
    ASSERT_INT_EQ(1, (int)dst.u32);
    return true;
}

static bool test_eq_false(void) {
    aegis_register_t a = make_i32(42), b = make_i32(43), dst;
    aegis_op_eq(&dst, &a, &b);
    ASSERT_INT_EQ(0, (int)dst.u32);
    return true;
}

static bool test_ne(void) {
    aegis_register_t a = make_i32(1), b = make_i32(2), dst;
    aegis_op_ne(&dst, &a, &b);
    ASSERT_INT_EQ(1, (int)dst.u32);
    return true;
}

static bool test_lt_true(void) {
    aegis_register_t a = make_i32(-1), b = make_i32(1), dst;
    aegis_op_lt(&dst, &a, &b);
    ASSERT_INT_EQ(1, (int)dst.u32);
    return true;
}

static bool test_lt_false(void) {
    aegis_register_t a = make_i32(5), b = make_i32(3), dst;
    aegis_op_lt(&dst, &a, &b);
    ASSERT_INT_EQ(0, (int)dst.u32);
    return true;
}

static bool test_le(void) {
    aegis_register_t a = make_i32(5), b = make_i32(5), dst;
    aegis_op_le(&dst, &a, &b);
    ASSERT_INT_EQ(1, (int)dst.u32);
    return true;
}

static bool test_gt(void) {
    aegis_register_t a = make_i32(10), b = make_i32(5), dst;
    aegis_op_gt(&dst, &a, &b);
    ASSERT_INT_EQ(1, (int)dst.u32);
    return true;
}

static bool test_ge(void) {
    aegis_register_t a = make_i32(5), b = make_i32(5), dst;
    aegis_op_ge(&dst, &a, &b);
    ASSERT_INT_EQ(1, (int)dst.u32);
    return true;
}

/* -- Conversion -- */

static bool test_i32_to_f32(void) {
    aegis_register_t src = make_i32(42), dst;
    aegis_op_i32_to_f32(&dst, &src);
    ASSERT(fabsf(dst.f32 - 42.0f) < 1e-6f);
    return true;
}

static bool test_f32_to_i32(void) {
    aegis_register_t src = make_f32(3.7f), dst;
    aegis_op_f32_to_i32(&dst, &src);
    ASSERT_INT_EQ(3, dst.i32);
    return true;
}

static bool test_i64_to_f64(void) {
    aegis_register_t src;
    memset(&src, 0, sizeof(src));
    src.i64 = 1000000;
    aegis_register_t dst;
    aegis_op_i64_to_f64(&dst, &src);
    ASSERT(fabs(dst.f64 - 1000000.0) < 1e-6);
    return true;
}

static bool test_f64_to_i64(void) {
    aegis_register_t src;
    memset(&src, 0, sizeof(src));
    src.f64 = 99.9;
    aegis_register_t dst;
    aegis_op_f64_to_i64(&dst, &src);
    ASSERT(dst.i64 == 99);
    return true;
}

static bool test_f64_to_f32(void) {
    aegis_register_t src;
    memset(&src, 0, sizeof(src));
    src.f64 = 3.14;
    aegis_register_t dst;
    aegis_op_f64_to_f32(&dst, &src);
    ASSERT(fabsf(dst.f32 - 3.14f) < 0.001f);
    return true;
}

static bool test_f32_to_f64(void) {
    aegis_register_t src = make_f32(2.5f), dst;
    aegis_op_f32_to_f64(&dst, &src);
    ASSERT(fabs(dst.f64 - 2.5) < 1e-6);
    return true;
}

int main(void) {
    g_pass = 0;
    g_fail = 0;

    printf("=== Aegis Arith/Compare/Convert Tests ===\n\n");

    RUN(test_add);
    RUN(test_add_negative);
    RUN(test_sub);
    RUN(test_mul);
    RUN(test_div);
    RUN(test_div_by_zero);
    RUN(test_mod);
    RUN(test_mod_by_zero);
    RUN(test_neg);
    RUN(test_neg_zero);

    RUN(test_and);
    RUN(test_or);
    RUN(test_xor);
    RUN(test_not);

    RUN(test_eq_true);
    RUN(test_eq_false);
    RUN(test_ne);
    RUN(test_lt_true);
    RUN(test_lt_false);
    RUN(test_le);
    RUN(test_gt);
    RUN(test_ge);

    RUN(test_i32_to_f32);
    RUN(test_f32_to_i32);
    RUN(test_i64_to_f64);
    RUN(test_f64_to_i64);
    RUN(test_f64_to_f32);
    RUN(test_f32_to_f64);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
