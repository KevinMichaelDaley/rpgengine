/**
 * @file lm_sh_tests.c
 * @brief Unit tests for lm_sh (order-2 spherical harmonics).
 */
#include <math.h>
#include <stdio.h>

#include "ferrum/lightmap/lm_sh.h"

#define LM_PI 3.14159265358979324f

#define ASSERT_TRUE(cond)                                                    \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, eps)                                     \
    do {                                                                     \
        float _e = (exp), _a = (act);                                        \
        if (fabsf(_e - _a) > (eps)) {                                        \
            printf("  FAIL %s:%d: |%.4f - %.4f| > %.4f\n", __FILE__,         \
                   __LINE__, _e, _a, (float)(eps));                          \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static vec3_t v3(float x, float y, float z) { return (vec3_t){ x, y, z }; }

/* N evenly spread directions on the unit sphere (Fibonacci spiral). */
static vec3_t fib_dir(int i, int n)
{
    const float ga = 2.399963229728653f;      /* golden angle */
    float z = 1.0f - (2.0f * (float)i + 1.0f) / (float)n;
    float r = sqrtf(fmaxf(0.0f, 1.0f - z * z));
    float phi = ga * (float)i;
    return v3(r * cosf(phi), r * sinf(phi), z);
}

/* Basis at +z matches the closed-form real SH values. */
static int test_basis_plus_z(void)
{
    float y[9];
    lm_sh9_basis(v3(0, 0, 1), y);
    ASSERT_FLOAT_NEAR(0.282095f, y[0], 1e-4f);   /* Y00 */
    ASSERT_FLOAT_NEAR(0.0f, y[1], 1e-4f);         /* Y1-1 (y) */
    ASSERT_FLOAT_NEAR(0.488603f, y[2], 1e-4f);   /* Y10 (z) */
    ASSERT_FLOAT_NEAR(0.0f, y[3], 1e-4f);         /* Y11 (x) */
    ASSERT_FLOAT_NEAR(0.315392f * 2.0f, y[6], 1e-4f); /* Y20 (3z^2-1) */
    return 0;
}

/* Uniform radiance L=1 over the whole sphere reconstructs irradiance ~= pi in
 * every normal direction (the analytic Lambertian result). */
static int test_uniform_irradiance_pi(void)
{
    const int N = 8192;
    lm_sh9_t sh;
    lm_sh9_zero(&sh);
    float w = 4.0f * LM_PI / (float)N;      /* solid angle per sample */
    for (int i = 0; i < N; ++i)
        lm_sh9_add_sample(&sh, fib_dir(i, N), 1.0f, w);
    ASSERT_FLOAT_NEAR(LM_PI, lm_sh9_irradiance(&sh, v3(0, 0, 1)), 0.05f);
    ASSERT_FLOAT_NEAR(LM_PI, lm_sh9_irradiance(&sh, v3(1, 0, 0)), 0.05f);
    ASSERT_FLOAT_NEAR(LM_PI, lm_sh9_irradiance(&sh, v3(-1, -1, 0)), 0.05f);
    return 0;
}

/* A single sample from +z lights +z more than -z (a cosine lobe). */
static int test_directional_lobe(void)
{
    lm_sh9_t sh;
    lm_sh9_zero(&sh);
    lm_sh9_add_sample(&sh, v3(0, 0, 1), 5.0f, 1.0f);
    float up = lm_sh9_irradiance(&sh, v3(0, 0, 1));
    float side = lm_sh9_irradiance(&sh, v3(1, 0, 0));
    float down = lm_sh9_irradiance(&sh, v3(0, 0, -1));
    ASSERT_TRUE(up > side);
    ASSERT_TRUE(side > down);
    ASSERT_TRUE(up > 0.0f);
    return 0;
}

/* Zero SH -> zero irradiance; unnormalised normal handled. */
static int test_zero_and_unnormalised(void)
{
    lm_sh9_t sh;
    lm_sh9_zero(&sh);
    ASSERT_FLOAT_NEAR(0.0f, lm_sh9_irradiance(&sh, v3(0, 0, 1)), 1e-6f);
    lm_sh9_add_sample(&sh, v3(0, 0, 7), 2.0f, 1.0f);   /* long dir -> normalised */
    float a = lm_sh9_irradiance(&sh, v3(0, 0, 3));
    float b = lm_sh9_irradiance(&sh, v3(0, 0, 1));
    ASSERT_FLOAT_NEAR(a, b, 1e-4f);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    { "basis_plus_z", test_basis_plus_z },
    { "uniform_irradiance_pi", test_uniform_irradiance_pi },
    { "directional_lobe", test_directional_lobe },
    { "zero_and_unnormalised", test_zero_and_unnormalised },
};

int main(void)
{
    int failed = 0;
    for (size_t i = 0; i < sizeof(TESTS) / sizeof(TESTS[0]); ++i) {
        printf("RUN  %s\n", TESTS[i].name);
        int r = TESTS[i].fn();
        printf(r == 0 ? "OK   %s\n" : "FAIL %s\n", TESTS[i].name);
        failed += (r != 0);
    }
    printf("%s (%d failed)\n", failed ? "FAILED" : "PASSED", failed);
    return failed ? 1 : 0;
}
