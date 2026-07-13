/**
 * @file lm_atlas_tests.c
 * @brief Unit tests for lm_atlas (shelf packing + uv remap).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "ferrum/lightmap/lm_atlas.h"

#define ASSERT_TRUE(cond)                                                    \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

/* Do rects i and j overlap (including their placement)? */
static bool overlap(const lm_atlas_rect_t *a, const lm_atlas_rect_t *b)
{
    return !(a->x + a->w <= b->x || b->x + b->w <= a->x ||
             a->y + a->h <= b->y || b->y + b->h <= a->y);
}

/* Happy: many random tiles pack without overlap, inside the atlas, w/ padding. */
static int test_pack_no_overlap(void)
{
    const uint32_t N = 60;
    lm_atlas_rect_t *r = malloc(N * sizeof(*r));
    uint32_t rng = 7u;
    for (uint32_t i = 0; i < N; ++i) {
        rng = rng * 1664525u + 1013904223u;
        r[i].w = 4 + (rng >> 24) % 40;
        rng = rng * 1664525u + 1013904223u;
        r[i].h = 4 + (rng >> 24) % 40;
        r[i].x = r[i].y = 0xFFFFFFFFu;
    }
    lm_atlas_t atlas;
    ASSERT_TRUE(lm_atlas_pack(r, N, 256, 1, &atlas));
    ASSERT_TRUE(atlas.width == 256 && atlas.height > 0);
    for (uint32_t i = 0; i < N; ++i) {
        ASSERT_TRUE(r[i].x + r[i].w <= atlas.width);
        ASSERT_TRUE(r[i].y + r[i].h <= atlas.height);
        for (uint32_t j = i + 1; j < N; ++j)
            ASSERT_TRUE(!overlap(&r[i], &r[j]));
    }
    free(r);
    return 0;
}

/* Remap: a surface-local uv maps into its atlas rect. */
static int test_remap_uv(void)
{
    lm_atlas_rect_t rects[2] = { { 32, 16, 0, 0 }, { 16, 16, 0, 0 } };
    lm_atlas_t atlas;
    ASSERT_TRUE(lm_atlas_pack(rects, 2, 128, 0, &atlas));
    float u, v;
    /* Centre of rect 0. */
    lm_atlas_remap_uv(&rects[0], &atlas, 0.5f, 0.5f, &u, &v);
    float ex = ((float)rects[0].x + 0.5f * 32.0f) / (float)atlas.width;
    ASSERT_TRUE(fabsf(u - ex) < 1e-5f);
    ASSERT_TRUE(u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f);
    return 0;
}

/* Edge: empty set, single tile, and a too-wide tile fails. */
static int test_edges(void)
{
    lm_atlas_t atlas;
    ASSERT_TRUE(lm_atlas_pack(NULL, 0, 64, 1, &atlas));
    ASSERT_TRUE(atlas.height == 0);

    lm_atlas_rect_t one = { 10, 20, 9, 9 };
    ASSERT_TRUE(lm_atlas_pack(&one, 1, 64, 2, &atlas));
    ASSERT_TRUE(one.x == 2 && one.y == 2);
    ASSERT_TRUE(atlas.height == 2 + 20 + 2);

    lm_atlas_rect_t big = { 100, 10, 0, 0 };
    ASSERT_TRUE(!lm_atlas_pack(&big, 1, 64, 1, &atlas));
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    { "pack_no_overlap", test_pack_no_overlap },
    { "remap_uv", test_remap_uv },
    { "edges", test_edges },
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
