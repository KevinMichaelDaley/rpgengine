/**
 * @file aegis_ops_math_tests.c
 * @brief Tests for vector and quaternion math instructions.
 */

#define _GNU_SOURCE
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/aegis/aegis_ops_math.h"

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

#define ASSERT_FLOAT_EQ(a, b) do { \
    if (fabsf((a) - (b)) > 1e-5f) { \
        printf("  ASSERT FAILED: %f != %f (line %d)\n", \
               (double)(a), (double)(b), __LINE__); \
        return false; \
    } \
} while (0)

static aegis_register_t make_vec3(float x, float y, float z) {
    aegis_register_t r;
    memset(&r, 0, sizeof(r));
    r.vec3[0] = x;
    r.vec3[1] = y;
    r.vec3[2] = z;
    return r;
}

static aegis_register_t make_quat(float x, float y, float z, float w) {
    aegis_register_t r;
    memset(&r, 0, sizeof(r));
    r.vec4[0] = x;
    r.vec4[1] = y;
    r.vec4[2] = z;
    r.vec4[3] = w;
    return r;
}

static aegis_register_t make_scalar(float v) {
    aegis_register_t r;
    memset(&r, 0, sizeof(r));
    r.f32 = v;
    return r;
}

/* -- vec3 component-wise -- */

static bool test_vec3_add(void) {
    aegis_register_t a = make_vec3(1, 2, 3), b = make_vec3(4, 5, 6), dst;
    aegis_op_vec3_add(&dst, &a, &b);
    ASSERT_FLOAT_EQ(5.0f, dst.vec3[0]);
    ASSERT_FLOAT_EQ(7.0f, dst.vec3[1]);
    ASSERT_FLOAT_EQ(9.0f, dst.vec3[2]);
    return true;
}

static bool test_vec3_sub(void) {
    aegis_register_t a = make_vec3(10, 20, 30), b = make_vec3(1, 2, 3), dst;
    aegis_op_vec3_sub(&dst, &a, &b);
    ASSERT_FLOAT_EQ(9.0f, dst.vec3[0]);
    ASSERT_FLOAT_EQ(18.0f, dst.vec3[1]);
    ASSERT_FLOAT_EQ(27.0f, dst.vec3[2]);
    return true;
}

static bool test_vec3_mul(void) {
    aegis_register_t a = make_vec3(2, 3, 4), b = make_vec3(5, 6, 7), dst;
    aegis_op_vec3_mul(&dst, &a, &b);
    ASSERT_FLOAT_EQ(10.0f, dst.vec3[0]);
    ASSERT_FLOAT_EQ(18.0f, dst.vec3[1]);
    ASSERT_FLOAT_EQ(28.0f, dst.vec3[2]);
    return true;
}

static bool test_vec3_scale(void) {
    aegis_register_t v = make_vec3(1, 2, 3), s = make_scalar(2.5f), dst;
    aegis_op_vec3_scale(&dst, &v, &s);
    ASSERT_FLOAT_EQ(2.5f, dst.vec3[0]);
    ASSERT_FLOAT_EQ(5.0f, dst.vec3[1]);
    ASSERT_FLOAT_EQ(7.5f, dst.vec3[2]);
    return true;
}

/* -- vec3 reduction -- */

static bool test_vec3_dot(void) {
    aegis_register_t a = make_vec3(1, 2, 3), b = make_vec3(4, 5, 6), dst;
    aegis_op_vec3_dot(&dst, &a, &b);
    /* 1*4 + 2*5 + 3*6 = 32 */
    ASSERT_FLOAT_EQ(32.0f, dst.f32);
    return true;
}

static bool test_vec3_cross(void) {
    aegis_register_t x = make_vec3(1, 0, 0), y = make_vec3(0, 1, 0), dst;
    aegis_op_vec3_cross(&dst, &x, &y);
    /* x × y = z */
    ASSERT_FLOAT_EQ(0.0f, dst.vec3[0]);
    ASSERT_FLOAT_EQ(0.0f, dst.vec3[1]);
    ASSERT_FLOAT_EQ(1.0f, dst.vec3[2]);
    return true;
}

static bool test_vec3_cross_anticommutative(void) {
    aegis_register_t x = make_vec3(1, 0, 0), y = make_vec3(0, 1, 0), dst;
    aegis_op_vec3_cross(&dst, &y, &x);
    /* y × x = -z */
    ASSERT_FLOAT_EQ(0.0f, dst.vec3[0]);
    ASSERT_FLOAT_EQ(0.0f, dst.vec3[1]);
    ASSERT_FLOAT_EQ(-1.0f, dst.vec3[2]);
    return true;
}

