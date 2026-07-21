/**
 * @file probe_bake_place_tests.c
 * @brief Unit tests for the offline post-bake probe placement pass (rpg-pjkb,
 *        feature 4). Written before the implementation (TDD phase 1).
 *
 * The loader must not regenerate probes per chunk-load (it never sees the full
 * scene), so placement runs as a SECOND OFFLINE PASS after the baker, over the
 * baked _cNNN.sdf chunks, and ships the result as manual probes (.probes).
 * Module A samples the baked chunks on the CPU (trilinear, min over chunks,
 * far-positive outside); module B composes place -> fixup -> save.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/lightmap/lm_sdf_file.h"
#include "ferrum/memory/arena.h"
#include "ferrum/probe/probe_file.h"
#include "ferrum/probe/probe_set.h"
#include "ferrum/probe/place/probe_brick.h"
#include "ferrum/probe/place/probe_fixup.h"
#include "ferrum/probe/place/probe_chunk_sdf.h"
#include "ferrum/probe/place/probe_bake_place.h"

#define ASSERT_TRUE(e) do { if (!(e)) { fprintf(stderr, \
    "  ASSERT_TRUE failed: %s (%s:%d)\n", #e, __FILE__, __LINE__); return 1; } } while (0)
#define ASSERT_FALSE(e) ASSERT_TRUE(!(e))
#define ASSERT_INT_EQ(a,b) do { long _a=(long)(a),_b=(long)(b); if (_a!=_b) { \
    fprintf(stderr,"  ASSERT_INT_EQ failed: %ld != %ld (%s:%d)\n",_a,_b,__FILE__,__LINE__); \
    return 1; } } while (0)
#define ASSERT_NEAR(a,b,eps) do { double _a=(double)(a),_b=(double)(b); \
    if (fabs(_a-_b)>(eps)) { fprintf(stderr, \
    "  ASSERT_NEAR failed: %g != %g (%s:%d)\n",_a,_b,__FILE__,__LINE__); return 1; } } while (0)

#define TMP_PREFIX "/tmp/probe_bake_place_test"

static uint8_t g_buf[16 * 1024 * 1024];

/* Fill a chunk's voxel grid with the analytic floor field sdf(p) = p.y,
 * sampled at voxel CENTRES (matching the runtime's uvw + 0.5 convention). */
static float *floor_chunk(const int32_t dims[3], float voxel, const float origin[3])
{
    size_t n = (size_t)dims[0] * dims[1] * dims[2];
    float *d = malloc(n * sizeof(float));
    for (int32_t z = 0; z < dims[2]; ++z)
        for (int32_t y = 0; y < dims[1]; ++y)
            for (int32_t x = 0; x < dims[0]; ++x)
                d[((size_t)z * dims[1] + y) * dims[0] + x] =
                    origin[1] + ((float)y + 0.5f) * voxel;
    return d;
}

/* Write two floor chunks side by side along x: [0,4) and [4,8), y,z in [0,4). */
static int write_two_chunks(void)
{
    int32_t dims[3] = { 16, 16, 16 };
    float voxel = 0.25f;
    float o0[3] = { 0, 0, 0 }, o1[3] = { 4, 0, 0 };
    float *d0 = floor_chunk(dims, voxel, o0);
    float *d1 = floor_chunk(dims, voxel, o1);
    bool ok = lm_sdf_save(TMP_PREFIX "_c000.sdf", dims, voxel, o0, d0) &&
              lm_sdf_save(TMP_PREFIX "_c001.sdf", dims, voxel, o1, d1);
    free(d0); free(d1);
    remove(TMP_PREFIX "_c002.sdf");   /* make the scan stop deterministically. */
    return ok ? 0 : 1;
}

/* ---- module A: chunked CPU sampler ---- */

