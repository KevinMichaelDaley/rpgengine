/**
 * @file lm_kdtree_tests.c
 * @brief Unit tests for lm_kdtree (3D kd-tree over luxel positions).
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "ferrum/lightmap/lm_kdtree.h"

#define ASSERT_TRUE(cond)                                                    \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static vec3_t v3(float x, float y, float z) { return (vec3_t){ x, y, z }; }

/* Deterministic pseudo-random point cloud. */
static uint32_t rng_state = 0x12345u;
static float frand(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return (float)(rng_state >> 8) / (float)(1u << 24);
}

static uint32_t brute_nearest(const vec3_t *p, uint32_t n, vec3_t q)
{
    uint32_t best = UINT32_MAX;
    float bd = INFINITY;
    for (uint32_t i = 0; i < n; ++i) {
        float d = vec3_distance_sq(p[i], q);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

static uint32_t brute_radius(const vec3_t *p, uint32_t n, vec3_t q, float r)
{
    uint32_t c = 0;
    for (uint32_t i = 0; i < n; ++i)
        if (vec3_distance_sq(p[i], q) <= r * r) ++c;
    return c;
}

static bool make_arena(arena_t *a, size_t bytes)
{
    void *buf = malloc(bytes);
    if (!buf) return false;
    arena_init(a, buf, bytes);
    return true;
}

/* Happy: nearest matches brute force over a random cloud. */
static int test_nearest_matches_brute(void)
{
    const uint32_t N = 500;
    vec3_t *pts = malloc(N * sizeof(*pts));
    for (uint32_t i = 0; i < N; ++i)
        pts[i] = v3(frand() * 10, frand() * 10, frand() * 10);
    arena_t arena;
    ASSERT_TRUE(make_arena(&arena, N * sizeof(lm_kdnode_t) * 4 + 4096));
    lm_kdtree_t tree;
    ASSERT_TRUE(lm_kdtree_build(&tree, pts, N, &arena));
    for (int q = 0; q < 200; ++q) {
        vec3_t query = v3(frand() * 10, frand() * 10, frand() * 10);
        uint32_t got = lm_kdtree_nearest(&tree, query);
        uint32_t exp = brute_nearest(pts, N, query);
        /* accept a tie in distance (equal-distance points) */
        ASSERT_TRUE(vec3_distance_sq(pts[got], query)
                    <= vec3_distance_sq(pts[exp], query) + 1e-5f);
    }
    free(arena.buffer);
    free(pts);
    return 0;
}

/* Happy: radius query returns exactly the in-radius set. */
static int test_radius_matches_brute(void)
{
    const uint32_t N = 400;
    vec3_t *pts = malloc(N * sizeof(*pts));
    for (uint32_t i = 0; i < N; ++i)
        pts[i] = v3(frand() * 8, frand() * 8, frand() * 8);
    arena_t arena;
    ASSERT_TRUE(make_arena(&arena, N * sizeof(lm_kdnode_t) * 4 + 4096));
    lm_kdtree_t tree;
    ASSERT_TRUE(lm_kdtree_build(&tree, pts, N, &arena));
    uint32_t out[N];
    for (int q = 0; q < 100; ++q) {
        vec3_t query = v3(frand() * 8, frand() * 8, frand() * 8);
        float r = 0.5f + frand() * 3.0f;
        uint32_t got = lm_kdtree_radius(&tree, query, r, out, N);
        uint32_t exp = brute_radius(pts, N, query, r);
        ASSERT_TRUE(got == exp);
        for (uint32_t k = 0; k < got; ++k)
            ASSERT_TRUE(vec3_distance_sq(pts[out[k]], query) <= r * r + 1e-4f);
    }
    free(arena.buffer);
    free(pts);
    return 0;
}

/* Edge: single point, and empty tree. */
static int test_degenerate_sizes(void)
{
    vec3_t one = v3(1, 2, 3);
    arena_t arena;
    ASSERT_TRUE(make_arena(&arena, 4096));
    lm_kdtree_t tree;
    ASSERT_TRUE(lm_kdtree_build(&tree, &one, 1, &arena));
    ASSERT_TRUE(lm_kdtree_nearest(&tree, v3(0, 0, 0)) == 0);
    uint32_t out[4];
    ASSERT_TRUE(lm_kdtree_radius(&tree, v3(1, 2, 3), 0.1f, out, 4) == 1);
    ASSERT_TRUE(lm_kdtree_radius(&tree, v3(5, 5, 5), 0.1f, out, 4) == 0);

    lm_kdtree_t empty;
    ASSERT_TRUE(lm_kdtree_build(&empty, &one, 0, &arena));
    ASSERT_TRUE(lm_kdtree_nearest(&empty, v3(0, 0, 0)) == UINT32_MAX);
    ASSERT_TRUE(lm_kdtree_radius(&empty, v3(0, 0, 0), 1.0f, out, 4) == 0);
    free(arena.buffer);
    return 0;
}

/* Edge: radius output capacity clamp still reports the true count. */
static int test_radius_cap_clamp(void)
{
    const uint32_t N = 64;
    vec3_t *pts = malloc(N * sizeof(*pts));
    for (uint32_t i = 0; i < N; ++i)
        pts[i] = v3(frand(), frand(), frand());   /* all within a unit box */
    arena_t arena;
    ASSERT_TRUE(make_arena(&arena, N * sizeof(lm_kdnode_t) * 4 + 4096));
    lm_kdtree_t tree;
    ASSERT_TRUE(lm_kdtree_build(&tree, pts, N, &arena));
    uint32_t out[4];
    uint32_t got = lm_kdtree_radius(&tree, v3(0.5f, 0.5f, 0.5f), 10.0f, out, 4);
    ASSERT_TRUE(got == N);   /* true count, even though only 4 written */
    free(arena.buffer);
    free(pts);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    { "nearest_matches_brute", test_nearest_matches_brute },
    { "radius_matches_brute", test_radius_matches_brute },
    { "degenerate_sizes", test_degenerate_sizes },
    { "radius_cap_clamp", test_radius_cap_clamp },
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
