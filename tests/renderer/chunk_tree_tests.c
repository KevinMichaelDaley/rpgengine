/*
 * @file chunk_tree_tests.c
 * @brief Unit tests for the adaptive chunk partition (rpg-zw99).
 *
 * The tree subdivides a coarse base grid toward "detail" (building) AABBs so
 * building regions become small leaves (fine voxel at a fixed grid resolution)
 * and flat/empty regions stay large (coarse voxel). These tests cover: uniform
 * fallback (no detail), refinement near a detail box, full coverage (every point
 * lands in exactly one leaf), point->leaf lookup, and the size CONTRAST that is
 * the whole point (a building leaf is much smaller than a flat leaf).
 */
#include "ferrum/renderer/chunk/chunk_tree.h"
#include "ferrum/memory/arena.h"

#include <stdio.h>
#include <math.h>
#include <string.h>

static int g_fail = 0;
#define CHECK(c, m) do { if (!(c)) { fprintf(stderr, "FAIL: %s (%s:%d)\n", m, __FILE__, __LINE__); ++g_fail; } } while (0)

static uint8_t g_buf[16 * 1024 * 1024];

static phys_aabb_t box(float x0, float y0, float z0, float x1, float y1, float z1)
{
    phys_aabb_t b; b.min.x = x0; b.min.y = y0; b.min.z = z0;
    b.max.x = x1; b.max.y = y1; b.max.z = z1; return b;
}

/* No detail -> a plain uniform max_chunk grid (every leaf == max_chunk). */
static void test_uniform_no_detail(void)
{
    arena_t a; arena_init(&a, g_buf, sizeof g_buf);
    chunk_tree_t t;
    CHECK(chunk_tree_build(&t, box(0,0,0, 256,256,256), 32.0f, 256.0f, 2.0f,
                           NULL, NULL, 0, 0.0f, &a), "build no-detail");
    CHECK(chunk_tree_count(&t) == 1u, "256^3 bounds @ 256 max -> 1 leaf");
    phys_aabb_t inner;
    chunk_tree_bounds(&t, 0, &inner, NULL);
    CHECK(fabsf((inner.max.x - inner.min.x) - 256.0f) < 1e-3f, "leaf edge == max_chunk");
}

/* A detail box refines its neighbourhood down toward min_chunk, while corners far
 * from it stay coarse -> the tree has BOTH small and large leaves (the contrast). */
static void test_refines_near_detail(void)
{
    arena_t a; arena_init(&a, g_buf, sizeof g_buf);
    chunk_tree_t t;
    /* One small "building" near a corner of a big flat scene. */
    float dmin[3] = { 10.0f, 0.0f, 10.0f };
    float dmax[3] = { 26.0f, 8.0f, 26.0f };
    CHECK(chunk_tree_build(&t, box(0,0,0, 512,64,512), 32.0f, 256.0f, 2.0f,
                           dmin, dmax, 1, 1.0f, &a), "build with detail");
    CHECK(chunk_tree_count(&t) > 1u, "detail forces subdivision");

    /* Smallest leaf near the building should reach ~min_chunk; largest stays big. */
    float smallest = 1e30f, largest = 0.0f;
    for (uint32_t i = 0; i < chunk_tree_count(&t); ++i) {
        phys_aabb_t in; chunk_tree_bounds(&t, i, &in, NULL);
        float e = in.max.x - in.min.x;
        if (e < smallest) smallest = e;
        if (e > largest) largest = e;
    }
    CHECK(smallest <= 64.0f + 1e-3f, "a building-region leaf is fine (<=64m)");
    CHECK(largest >= 128.0f - 1e-3f, "a flat-region leaf stays coarse (>=128m)");
    CHECK(largest > smallest + 1e-3f, "voxel-size CONTRAST exists (small vs large leaves)");

    /* The leaf under the building centre must be small; a far corner must be big. */
    uint32_t lb = chunk_tree_of_point(&t, 18.0f, 4.0f, 18.0f);
    uint32_t lf = chunk_tree_of_point(&t, 500.0f, 4.0f, 500.0f);
    CHECK(lb != UINT32_MAX && lf != UINT32_MAX, "both points map to leaves");
    phys_aabb_t bb, fb; chunk_tree_bounds(&t, lb, &bb, NULL); chunk_tree_bounds(&t, lf, &fb, NULL);
    CHECK((bb.max.x - bb.min.x) < (fb.max.x - fb.min.x) - 1e-3f,
          "building leaf smaller than a far flat leaf");
}

