#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "ferrum/math/quat.h"
#include "ferrum/math/vec3.h"

/* New module to be implemented (this test is intentionally RED until it exists). */
#include "ferrum/net/quantization.h"

#define ASSERT_TRUE(cond)                                                                                \
    do {                                                                                                 \
        if (!(cond)) {                                                                                   \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                                                          \
    do {                                                                                                 \
        if ((exp) != (act)) {                                                                            \
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d got %d\n", __FILE__, __LINE__,     \
                    (int)(exp), (int)(act));                                                             \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_I32_EQ(exp, act)                                                                          \
    do {                                                                                                 \
        if ((int32_t)(exp) != (int32_t)(act)) {                                                           \
            fprintf(stderr, "ASSERT_I32_EQ failed: %s:%d: expected %d got %d\n", __FILE__, __LINE__,     \
                    (int)(int32_t)(exp), (int)(int32_t)(act));                                           \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_U16_EQ(exp, act)                                                                          \
    do {                                                                                                 \
        if ((uint16_t)(exp) != (uint16_t)(act)) {                                                         \
            fprintf(stderr, "ASSERT_U16_EQ failed: %s:%d: expected %u got %u\n", __FILE__, __LINE__,     \
                    (unsigned)(uint16_t)(exp), (unsigned)(uint16_t)(act));                               \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

static float absf(float x) {
    return (x < 0.0f) ? -x : x;
}

static int assert_float_near(float expected, float actual, float epsilon) {
    const float diff = absf(expected - actual);
    if (diff > epsilon) {
        fprintf(stderr, "ASSERT_FLOAT_NEAR failed: expected=%f actual=%f diff=%f eps=%f\n", expected, actual,
                diff, epsilon);
        return 1;
    }
    return 0;
}

static int assert_vec3_near(vec3_t expected, vec3_t actual, float epsilon) {
    if (assert_float_near(expected.x, actual.x, epsilon) != 0) {
        return 1;
    }
    if (assert_float_near(expected.y, actual.y, epsilon) != 0) {
        return 1;
    }
    if (assert_float_near(expected.z, actual.z, epsilon) != 0) {
        return 1;
    }
    return 0;
}

static float quat_len(quat_t q) {
    return sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
}

static int test_vec3_mm_rounding_halves_away_from_zero(void) {
    net_qvec3_mm_t q;

    ASSERT_INT_EQ(NET_QUANT_OK, net_quantize_vec3_mm((vec3_t){0.0005f, 0.0f, 0.0f}, &q));
    ASSERT_I32_EQ(1, q.x_mm);

    ASSERT_INT_EQ(NET_QUANT_OK, net_quantize_vec3_mm((vec3_t){0.00049f, 0.0f, 0.0f}, &q));
    ASSERT_I32_EQ(0, q.x_mm);

    ASSERT_INT_EQ(NET_QUANT_OK, net_quantize_vec3_mm((vec3_t){-0.0005f, 0.0f, 0.0f}, &q));
    ASSERT_I32_EQ(-1, q.x_mm);

    ASSERT_INT_EQ(NET_QUANT_OK, net_quantize_vec3_mm((vec3_t){-0.00049f, 0.0f, 0.0f}, &q));
    ASSERT_I32_EQ(0, q.x_mm);

    return 0;
}

static int test_vec3_mm_roundtrip_max_error_is_half_mm(void) {
    const float eps = 0.0005f + 1e-7f;

    vec3_t cases[] = {
        {0.0f, 0.0f, 0.0f},
        {1.234567f, -2.345678f, 3.456789f},
        {-123.0004f, 0.0004f, 999.9996f},
        {10.001f, 10.002f, 10.003f},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        net_qvec3_mm_t q;
        vec3_t out;

        ASSERT_INT_EQ(NET_QUANT_OK, net_quantize_vec3_mm(cases[i], &q));
        ASSERT_INT_EQ(NET_QUANT_OK, net_dequantize_vec3_mm(q, &out));
        ASSERT_TRUE(assert_vec3_near(cases[i], out, eps) == 0);
    }

    return 0;
}

static int test_vec3_mm_overflow_is_range_error(void) {
    net_qvec3_mm_t q;

    /* Too large for int32 millimeters. */
    ASSERT_INT_EQ(NET_QUANT_ERR_RANGE, net_quantize_vec3_mm((vec3_t){3.0e9f, 0.0f, 0.0f}, &q));
    ASSERT_INT_EQ(NET_QUANT_ERR_RANGE, net_quantize_vec3_mm((vec3_t){-3.0e9f, 0.0f, 0.0f}, &q));
    return 0;
}

static int test_invalid_args_are_rejected(void) {
    net_qvec3_mm_t q;
    vec3_t v;
    ASSERT_INT_EQ(NET_QUANT_ERR_INVALID, net_quantize_vec3_mm((vec3_t){0}, NULL));
    ASSERT_INT_EQ(NET_QUANT_ERR_INVALID, net_dequantize_vec3_mm(q, NULL));
    ASSERT_INT_EQ(NET_QUANT_ERR_INVALID, net_dequantize_vec3_mm(q, &v));
    return 0;
}

static int test_anim_time_add_wraps_u16(void) {
    ASSERT_U16_EQ(4u, net_anim_time_u16_add_wrap(65530u, 10u));
    ASSERT_U16_EQ(65530u, net_anim_time_u16_add_wrap(65530u, 0u));
    ASSERT_U16_EQ(0u, net_anim_time_u16_add_wrap(0u, 0u));
    return 0;
}

static int test_anim_time_signed_delta_handles_wraparound(void) {
    ASSERT_INT_EQ(11, net_anim_time_u16_delta_signed(5u, 65530u));
    ASSERT_INT_EQ(-11, net_anim_time_u16_delta_signed(65530u, 5u));
    ASSERT_INT_EQ(0, net_anim_time_u16_delta_signed(100u, 100u));
    return 0;
}

static int test_quat_snorm16_canonicalizes_sign(void) {
    quat_t q = (quat_t){0.0f, 0.0f, 0.0f, -1.0f};
    quat_t q_neg = (quat_t){-q.x, -q.y, -q.z, -q.w};

    net_qquat_snorm16_t a;
    net_qquat_snorm16_t b;

    ASSERT_INT_EQ(NET_QUANT_OK, net_quantize_quat_snorm16(q, &a));
    ASSERT_INT_EQ(NET_QUANT_OK, net_quantize_quat_snorm16(q_neg, &b));

    ASSERT_TRUE(a.x == b.x);
    ASSERT_TRUE(a.y == b.y);
    ASSERT_TRUE(a.z == b.z);
    ASSERT_TRUE(a.w == b.w);

    return 0;
}

static int test_quat_snorm16_roundtrip_is_normalized(void) {
    const float eps = 2.5e-4f;

    quat_t q = quat_from_axis_angle((vec3_t){0.0f, 1.0f, 0.0f}, 1.2345f, 1e-6f);

    net_qquat_snorm16_t packed;
    quat_t out;

    ASSERT_INT_EQ(NET_QUANT_OK, net_quantize_quat_snorm16(q, &packed));
    ASSERT_INT_EQ(NET_QUANT_OK, net_dequantize_quat_snorm16(packed, &out));

    ASSERT_TRUE(out.w >= 0.0f);
    ASSERT_TRUE(assert_float_near(1.0f, quat_len(out), eps) == 0);

    /* Compare both q and -q since they represent the same rotation; we canonicalize to w>=0. */
    if (q.w < 0.0f) {
        q = (quat_t){-q.x, -q.y, -q.z, -q.w};
    }

    ASSERT_TRUE(assert_float_near(q.x, out.x, eps) == 0);
    ASSERT_TRUE(assert_float_near(q.y, out.y, eps) == 0);
    ASSERT_TRUE(assert_float_near(q.z, out.z, eps) == 0);
    ASSERT_TRUE(assert_float_near(q.w, out.w, eps) == 0);

    return 0;
}

static int run(const char *name, int (*fn)(void)) {
    printf("RUN %s\n", name);
    int rc = fn();
    if (rc == 0) {
        printf("OK %s\n", name);
    }
    return rc;
}

int main(void) {
    int rc = 0;

    rc = run("vec3_mm_rounding_halves_away_from_zero", test_vec3_mm_rounding_halves_away_from_zero);
    if (rc != 0) {
        return rc;
    }

    rc = run("vec3_mm_roundtrip_max_error_is_half_mm", test_vec3_mm_roundtrip_max_error_is_half_mm);
    if (rc != 0) {
        return rc;
    }

    rc = run("vec3_mm_overflow_is_range_error", test_vec3_mm_overflow_is_range_error);
    if (rc != 0) {
        return rc;
    }

    rc = run("invalid_args_are_rejected", test_invalid_args_are_rejected);
    if (rc != 0) {
        return rc;
    }

    rc = run("anim_time_add_wraps_u16", test_anim_time_add_wraps_u16);
    if (rc != 0) {
        return rc;
    }

    rc = run("anim_time_signed_delta_handles_wraparound", test_anim_time_signed_delta_handles_wraparound);
    if (rc != 0) {
        return rc;
    }

    rc = run("quat_snorm16_canonicalizes_sign", test_quat_snorm16_canonicalizes_sign);
    if (rc != 0) {
        return rc;
    }

    rc = run("quat_snorm16_roundtrip_is_normalized", test_quat_snorm16_roundtrip_is_normalized);
    if (rc != 0) {
        return rc;
    }

    printf("All 8 tests passed\n");
    return 0;
}