static int test_chunk_sample_matches_analytic(void)
{
    ASSERT_INT_EQ(write_two_chunks(), 0);
    probe_chunk_sdf_t cs;
    ASSERT_TRUE(probe_chunk_sdf_open(TMP_PREFIX, &cs));
    ASSERT_INT_EQ(cs.count, 2);
    float p0[3] = { 1.0f, 1.7f, 2.0f };     /* inside chunk 0. */
    ASSERT_NEAR(probe_chunk_sdf_sample(p0, &cs), 1.7f, 0.05);
    float p1[3] = { 6.5f, 0.6f, 1.0f };     /* inside chunk 1. */
    ASSERT_NEAR(probe_chunk_sdf_sample(p1, &cs), 0.6f, 0.05);
    float po[3] = { 20.0f, 1.0f, 1.0f };    /* outside every chunk. */
    ASSERT_TRUE(probe_chunk_sdf_sample(po, &cs) > 1e6f);
    probe_chunk_sdf_close(&cs);
    return 0;
}

static int test_chunk_open_failures(void)
{
    probe_chunk_sdf_t cs;
    ASSERT_FALSE(probe_chunk_sdf_open("/tmp/definitely_missing_prefix_xyz", &cs));
    ASSERT_FALSE(probe_chunk_sdf_open(NULL, &cs));
    ASSERT_FALSE(probe_chunk_sdf_open(TMP_PREFIX, NULL));
    return 0;
}

/* ---- module B: place -> fixup -> save ---- */

static float sdf_floor(const float p[3], void *user)
{
    (void)user;
    return p[1];
}

static int test_bake_place_roundtrip(void)
{
    probe_brick_config_t brick; memset(&brick, 0, sizeof brick);
    brick.aabb_max[0] = 9.0f; brick.aabb_max[1] = 9.0f; brick.aabb_max[2] = 9.0f;
    brick.coarse_brick = 9.0f; brick.levels = 2; brick.fill_empty = 1;
    brick.sdf = sdf_floor;
    probe_fixup_config_t fix; memset(&fix, 0, sizeof fix);
    fix.clearance = 0.15f; fix.bias = 0.02f; fix.max_push = 1.0f;
    fix.sdf = sdf_floor;

    arena_t a; arena_init(&a, g_buf, sizeof g_buf);
    uint32_t n_saved = 0;
    ASSERT_TRUE(probe_bake_place_run(&brick, &fix, &a, TMP_PREFIX ".probes", &n_saved));
    ASSERT_TRUE(n_saved > 0);

    arena_t la; arena_init(&la, g_buf + 8 * 1024 * 1024, 8 * 1024 * 1024);
    probe_set_t loaded;
    ASSERT_TRUE(probe_set_load(TMP_PREFIX ".probes", &la, &loaded));
    ASSERT_INT_EQ(loaded.count, n_saved);
    /* Every saved probe is at clearance: the fix-up pushed floor-lattice probes
     * (y = 0) up, and nothing saved sits below clearance. */
    for (uint32_t i = 0; i < loaded.count; ++i)
        ASSERT_TRUE(loaded.positions[i * 3 + 1] >= fix.clearance - 1e-3f);
    return 0;
}

/* NULL fixup config: placement saves raw lattice positions (still valid). */
static int test_bake_place_no_fixup(void)
{
    probe_brick_config_t brick; memset(&brick, 0, sizeof brick);
    brick.aabb_max[0] = 9.0f; brick.aabb_max[1] = 9.0f; brick.aabb_max[2] = 9.0f;
    brick.coarse_brick = 9.0f; brick.levels = 1; brick.fill_empty = 1;
    brick.sdf = sdf_floor;
    arena_t a; arena_init(&a, g_buf, sizeof g_buf);
    uint32_t n_saved = 0;
    ASSERT_TRUE(probe_bake_place_run(&brick, NULL, &a, TMP_PREFIX ".probes", &n_saved));
    ASSERT_INT_EQ(n_saved, 64);   /* one coarse brick, no dedup partners. */
    arena_t la; arena_init(&la, g_buf + 8 * 1024 * 1024, 8 * 1024 * 1024);
    probe_set_t loaded;
    ASSERT_TRUE(probe_set_load(TMP_PREFIX ".probes", &la, &loaded));
    /* Raw lattice: the y=0 row survives untouched without fixup. */
    int has_floor_row = 0;
    for (uint32_t i = 0; i < loaded.count; ++i)
        if (fabsf(loaded.positions[i * 3 + 1]) < 1e-4f) has_floor_row = 1;
    ASSERT_TRUE(has_floor_row);
    return 0;
}

