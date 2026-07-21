/**
 * @file gi_zone_sdf_tests.c
 * @brief Unit tests for the GLOBAL low-res zone SDF composed from the bake's fine
 *        per-chunk SDFs (page-fault fallback for probe rays; rpg GI leak fix).
 *
 * The compose must be CONSERVATIVE: a coarse cell takes the MINIMUM fine distance
 * inside it, so a thin wall can never vanish at low resolution -- a sphere march
 * against the coarse field then still stops at (or before) every surface the fine
 * field had. Coverage: plan (bounds union / voxel / dims), min-downsample, thin-
 * wall survival, near-surface albedo selection, empty cells stay far, v1 chunks
 * (no albedo) fall back to mid-grey, capacity + bad-arg failure modes.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/renderer/gi/gi_zone_sdf.h"

#define ASSERT_TRUE(expr)                                                     \
    do { if (!(expr)) { fprintf(stderr, "  ASSERT failed: %s (%s:%d)\n",      \
        #expr, __FILE__, __LINE__); return 1; } } while (0)
#define ASSERT_INT_EQ(a, b)                                                   \
    do { long _a=(long)(a), _b=(long)(b); if (_a!=_b) { fprintf(stderr,      \
        "  ASSERT_INT_EQ failed: %ld != %ld (%s:%d)\n", _a, _b,               \
        __FILE__, __LINE__); return 1; } } while (0)
#define ASSERT_NEAR(a, b, eps)                                                \
    do { double _a=(double)(a), _b=(double)(b); if (fabs(_a-_b) > (eps)) {    \
        fprintf(stderr, "  ASSERT_NEAR failed: %g != %g (%s:%d)\n", _a, _b,   \
        __FILE__, __LINE__); return 1; } } while (0)

/* One 8x8x8 fine chunk at 0.25 m voxels covering [0,2)^3, dist = +1 everywhere
 * except a thin x= wall (fine voxels ix==4 -> dist -0.05). Red albedo on the wall. */
static float s_dist[8 * 8 * 8];
static float s_alb[8 * 8 * 8 * 3];

static gi_zone_sdf_src_t wall_chunk(void)
{
    gi_zone_sdf_src_t s;
    memset(&s, 0, sizeof s);
    for (int i = 0; i < 8 * 8 * 8; ++i) {
        s_dist[i] = 1.0f;
        s_alb[i*3+0] = 0.2f; s_alb[i*3+1] = 0.2f; s_alb[i*3+2] = 0.2f;
    }
    for (int z = 0; z < 8; ++z)
        for (int y = 0; y < 8; ++y) {
            int i = (z * 8 + y) * 8 + 4;           /* ix == 4: the wall plane. */
            s_dist[i] = -0.05f;
            s_alb[i*3+0] = 0.9f; s_alb[i*3+1] = 0.05f; s_alb[i*3+2] = 0.05f;
        }
    s.dist = s_dist; s.albedo = s_alb;
    s.dims[0] = s.dims[1] = s.dims[2] = 8;
    s.voxel = 0.25f;
    return s;   /* origin (0,0,0). */
}

static int test_plan_bounds_union(void)
{
    gi_zone_sdf_src_t a = wall_chunk(), b = wall_chunk();
    b.origin[0] = 2.0f;                 /* second chunk continues along +x. */
    gi_zone_sdf_src_t srcs[2] = { a, b };
    int32_t dims[3]; float vox, org[3];
    ASSERT_TRUE(gi_zone_sdf_plan(srcs, 2, 8, dims, &vox, org));
    ASSERT_NEAR(org[0], 0.0f, 1e-6); ASSERT_NEAR(org[1], 0.0f, 1e-6);
    /* Union spans x 0..4, y/z 0..2 -> longest extent 4 m over <= 8 cells. */
    ASSERT_NEAR(vox, 0.5f, 1e-6);
    ASSERT_INT_EQ(dims[0], 8); ASSERT_INT_EQ(dims[1], 4); ASSERT_INT_EQ(dims[2], 4);
    return 0;
}

static int test_min_downsample_thin_wall_survives(void)
{
    gi_zone_sdf_src_t s = wall_chunk();
    int32_t dims[3]; float vox, org[3];
    ASSERT_TRUE(gi_zone_sdf_plan(&s, 1, 4, dims, &vox, org));   /* 2 m / 4 = 0.5 m cells */
    float dist[64], alb[64 * 3];
    ASSERT_TRUE(gi_zone_sdf_compose(&s, 1, dims, vox, org, dist, alb, 64));
    /* The wall's fine voxels (ix==4, x centre 1.125) land in coarse cell cx=2:
     * that cell must keep the NEGATIVE min (wall survives), neighbours stay +. */
    int found_neg = 0;
    for (int z = 0; z < dims[2]; ++z)
        for (int y = 0; y < dims[1]; ++y) {
            int c2 = (z * dims[1] + y) * dims[0] + 2;
            int c0 = (z * dims[1] + y) * dims[0] + 0;
            if (dist[c2] < 0.0f) found_neg = 1;
            ASSERT_TRUE(dist[c0] > 0.0f);
        }
    ASSERT_TRUE(found_neg);
    return 0;
}

