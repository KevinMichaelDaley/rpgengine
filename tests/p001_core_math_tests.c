#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "ferrum/math/constants.h"
#include "ferrum/math/vec2.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/vec4.h"
#include "ferrum/math/quat.h"
#include "ferrum/math/mat4.h"
#include "ferrum/job/system.h"
#include "ferrum/job/counter.h"

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

#define ASSERT_FLOAT_NEAR(exp, act, eps)                                                                 \
    do {                                                                                                 \
        float _e = (float)(exp);                                                                         \
        float _a = (float)(act);                                                                         \
        if (fabsf(_e - _a) > (eps)) {                                                                    \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: expected %f got %f (eps=%f)\n", __FILE__,  \
                    __LINE__, (double)_e, (double)_a, (double)(eps));                                    \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static int float_is_finite(float v) {
    return isfinite((double)v) ? 1 : 0;
}

static int vec3_near(vec3_t a, vec3_t b, float eps) {
    return fabsf(a.x - b.x) <= eps && fabsf(a.y - b.y) <= eps && fabsf(a.z - b.z) <= eps;
}

static int vec4_near(vec4_t a, vec4_t b, float eps) {
    return fabsf(a.x - b.x) <= eps && fabsf(a.y - b.y) <= eps && fabsf(a.z - b.z) <= eps &&
           fabsf(a.w - b.w) <= eps;
}

static int mat4_near(mat4_t a, mat4_t b, float eps) {
    for (int i = 0; i < 16; ++i) {
        if (fabsf(a.m[i] - b.m[i]) > eps) {
            return 0;
        }
    }
    return 1;
}

/* ---------- Tests ---------- */
static int test_translation_mat4_mul_vec4(void) {
    mat4_t t = mat4_translation(10.0f, 0.0f, 0.0f);
    vec4_t p = {0.0f, 0.0f, 0.0f, 1.0f};
    vec4_t r = mat4_mul_vec4(t, p);
    ASSERT_FLOAT_NEAR(10.0f, r.x, 1e-5f);
    ASSERT_FLOAT_NEAR(0.0f, r.y, 1e-5f);
    ASSERT_FLOAT_NEAR(0.0f, r.z, 1e-5f);
    ASSERT_FLOAT_NEAR(1.0f, r.w, 1e-5f);
    return 0;
}

static int test_identity_invariants(void) {
    mat4_t i = mat4_identity();
    vec4_t v = {1.0f, 2.0f, 3.0f, 1.0f};
    vec4_t r = mat4_mul_vec4(i, v);
    ASSERT_TRUE(vec4_near(v, r, 1e-6f));
    mat4_t m = mat4_mul(i, i);
    ASSERT_TRUE(mat4_near(i, m, 1e-6f));
    mat4_t t = mat4_transpose(i);
    ASSERT_TRUE(mat4_near(i, t, 1e-6f));
    return 0;
}

static int test_vec3_dot_matches_magnitude(void) {
    vec3_t v = {2.0f, 3.0f, 6.0f};
    float dot = vec3_dot(v, v);
    float mag = vec3_magnitude(v);
    ASSERT_FLOAT_NEAR(dot, mag * mag, 1e-5f);
    return 0;
}

static int test_vec3_normalize_safe_nonzero(void) {
    vec3_t v = {3.0f, 4.0f, 0.0f};
    vec3_t n = vec3_normalize_safe(v, 1e-6f);
    ASSERT_FLOAT_NEAR(0.6f, n.x, 1e-4f);
    ASSERT_FLOAT_NEAR(0.8f, n.y, 1e-4f);
    ASSERT_FLOAT_NEAR(0.0f, n.z, 1e-4f);
    ASSERT_FLOAT_NEAR(1.0f, vec3_magnitude(n), 1e-4f);
    return 0;
}

static int test_quat_to_mat4_axis_angle_rotates_vec(void) {
    vec3_t axis = {0.0f, 0.0f, 1.0f};
    quat_t q = quat_from_axis_angle(axis, FERRUM_PI_2, 1e-6f);
    mat4_t m;
    ASSERT_INT_EQ(0, quat_to_mat4(q, &m));
    vec4_t p = {1.0f, 0.0f, 0.0f, 1.0f};
    vec4_t r = mat4_mul_vec4(m, p);
    ASSERT_FLOAT_NEAR(0.0f, r.x, 1e-4f);
    ASSERT_FLOAT_NEAR(1.0f, r.y, 1e-4f);
    ASSERT_FLOAT_NEAR(0.0f, r.z, 1e-4f);
    ASSERT_FLOAT_NEAR(1.0f, r.w, 1e-5f);
    return 0;
}

static int test_mat4_multiplication_associative_tolerant(void) {
    mat4_t a = mat4_rotation_x(0.3f);
    mat4_t b = mat4_rotation_y(0.5f);
    mat4_t c = mat4_rotation_z(-0.2f);
    mat4_t lhs = mat4_mul(mat4_mul(a, b), c);
    mat4_t rhs = mat4_mul(a, mat4_mul(b, c));
    ASSERT_TRUE(mat4_near(lhs, rhs, 1e-5f));
    return 0;
}

static int test_vec3_normalize_zero_returns_zero(void) {
    vec3_t zero = {0.0f, 0.0f, 0.0f};
    vec3_t n = vec3_normalize_safe(zero, 1e-6f);
    ASSERT_TRUE(vec3_near(zero, n, 0.0f));
    return 0;
}

static int test_vec3_normalize_epsilon_zero_is_finite(void) {
    vec3_t v = {1.0f, 2.0f, 3.0f};
    vec3_t n = vec3_normalize_safe(v, 0.0f);
    ASSERT_TRUE(float_is_finite(n.x));
    ASSERT_TRUE(float_is_finite(n.y));
    ASSERT_TRUE(float_is_finite(n.z));
    ASSERT_FLOAT_NEAR(1.0f, vec3_magnitude(n), 1e-5f);
    vec3_t zero = vec3_normalize_safe((vec3_t){0.0f, 0.0f, 0.0f}, 0.0f);
    ASSERT_TRUE(vec3_near(zero, (vec3_t){0.0f, 0.0f, 0.0f}, 0.0f));
    return 0;
}

static int test_vec3_cross_basis_identities(void) {
    vec3_t x = {1.0f, 0.0f, 0.0f};
    vec3_t y = {0.0f, 1.0f, 0.0f};
    vec3_t z = {0.0f, 0.0f, 1.0f};
    ASSERT_TRUE(vec3_near(z, vec3_cross(x, y), 1e-6f));
    ASSERT_TRUE(vec3_near((vec3_t){0.0f, 0.0f, -1.0f}, vec3_cross(y, x), 1e-6f));
    ASSERT_TRUE(vec3_near((vec3_t){0.0f, 0.0f, 0.0f}, vec3_cross(x, x), 1e-6f));
    return 0;
}

static int test_look_at_degenerate_rejected(void) {
    vec3_t eye = {0.0f, 0.0f, 0.0f};
    vec3_t up = {0.0f, 1.0f, 0.0f};
    mat4_t out = mat4_identity();
    ASSERT_INT_EQ(-1, mat4_look_at(eye, eye, up, &out));
    for (int i = 0; i < 16; ++i) {
        ASSERT_TRUE(float_is_finite(out.m[i]));
    }
    return 0;
}

static int test_inverse_singular_fails(void) {
    mat4_t zero = {0};
    mat4_t out = mat4_identity();
    ASSERT_INT_EQ(-1, mat4_inverse(zero, &out));
    for (int i = 0; i < 16; ++i) {
        ASSERT_TRUE(float_is_finite(out.m[i]));
    }
    return 0;
}

static int test_perspective_param_validation(void) {
    mat4_t out;
    ASSERT_INT_EQ(-1, mat4_perspective(0.0f, 1.0f, 0.1f, 100.0f, &out));
    ASSERT_INT_EQ(-1, mat4_perspective(FERRUM_PI_2, 0.0f, 0.1f, 100.0f, &out));
    ASSERT_INT_EQ(-1, mat4_perspective(FERRUM_PI_2, 1.0f, -1.0f, 100.0f, &out));
    ASSERT_INT_EQ(-1, mat4_perspective(FERRUM_PI_2, 1.0f, 10.0f, 1.0f, &out));
    return 0;
}

static int test_perspective_regression_indices(void) {
    mat4_t p;
    ASSERT_INT_EQ(0, mat4_perspective(FERRUM_PI_2, 1.0f, 0.1f, 100.0f, &p));
    ASSERT_FLOAT_NEAR(1.0f, p.m[0], 1e-6f);
    ASSERT_FLOAT_NEAR(1.0f, p.m[5], 1e-6f);
    ASSERT_FLOAT_NEAR(-1.002002f, p.m[10], 1e-5f);
    ASSERT_FLOAT_NEAR(-1.0f, p.m[11], 1e-5f);
    ASSERT_FLOAT_NEAR(-0.200200f, p.m[14], 1e-5f);
    return 0;
}

static int test_quat_slerp_endpoints_and_norm(void) {
    quat_t a = quat_normalize_safe((quat_t){0.0f, 0.0f, 0.0f, 1.0f}, 1e-6f);
    quat_t b = quat_from_axis_angle((vec3_t){0.0f, 1.0f, 0.0f}, FERRUM_PI, 1e-6f);
    quat_t t0 = quat_slerp(a, b, 0.0f, 1e-6f);
    quat_t t1 = quat_slerp(a, b, 1.0f, 1e-6f);
    quat_t tmid = quat_slerp(a, b, 0.5f, 1e-6f);
    ASSERT_FLOAT_NEAR(a.w, t0.w, 1e-6f);
    ASSERT_FLOAT_NEAR(b.w, t1.w, 1e-6f);
    float mag = sqrtf(tmid.x * tmid.x + tmid.y * tmid.y + tmid.z * tmid.z + tmid.w * tmid.w);
    ASSERT_FLOAT_NEAR(1.0f, mag, 1e-4f);
    return 0;
}

static int test_look_at_perspective_handedness(void) {
    vec3_t eye = {0.0f, 0.0f, 5.0f};
    vec3_t target = {0.0f, 0.0f, 0.0f};
    vec3_t up = {0.0f, 1.0f, 0.0f};
    mat4_t view;
    ASSERT_INT_EQ(0, mat4_look_at(eye, target, up, &view));
    mat4_t proj;
    ASSERT_INT_EQ(0, mat4_perspective(FERRUM_PI_2, 1.0f, 0.1f, 100.0f, &proj));
    mat4_t vp = mat4_mul(proj, view);
    vec4_t p = {0.0f, 0.0f, 0.0f, 1.0f};
    vec4_t clip = mat4_mul_vec4(vp, p);
    ASSERT_FLOAT_NEAR(0.0f, clip.x, 1e-5f);
    ASSERT_FLOAT_NEAR(0.0f, clip.y, 1e-5f);
    ASSERT_TRUE(clip.w > 0.0f);
    float ndc_z = clip.z / clip.w;
    ASSERT_TRUE(ndc_z > 0.5f && ndc_z < 1.0f);
    return 0;
}

struct math_job_ctx {
    mat4_t *out;
    size_t index;
};

static void math_job_fn(void *user) {
    struct math_job_ctx *ctx = (struct math_job_ctx *)user;
    mat4_t a = mat4_rotation_x(0.1f * (float)ctx->index);
    mat4_t b = mat4_translation((float)ctx->index, -0.5f, 0.25f);
    ctx->out[ctx->index] = mat4_mul(a, b);
}

static int test_math_under_scheduler_load(void) {
    job_system_t sys_; job_system_t* sys=&sys_;
job_system_create_status_t sys_create_status =  job_system_create(sys,2, 64, 64 * 1024, 2048, 0);
    ASSERT_TRUE(sys != NULL);
    ASSERT_INT_EQ(0, job_system_start(sys));

    const size_t count = 32;
    mat4_t results[count];
    struct math_job_ctx contexts[count];
    job_counter_t counter;
    job_counter_init(&counter, 0);
    for (size_t i = 0; i < count; ++i) {
        contexts[i].out = results;
        contexts[i].index = i;
        job_id_t id = job_dispatch(sys, math_job_fn, &contexts[i], 0, &counter);
        ASSERT_TRUE(id != JOB_ID_INVALID);
    }
    ASSERT_INT_EQ(JOB_WAIT_OK, job_wait_counter(&counter, 0));
    for (size_t i = 0; i < count; ++i) {
        mat4_t ref = mat4_mul(mat4_rotation_x(0.1f * (float)i),
                              mat4_translation((float)i, -0.5f, 0.25f));
        ASSERT_TRUE(mat4_near(ref, results[i], 1e-6f));
    }

    job_system_shutdown(sys);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"translation_mat4_mul_vec4", test_translation_mat4_mul_vec4},
    {"identity_invariants", test_identity_invariants},
    {"vec3_dot_matches_magnitude", test_vec3_dot_matches_magnitude},
    {"vec3_normalize_safe_nonzero", test_vec3_normalize_safe_nonzero},
    {"quat_to_mat4_axis_angle_rotates_vec", test_quat_to_mat4_axis_angle_rotates_vec},
    {"mat4_multiplication_associative_tolerant", test_mat4_multiplication_associative_tolerant},
    {"vec3_normalize_zero_returns_zero", test_vec3_normalize_zero_returns_zero},
    {"vec3_normalize_epsilon_zero_is_finite", test_vec3_normalize_epsilon_zero_is_finite},
    {"vec3_cross_basis_identities", test_vec3_cross_basis_identities},
    {"look_at_degenerate_rejected", test_look_at_degenerate_rejected},
    {"inverse_singular_fails", test_inverse_singular_fails},
    {"perspective_param_validation", test_perspective_param_validation},
    {"perspective_regression_indices", test_perspective_regression_indices},
    {"quat_slerp_endpoints_and_norm", test_quat_slerp_endpoints_and_norm},
    {"look_at_perspective_handedness", test_look_at_perspective_handedness},
    {"math_under_scheduler_load", test_math_under_scheduler_load},
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0;
    for (size_t i = 0; i < total; ++i) {
        struct test_case *tc = &TESTS[i];
        printf("RUN %s\n", tc->name);
        int rc = tc->fn();
        if (rc == 0) {
            printf("OK %s\n", tc->name);
            passed++;
        } else {
            fprintf(stderr, "FAIL %s (rc=%d)\n", tc->name, rc);
            break;
        }
    }
    if (passed == total) {
        printf("All %zu tests passed\n", passed);
        return 0;
    }
    return 1;
}
