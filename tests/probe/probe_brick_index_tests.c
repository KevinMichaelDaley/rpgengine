/**
 * @file probe_brick_index_tests.c
 * @brief Unit tests for the brick index builder (rpg-pjkb, feature 3).
 *        Written before the implementation (TDD phase 1).
 *
 * The index is the runtime indirection of the survey plan: a dense voxel grid
 * at FINEST-brick granularity over the placement AABB, each voxel holding the
 * id of the brick that shades it, finer bricks pre-splatted OVER coarser ones
 * (so sampling is one fetch -- no hierarchy walk). -1 marks uncovered voxels.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/memory/arena.h"
#include "ferrum/probe/probe_set.h"
#include "ferrum/probe/place/probe_brick.h"
#include "ferrum/probe/place/probe_brick_index.h"

#define ASSERT_TRUE(e) do { if (!(e)) { fprintf(stderr, \
    "  ASSERT_TRUE failed: %s (%s:%d)\n", #e, __FILE__, __LINE__); return 1; } } while (0)
#define ASSERT_FALSE(e) ASSERT_TRUE(!(e))
#define ASSERT_INT_EQ(a,b) do { long _a=(long)(a),_b=(long)(b); if (_a!=_b) { \
    fprintf(stderr,"  ASSERT_INT_EQ failed: %ld != %ld (%s:%d)\n",_a,_b,__FILE__,__LINE__); \
    return 1; } } while (0)

static uint8_t g_buf[16 * 1024 * 1024];

static float sdf_empty(const float p[3], void *user)
{
    (void)p; (void)user;
    return 1e9f;
}

static float sdf_floor(const float p[3], void *user)
{
    (void)user;
    return p[1];
}

/* Look up the voxel containing world point p (test-side helper). */
static int32_t index_at(const probe_brick_index_t *ix, const float p[3])
{
    int v[3];
    for (int a = 0; a < 3; ++a) {
        v[a] = (int)floorf((p[a] - ix->origin[a]) / ix->voxel);
        if (v[a] < 0 || v[a] >= ix->dim[a]) return -1;
    }
    return ix->brick_of[(v[2] * ix->dim[1] + v[1]) * ix->dim[0] + v[0]];
}

/* One coarse brick, one level: every voxel maps to brick 0. */
static int test_single_brick_full_cover(void)
{
    arena_t a; arena_init(&a, g_buf, sizeof g_buf);
    probe_brick_config_t cfg; memset(&cfg, 0, sizeof cfg);
    cfg.aabb_max[0] = cfg.aabb_max[1] = cfg.aabb_max[2] = 9.0f;
    cfg.coarse_brick = 9.0f; cfg.levels = 1; cfg.fill_empty = 1;
    cfg.sdf = sdf_empty;
    probe_set_t set; probe_brick_t *bricks; uint32_t nb;
    ASSERT_TRUE(probe_brick_place(&cfg, &a, &set, &bricks, &nb));
    probe_brick_index_t ix;
    ASSERT_TRUE(probe_brick_index_build(&cfg, bricks, nb, &a, &ix));
    ASSERT_INT_EQ(ix.dim[0], 1); ASSERT_INT_EQ(ix.dim[1], 1); ASSERT_INT_EQ(ix.dim[2], 1);
    ASSERT_INT_EQ(ix.brick_of[0], 0);
    return 0;
}

/* Floor scene, 2 levels: voxels near the floor resolve to level-1 bricks,
 * voxels of covered-but-not-refined space resolve to their level-0 brick, and
 * every returned id's brick box actually CONTAINS the query point. */
static int test_finer_overwrites_coarser(void)
{
    arena_t a; arena_init(&a, g_buf, sizeof g_buf);
    probe_brick_config_t cfg; memset(&cfg, 0, sizeof cfg);
    cfg.aabb_max[0] = 9.0f; cfg.aabb_max[1] = 9.0f; cfg.aabb_max[2] = 9.0f;
    cfg.coarse_brick = 9.0f; cfg.levels = 2; cfg.fill_empty = 1;
    cfg.sdf = sdf_floor;
    probe_set_t set; probe_brick_t *bricks; uint32_t nb;
    ASSERT_TRUE(probe_brick_place(&cfg, &a, &set, &bricks, &nb));
    ASSERT_TRUE(nb > 1);
    probe_brick_index_t ix;
    ASSERT_TRUE(probe_brick_index_build(&cfg, bricks, nb, &a, &ix));
    ASSERT_INT_EQ(ix.dim[0], 3); /* 1 coarse cell * 3^(levels-1). */

    float low[3] = { 1.0f, 0.5f, 1.0f };   /* floor band: must be level 1. */
    int32_t bl = index_at(&ix, low);
    ASSERT_TRUE(bl >= 0 && (uint32_t)bl < nb);
    ASSERT_INT_EQ(bricks[bl].level, 1);

    float high[3] = { 1.0f, 7.5f, 1.0f };  /* high air: the coarse brick. */
    int32_t bh = index_at(&ix, high);
    ASSERT_TRUE(bh >= 0 && (uint32_t)bh < nb);
    ASSERT_INT_EQ(bricks[bh].level, 0);

    /* Containment: sample a grid of points; any hit brick must contain them. */
    for (float y = 0.5f; y < 9.0f; y += 1.0f)
        for (float x = 0.5f; x < 9.0f; x += 2.0f) {
            float p[3] = { x, y, 4.5f };
            int32_t b = index_at(&ix, p);
            if (b < 0) continue;
            for (int ax = 0; ax < 3; ++ax) {
                ASSERT_TRUE(p[ax] >= bricks[b].min[ax] - 1e-4f);
                ASSERT_TRUE(p[ax] <= bricks[b].min[ax] + bricks[b].size + 1e-4f);
            }
        }
    return 0;
}