static float sdf_empty(const float p[3], void *user)
{
    (void)p; (void)user;
    return 1e9f;
}

/* Zero kept bricks: an empty .probes file is written and loads back as 0. */
static int test_bake_place_empty(void)
{
    probe_brick_config_t brick; memset(&brick, 0, sizeof brick);
    brick.aabb_max[0] = 9.0f; brick.aabb_max[1] = 9.0f; brick.aabb_max[2] = 9.0f;
    brick.coarse_brick = 9.0f; brick.levels = 1; brick.fill_empty = 0;
    brick.sdf = sdf_empty;
    arena_t a; arena_init(&a, g_buf, sizeof g_buf);
    uint32_t n_saved = 123;
    ASSERT_TRUE(probe_bake_place_run(&brick, NULL, &a, TMP_PREFIX ".probes", &n_saved));
    ASSERT_INT_EQ(n_saved, 0);
    arena_t la; arena_init(&la, g_buf + 8 * 1024 * 1024, 8 * 1024 * 1024);
    probe_set_t loaded;
    ASSERT_TRUE(probe_set_load(TMP_PREFIX ".probes", &la, &loaded));
    ASSERT_INT_EQ(loaded.count, 0);
    return 0;
}

static int test_bake_place_failures(void)
{
    probe_brick_config_t brick; memset(&brick, 0, sizeof brick);
    brick.aabb_max[0] = 9.0f; brick.aabb_max[1] = 9.0f; brick.aabb_max[2] = 9.0f;
    brick.coarse_brick = 9.0f; brick.levels = 1; brick.fill_empty = 1;
    brick.sdf = sdf_floor;
    arena_t a; arena_init(&a, g_buf, sizeof g_buf);
    uint32_t n = 0;
    ASSERT_FALSE(probe_bake_place_run(NULL, NULL, &a, TMP_PREFIX ".probes", &n));
    ASSERT_FALSE(probe_bake_place_run(&brick, NULL, NULL, TMP_PREFIX ".probes", &n));
    ASSERT_FALSE(probe_bake_place_run(&brick, NULL, &a, NULL, &n));
    ASSERT_FALSE(probe_bake_place_run(&brick, NULL, &a,
                                      "/definitely/missing/dir/x.probes", &n));
    return 0;
}

typedef int (*test_fn)(void);
typedef struct { const char *name; test_fn fn; } test_case_t;

int main(void)
{
    static const test_case_t tests[] = {
        { "chunk_sample_matches_analytic", test_chunk_sample_matches_analytic },
        { "chunk_open_failures",           test_chunk_open_failures },
        { "bake_place_roundtrip",          test_bake_place_roundtrip },
        { "bake_place_no_fixup",           test_bake_place_no_fixup },
        { "bake_place_empty",              test_bake_place_empty },
        { "bake_place_failures",           test_bake_place_failures },
    };
    int failed = 0;
    const int n = (int)(sizeof tests / sizeof tests[0]);
    for (int i = 0; i < n; ++i) {
        int rc = tests[i].fn();
        if (rc != 0) { fprintf(stderr, "FAIL %s\n", tests[i].name); ++failed; }
    }
    printf("probe_bake_place_tests: %d/%d passed\n", n - failed, n);
    return failed == 0 ? 0 : 1;
}