static int test_albedo_follows_min_distance(void)
{
    gi_zone_sdf_src_t s = wall_chunk();
    int32_t dims[3]; float vox, org[3];
    gi_zone_sdf_plan(&s, 1, 4, dims, &vox, org);
    float dist[64], alb[64 * 3];
    ASSERT_TRUE(gi_zone_sdf_compose(&s, 1, dims, vox, org, dist, alb, 64));
    /* The wall cell's albedo must be the WALL's red, not the background grey. */
    int c = (0 * dims[1] + 0) * dims[0] + 2;
    ASSERT_NEAR(alb[c*3+0], 0.9f, 1e-4);
    ASSERT_NEAR(alb[c*3+1], 0.05f, 1e-4);
    return 0;
}

static int test_uncovered_cells_stay_far(void)
{
    /* Two chunks with a gap between them: cells in the gap keep a LARGE positive
     * distance (empty), they are never zero/negative. */
    gi_zone_sdf_src_t a = wall_chunk(), b = wall_chunk();
    b.origin[0] = 6.0f;                 /* gap x in [2,6). */
    gi_zone_sdf_src_t srcs[2] = { a, b };
    int32_t dims[3]; float vox, org[3];
    ASSERT_TRUE(gi_zone_sdf_plan(srcs, 2, 8, dims, &vox, org));
    float dist[8 * 2 * 2], alb[8 * 2 * 2 * 3];
    ASSERT_TRUE(gi_zone_sdf_compose(srcs, 2, dims, vox, org, dist, alb, 8 * 2 * 2));
    /* vox = 8 m / 8 = 1 m; gap cells cx=3,4 (x 3..5) are covered by neither box. */
    int cgap = (0 * dims[1] + 0) * dims[0] + 4;
    ASSERT_TRUE(dist[cgap] >= 1.0f);
    return 0;
}

static int test_v1_chunk_grey_albedo(void)
{
    gi_zone_sdf_src_t s = wall_chunk();
    s.albedo = NULL;                    /* v1: distance only. */
    int32_t dims[3]; float vox, org[3];
    gi_zone_sdf_plan(&s, 1, 4, dims, &vox, org);
    float dist[64], alb[64 * 3];
    ASSERT_TRUE(gi_zone_sdf_compose(&s, 1, dims, vox, org, dist, alb, 64));
    int c = (0 * dims[1] + 0) * dims[0] + 2;
    ASSERT_NEAR(alb[c*3+0], 0.5f, 1e-4);
    return 0;
}

static int test_failure_modes(void)
{
    gi_zone_sdf_src_t s = wall_chunk();
    int32_t dims[3]; float vox, org[3];
    ASSERT_TRUE(!gi_zone_sdf_plan(NULL, 1, 4, dims, &vox, org));
    ASSERT_TRUE(!gi_zone_sdf_plan(&s, 0, 4, dims, &vox, org));
    ASSERT_TRUE(!gi_zone_sdf_plan(&s, 1, 0, dims, &vox, org));
    gi_zone_sdf_plan(&s, 1, 4, dims, &vox, org);
    float dist[64], alb[64 * 3];
    ASSERT_TRUE(!gi_zone_sdf_compose(&s, 1, dims, vox, org, dist, alb, 8)); /* cap too small */
    ASSERT_TRUE(!gi_zone_sdf_compose(NULL, 1, dims, vox, org, dist, alb, 64));
    return 0;
}

int main(void)
{
    struct { const char *n; int (*f)(void); } t[] = {
        { "plan_bounds_union",              test_plan_bounds_union },
        { "min_downsample_thin_wall",       test_min_downsample_thin_wall_survives },
        { "albedo_follows_min_distance",    test_albedo_follows_min_distance },
        { "uncovered_cells_stay_far",       test_uncovered_cells_stay_far },
        { "v1_chunk_grey_albedo",           test_v1_chunk_grey_albedo },
        { "failure_modes",                  test_failure_modes },
    };
    int n = (int)(sizeof t / sizeof t[0]), pass = 0;
    for (int i = 0; i < n; ++i) {
        int rc = t[i].f();
        printf("[%s] %s\n", rc == 0 ? "ok  " : "FAIL", t[i].n);
        pass += (rc == 0);
    }
    printf("\ngi_zone_sdf_tests: %d/%d passed\n", pass, n);
    return pass == n ? 0 : 1;
}