/* Every point inside bounds maps to exactly one leaf, and that leaf's inner box
 * contains it (full coverage, no gaps). */
static void test_full_coverage(void)
{
    arena_t a; arena_init(&a, g_buf, sizeof g_buf);
    chunk_tree_t t;
    float dmin[3] = { 100.0f, 0.0f, 100.0f };
    float dmax[3] = { 140.0f, 12.0f, 140.0f };
    CHECK(chunk_tree_build(&t, box(0,0,0, 512,64,512), 32.0f, 128.0f, 2.0f,
                           dmin, dmax, 1, 2.0f, &a), "build for coverage");
    int misses = 0, outside = 0;
    for (int gx = 0; gx < 20; ++gx)
        for (int gz = 0; gz < 20; ++gz) {
            float x = 5.0f + (float)gx * 25.0f, y = 6.0f, z = 5.0f + (float)gz * 25.0f;
            uint32_t l = chunk_tree_of_point(&t, x, y, z);
            if (l == UINT32_MAX) { ++outside; continue; }
            phys_aabb_t in; chunk_tree_bounds(&t, l, &in, NULL);
            if (x < in.min.x || x > in.max.x || z < in.min.z) ++misses;
        }
    CHECK(outside == 0, "all in-bounds points map to a leaf");
    CHECK(misses == 0, "each point lies inside its leaf's inner box");
}

/* Edge: min_chunk == max_chunk collapses to a uniform grid regardless of detail. */
static void test_min_eq_max(void)
{
    arena_t a; arena_init(&a, g_buf, sizeof g_buf);
    chunk_tree_t t;
    float dmin[3] = { 0,0,0 }, dmax[3] = { 10,10,10 };
    CHECK(chunk_tree_build(&t, box(0,0,0, 256,256,256), 128.0f, 128.0f, 1.0f,
                           dmin, dmax, 1, 0.0f, &a), "min==max build");
    for (uint32_t i = 0; i < chunk_tree_count(&t); ++i) {
        phys_aabb_t in; chunk_tree_bounds(&t, i, &in, NULL);
        CHECK(fabsf((in.max.x - in.min.x) - 128.0f) < 1e-3f, "all leaves 128m (uniform)");
    }
    CHECK(chunk_tree_count(&t) == 8u, "256^3 @ 128 -> 2x2x2 = 8 leaves");
}

/* Edge: a point outside the base grid returns UINT32_MAX. */
static void test_out_of_bounds(void)
{
    arena_t a; arena_init(&a, g_buf, sizeof g_buf);
    chunk_tree_t t;
    CHECK(chunk_tree_build(&t, box(0,0,0, 128,128,128), 32.0f, 128.0f, 1.0f,
                           NULL, NULL, 0, 0.0f, &a), "build oob");
    CHECK(chunk_tree_of_point(&t, -5.0f, 5.0f, 5.0f) == UINT32_MAX, "neg x outside");
    CHECK(chunk_tree_of_point(&t, 5.0f, 5.0f, 999.0f) == UINT32_MAX, "far z outside");
}

int main(void)
{
    test_uniform_no_detail();
    test_refines_near_detail();
    test_full_coverage();
    test_min_eq_max();
    test_out_of_bounds();
    if (g_fail == 0) printf("chunk_tree_tests: all passed\n");
    return g_fail ? 1 : 0;
}