static bool test_vec3_len(void) {
    aegis_register_t a = make_vec3(3, 4, 0), dst;
    aegis_op_vec3_len(&dst, &a);
    ASSERT_FLOAT_EQ(5.0f, dst.f32);
    return true;
}

static bool test_vec3_norm(void) {
    aegis_register_t a = make_vec3(3, 4, 0), dst;
    aegis_op_vec3_norm(&dst, &a);
    ASSERT_FLOAT_EQ(0.6f, dst.vec3[0]);
    ASSERT_FLOAT_EQ(0.8f, dst.vec3[1]);
    ASSERT_FLOAT_EQ(0.0f, dst.vec3[2]);
    return true;
}

static bool test_vec3_norm_zero(void) {
    aegis_register_t a = make_vec3(0, 0, 0), dst;
    aegis_op_vec3_norm(&dst, &a);
    /* Zero vector → zero result, no NaN. */
    ASSERT_FLOAT_EQ(0.0f, dst.vec3[0]);
    ASSERT_FLOAT_EQ(0.0f, dst.vec3[1]);
    ASSERT_FLOAT_EQ(0.0f, dst.vec3[2]);
    return true;
}

/* -- quaternion -- */

static bool test_quat_mul_identity(void) {
    /* Identity quaternion: (0, 0, 0, 1) */
    aegis_register_t identity = make_quat(0, 0, 0, 1);
    aegis_register_t q = make_quat(0.5f, 0.5f, 0.5f, 0.5f);
    aegis_register_t dst;
    aegis_op_quat_mul(&dst, &identity, &q);
    ASSERT_FLOAT_EQ(q.vec4[0], dst.vec4[0]);
    ASSERT_FLOAT_EQ(q.vec4[1], dst.vec4[1]);
    ASSERT_FLOAT_EQ(q.vec4[2], dst.vec4[2]);
    ASSERT_FLOAT_EQ(q.vec4[3], dst.vec4[3]);
    return true;
}

static bool test_quat_mul_non_commutative(void) {
    aegis_register_t a = make_quat(1, 0, 0, 1);
    aegis_register_t b = make_quat(0, 1, 0, 1);
    aegis_register_t ab, ba;
    aegis_op_quat_mul(&ab, &a, &b);
    aegis_op_quat_mul(&ba, &b, &a);
    /* Quaternion multiplication is not commutative. */
    bool same = (fabsf(ab.vec4[0] - ba.vec4[0]) < 1e-6f)
             && (fabsf(ab.vec4[1] - ba.vec4[1]) < 1e-6f)
             && (fabsf(ab.vec4[2] - ba.vec4[2]) < 1e-6f)
             && (fabsf(ab.vec4[3] - ba.vec4[3]) < 1e-6f);
    ASSERT(!same);
    return true;
}

static bool test_quat_rotate_identity(void) {
    aegis_register_t identity = make_quat(0, 0, 0, 1);
    aegis_register_t v = make_vec3(1, 2, 3);
    aegis_register_t dst;
    aegis_op_quat_rotate(&dst, &identity, &v);
    ASSERT_FLOAT_EQ(1.0f, dst.vec3[0]);
    ASSERT_FLOAT_EQ(2.0f, dst.vec3[1]);
    ASSERT_FLOAT_EQ(3.0f, dst.vec3[2]);
    return true;
}

static bool test_quat_rotate_90_z(void) {
    /* 90° around Z: q = (0, 0, sin(45°), cos(45°)) */
    float s = sinf((float)M_PI / 4.0f);
    float c = cosf((float)M_PI / 4.0f);
    aegis_register_t q = make_quat(0, 0, s, c);
    aegis_register_t v = make_vec3(1, 0, 0);
    aegis_register_t dst;
    aegis_op_quat_rotate(&dst, &q, &v);
    /* (1,0,0) rotated 90° around Z → (0,1,0) */
    ASSERT_FLOAT_EQ(0.0f, dst.vec3[0]);
    ASSERT_FLOAT_EQ(1.0f, dst.vec3[1]);
    ASSERT_FLOAT_EQ(0.0f, dst.vec3[2]);
    return true;
}

int main(void) {
    g_pass = 0;
    g_fail = 0;

    printf("=== Aegis Vec/Quat Math Tests ===\n\n");

    RUN(test_vec3_add);
    RUN(test_vec3_sub);
    RUN(test_vec3_mul);
    RUN(test_vec3_scale);
    RUN(test_vec3_dot);
    RUN(test_vec3_cross);
    RUN(test_vec3_cross_anticommutative);
    RUN(test_vec3_len);
    RUN(test_vec3_norm);
    RUN(test_vec3_norm_zero);
    RUN(test_quat_mul_identity);
    RUN(test_quat_mul_non_commutative);
    RUN(test_quat_rotate_identity);
    RUN(test_quat_rotate_90_z);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
