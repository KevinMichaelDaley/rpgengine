/**
 * @file lm_lightmap_tests.c
 * @brief Unit tests for lm_lightmap (surface -> luxel grid, SH read-back).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "ferrum/lightmap/lm_lightmap.h"

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

static bool make_arena(arena_t *a, size_t bytes)
{
    void *buf = malloc(bytes);
    if (!buf) return false;
    arena_init(a, buf, bytes);
    return true;
}

/* A 4 x 4 m floor at z=0 in the xy plane, normal +z. */
static lm_surface_t floor_surface(uint32_t ru, uint32_t rv)
{
    lm_surface_t s;
    s.origin = v3(0, 0, 0);
    s.edge_u = v3(4, 0, 0);
    s.edge_v = v3(0, 4, 0);
    s.normal = v3(0, 0, 1);
    s.albedo = v3(0.8f, 0.8f, 0.8f);
    s.emissive = v3(0, 0, 0);
    s.res_u = ru;
    s.res_v = rv;
    return s;
}

/* Happy: extraction fills a res_u*res_v grid with luxel centres and normals. */
static int test_extract_grid(void)
{
    lm_surface_t s = floor_surface(4, 4);
    arena_t arena;
    ASSERT_TRUE(make_arena(&arena, 64 * sizeof(lm_luxel_t) + 4096));
    lm_lightmap_t lm;
    ASSERT_TRUE(lm_lightmap_from_surface(&lm, &s, &arena));
    ASSERT_TRUE(lm.res_u == 4 && lm.res_v == 4);

    /* First luxel centre = origin + half a cell along each edge. */
    lm_luxel_t *first = lm_lightmap_at(&lm, 0, 0);
    ASSERT_FLOAT_NEAR(0.5f, first->pos.x, 1e-4f);   /* 4m/4 * 0.5 = 0.5 */
    ASSERT_FLOAT_NEAR(0.5f, first->pos.y, 1e-4f);
    ASSERT_FLOAT_NEAR(0.0f, first->pos.z, 1e-4f);
    ASSERT_FLOAT_NEAR(1.0f, first->normal.z, 1e-4f);

    /* Opposite corner luxel centre = 3.5, 3.5. */
    lm_luxel_t *last = lm_lightmap_at(&lm, 3, 3);
    ASSERT_FLOAT_NEAR(3.5f, last->pos.x, 1e-4f);
    ASSERT_FLOAT_NEAR(3.5f, last->pos.y, 1e-4f);
    ASSERT_FLOAT_NEAR(0.8f, last->albedo.x, 1e-4f);
    free(arena.buffer);
    return 0;
}

/* Read-back: a uniform incident-radiance SH gives a uniform, non-negative image
 * (~pi per channel for unit radiance). */
static int test_readback(void)
{
    lm_surface_t s = floor_surface(2, 2);
    arena_t arena;
    ASSERT_TRUE(make_arena(&arena, 16 * sizeof(lm_luxel_t) + 4096));
    lm_lightmap_t lm;
    ASSERT_TRUE(lm_lightmap_from_surface(&lm, &s, &arena));
    /* Seed every luxel with uniform radiance projected over the sphere. */
    const int N = 4096;
    for (uint32_t i = 0; i < lm.res_u * lm.res_v; ++i) {
        for (int j = 0; j < N; ++j) {
            float z = 1.0f - (2.0f * j + 1.0f) / N;
            float r = sqrtf(fmaxf(0.0f, 1.0f - z * z));
            float phi = 2.399963f * j;
            vec3_t d = v3(r * cosf(phi), r * sinf(phi), z);
            float w = 4.0f * 3.14159265f / N;
            for (int c = 0; c < 3; ++c)
                lm_sh9_add_sample(&lm.luxels[i].sh[c], d, 1.0f, w);
        }
    }
    float rgb[2 * 2 * 3];
    lm_lightmap_readback(&lm, rgb);
    for (int i = 0; i < 2 * 2 * 3; ++i)
        ASSERT_FLOAT_NEAR(3.14159265f, rgb[i], 0.05f);
    free(arena.buffer);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    { "extract_grid", test_extract_grid },
    { "readback", test_readback },
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