/* Without fill_empty, high-air voxels are uncovered -> -1. */
static int test_uncovered_is_minus_one(void)
{
    arena_t a; arena_init(&a, g_buf, sizeof g_buf);
    probe_brick_config_t cfg; memset(&cfg, 0, sizeof cfg);
    cfg.aabb_max[0] = 9.0f; cfg.aabb_max[1] = 18.0f; cfg.aabb_max[2] = 9.0f;
    cfg.coarse_brick = 9.0f; cfg.levels = 2; cfg.fill_empty = 0;
    cfg.sdf = sdf_floor;
    probe_set_t set; probe_brick_t *bricks; uint32_t nb;
    ASSERT_TRUE(probe_brick_place(&cfg, &a, &set, &bricks, &nb));
    probe_brick_index_t ix;
    ASSERT_TRUE(probe_brick_index_build(&cfg, bricks, nb, &a, &ix));
    float high[3] = { 4.5f, 16.0f, 4.5f };  /* second coarse cell: pure air. */
    ASSERT_INT_EQ(index_at(&ix, high), -1);
    float low[3] = { 4.5f, 0.5f, 4.5f };
    ASSERT_TRUE(index_at(&ix, low) >= 0);
    return 0;
}

/* Zero bricks: a valid all -1 index. */
static int test_zero_bricks(void)
{
    arena_t a; arena_init(&a, g_buf, sizeof g_buf);
    probe_brick_config_t cfg; memset(&cfg, 0, sizeof cfg);
    cfg.aabb_max[0] = cfg.aabb_max[1] = cfg.aabb_max[2] = 9.0f;
    cfg.coarse_brick = 9.0f; cfg.levels = 1; cfg.fill_empty = 0;
    cfg.sdf = sdf_empty;
    probe_brick_index_t ix;
    ASSERT_TRUE(probe_brick_index_build(&cfg, NULL, 0, &a, &ix));
    ASSERT_INT_EQ(ix.brick_of[0], -1);
    return 0;
}

static int test_null_and_exhaustion(void)
{
    arena_t a; arena_init(&a, g_buf, sizeof g_buf);
    probe_brick_config_t cfg; memset(&cfg, 0, sizeof cfg);
    cfg.aabb_max[0] = cfg.aabb_max[1] = cfg.aabb_max[2] = 9.0f;
    cfg.coarse_brick = 9.0f; cfg.levels = 1; cfg.sdf = sdf_empty;
    probe_brick_index_t ix;
    ASSERT_FALSE(probe_brick_index_build(NULL, NULL, 0, &a, &ix));
    ASSERT_FALSE(probe_brick_index_build(&cfg, NULL, 0, NULL, &ix));
    ASSERT_FALSE(probe_brick_index_build(&cfg, NULL, 0, &a, NULL));
    static uint8_t tiny[16];
    arena_t t; arena_init(&t, tiny, sizeof tiny);
    cfg.levels = 4;   /* 27^3 voxels ~ 78 KB: cannot fit in 16 bytes. */
    ASSERT_FALSE(probe_brick_index_build(&cfg, NULL, 0, &t, &ix));
    return 0;
}

typedef int (*test_fn)(void);
typedef struct { const char *name; test_fn fn; } test_case_t;

int main(void)
{
    static const test_case_t tests[] = {
        { "single_brick_full_cover",   test_single_brick_full_cover },
        { "finer_overwrites_coarser",  test_finer_overwrites_coarser },
        { "uncovered_is_minus_one",    test_uncovered_is_minus_one },
        { "zero_bricks",               test_zero_bricks },
        { "null_and_exhaustion",       test_null_and_exhaustion },
    };
    int failed = 0;
    const int n = (int)(sizeof tests / sizeof tests[0]);
    for (int i = 0; i < n; ++i) {
        int rc = tests[i].fn();
        if (rc != 0) { fprintf(stderr, "FAIL %s\n", tests[i].name); ++failed; }
    }
    printf("probe_brick_index_tests: %d/%d passed\n", n - failed, n);
    return failed == 0 ? 0 : 1;
}
