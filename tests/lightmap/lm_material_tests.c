/**
 * @file lm_material_tests.c
 * @brief Unit tests for lm_material (voxel-id -> reflector table).
 */
#include <math.h>
#include <stdio.h>

#include "ferrum/lightmap/lm_material.h"

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
            printf("  FAIL %s:%d: |%.4f - %.4f|\n", __FILE__, __LINE__,      \
                   _e, _a);                                                  \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static vec3_t v3(float x, float y, float z) { return (vec3_t){ x, y, z }; }

static int test_lookup_valid_and_fallback(void)
{
    lm_material_t mats[3] = {
        { v3(0, 0, 0), v3(0, 0, 0) },        /* 0: air (no reflection) */
        { v3(0.6f, 0.5f, 0.4f), v3(0, 0, 0) },/* 1: stone */
        { v3(0.4f, 0.25f, 0.1f), v3(2, 1, 0) },/* 2: emissive lava */
    };
    lm_material_table_t table = { mats, 3, { v3(0.18f, 0.18f, 0.18f), v3(0, 0, 0) } };

    lm_material_t stone = lm_material_lookup(&table, 1);
    ASSERT_FLOAT_NEAR(0.6f, stone.albedo.x, 1e-4f);
    lm_material_t lava = lm_material_lookup(&table, 2);
    ASSERT_FLOAT_NEAR(2.0f, lava.emissive.x, 1e-4f);
    /* Unknown id -> fallback. */
    lm_material_t unk = lm_material_lookup(&table, 99);
    ASSERT_FLOAT_NEAR(0.18f, unk.albedo.x, 1e-4f);
    return 0;
}

static int test_empty_table(void)
{
    lm_material_table_t table = { NULL, 0, { v3(0.5f, 0.5f, 0.5f), v3(0, 0, 0) } };
    lm_material_t m = lm_material_lookup(&table, 0);
    ASSERT_FLOAT_NEAR(0.5f, m.albedo.y, 1e-4f);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    { "lookup_valid_and_fallback", test_lookup_valid_and_fallback },
    { "empty_table", test_empty_table },
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
