/**
 * @file lm_light_tests.c
 * @brief Unit tests for lm_light (point / directional / spot incidence).
 */
#include <math.h>
#include <stdio.h>

#include "ferrum/lightmap/lm_light.h"

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

/* Point light: inverse-square falloff and direction toward the light. */
static int test_point_inverse_square(void)
{
    lm_light_t l = { LM_LIGHT_POINT, v3(0, 0, 0), v3(0, 0, 0),
                     v3(4, 4, 4), 0.0f, 0.0f, 0.0f };
    vec3_t dir, irr;
    float dist;
    ASSERT_TRUE(lm_light_incident(&l, v3(1, 0, 0), &dir, &dist, &irr));
    ASSERT_FLOAT_NEAR(1.0f, dist, 1e-4f);
    ASSERT_FLOAT_NEAR(-1.0f, dir.x, 1e-4f);           /* toward origin */
    ASSERT_FLOAT_NEAR(4.0f, irr.x, 1e-4f);            /* 4 / 1^2 */

    vec3_t dir2, irr2;
    float dist2;
    ASSERT_TRUE(lm_light_incident(&l, v3(2, 0, 0), &dir2, &dist2, &irr2));
    ASSERT_FLOAT_NEAR(1.0f, irr2.x, 1e-4f);           /* 4 / 2^2 = 1 */
    return 0;
}

/* Directional: constant irradiance, direction opposite the emission dir. */
static int test_directional_constant(void)
{
    lm_light_t l = { LM_LIGHT_DIRECTIONAL, v3(0, 0, 0), v3(0, 0, -1),
                     v3(2, 2, 2), 0.0f, 0.0f, 0.0f };
    vec3_t dir, irr;
    float dist;
    ASSERT_TRUE(lm_light_incident(&l, v3(5, 9, 3), &dir, &dist, &irr));
    ASSERT_FLOAT_NEAR(1.0f, dir.z, 1e-4f);            /* toward +z (up to light) */
    ASSERT_FLOAT_NEAR(2.0f, irr.x, 1e-4f);
    vec3_t dir2, irr2;
    float dist2;
    ASSERT_TRUE(lm_light_incident(&l, v3(-100, 50, -7), &dir2, &dist2, &irr2));
    ASSERT_FLOAT_NEAR(2.0f, irr2.x, 1e-4f);           /* no falloff */
    ASSERT_TRUE(dist > 1000.0f);
    return 0;
}

/* Range: beyond the cutoff the light contributes nothing. */
static int test_point_range_cutoff(void)
{
    lm_light_t l = { LM_LIGHT_POINT, v3(0, 0, 0), v3(0, 0, 0),
                     v3(1, 1, 1), 5.0f, 0.0f, 0.0f };
    vec3_t dir, irr;
    float dist;
    ASSERT_TRUE(lm_light_incident(&l, v3(2, 0, 0), &dir, &dist, &irr));
    ASSERT_TRUE(irr.x > 0.0f);
    ASSERT_TRUE(!lm_light_incident(&l, v3(6, 0, 0), &dir, &dist, &irr)); /* past range */
    return 0;
}

/* Spot: bright on-axis, dark outside the outer cone. */
static int test_spot_cone(void)
{
    /* Aims straight down -z from (0,0,5); ~30deg outer, ~20deg inner. */
    lm_light_t l = { LM_LIGHT_SPOT, v3(0, 0, 5), v3(0, 0, -1),
                     v3(10, 10, 10), 0.0f, cosf(0.35f), cosf(0.52f) };
    vec3_t dir, irr;
    float dist;
    /* On-axis point below the light: fully lit. */
    ASSERT_TRUE(lm_light_incident(&l, v3(0, 0, 0), &dir, &dist, &irr));
    ASSERT_TRUE(irr.x > 0.0f);
    /* Far off-axis (beyond the outer cone): no light. */
    ASSERT_TRUE(!lm_light_incident(&l, v3(10, 0, 0), &dir, &dist, &irr));
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    { "point_inverse_square", test_point_inverse_square },
    { "directional_constant", test_directional_constant },
    { "point_range_cutoff", test_point_range_cutoff },
    { "spot_cone", test_spot_cone },
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
