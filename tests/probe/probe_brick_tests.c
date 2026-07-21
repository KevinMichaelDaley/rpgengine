/**
 * @file probe_brick_tests.c
 * @brief Unit tests for the SDF-driven ternary brick probe placer (rpg-pjkb,
 *        feature 1). Written before the implementation (TDD phase 1).
 *
 * The placer replaces the uniform lattice: a ternary brick hierarchy is kept
 * where |SDF(brick center)| <= half the brick diagonal (the Unity-APV keep
 * test, ref/probe_placement_survey.md), each kept brick contributing a 4x4x4
 * probe lattice at spacing size/3, probes deduped across face-adjacent and
 * nested bricks. Analytic SDFs (plane / sphere / empty) stand in for the baked
 * stream so the tests stay headless and exact.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/memory/arena.h"
#include "ferrum/probe/probe_set.h"
#include "ferrum/probe/place/probe_brick.h"

#define ASSERT_TRUE(e) do { if (!(e)) { fprintf(stderr, \
    "  ASSERT_TRUE failed: %s (%s:%d)\n", #e, __FILE__, __LINE__); return 1; } } while (0)
#define ASSERT_FALSE(e) ASSERT_TRUE(!(e))
#define ASSERT_INT_EQ(a,b) do { long _a=(long)(a),_b=(long)(b); if (_a!=_b) { \
    fprintf(stderr,"  ASSERT_INT_EQ failed: %ld != %ld (%s:%d)\n",_a,_b,__FILE__,__LINE__); \
    return 1; } } while (0)
#define ASSERT_NEAR(a,b,eps) do { double _a=(double)(a),_b=(double)(b); \
    if (fabs(_a-_b)>(eps)) { fprintf(stderr, \
    "  ASSERT_NEAR failed: %g != %g (%s:%d)\n",_a,_b,__FILE__,__LINE__); return 1; } } while (0)

static uint8_t g_buf[16 * 1024 * 1024];

/* ---- analytic SDFs ---- */

/* Open air everywhere: no geometry. */
static float sdf_empty(const float p[3], void *user)
{
    (void)p; (void)user;
    return 1e9f;
}

/* Halfspace floor at y=0 (positive above). */
static float sdf_floor(const float p[3], void *user)
{
    (void)user;
    return p[1];
}

static float sdf_zero(const float p[3], void *user)
{
    (void)p; (void)user;
    return 0.0f;
}

/* Sphere: centre + radius passed via user. */
typedef struct { float c[3], r; } sphere_t;
static float sdf_sphere(const float p[3], void *user)
{
    const sphere_t *s = (const sphere_t *)user;
    float dx = p[0] - s->c[0], dy = p[1] - s->c[1], dz = p[2] - s->c[2];
    return sqrtf(dx * dx + dy * dy + dz * dz) - s->r;
}

/* ---- helpers ---- */

static void config_basic(probe_brick_config_t *cfg,
                         float (*sdf)(const float[3], void *), void *user)
{
    memset(cfg, 0, sizeof *cfg);
    cfg->aabb_min[0] = 0.0f; cfg->aabb_min[1] = 0.0f; cfg->aabb_min[2] = 0.0f;
    cfg->aabb_max[0] = 9.0f; cfg->aabb_max[1] = 9.0f; cfg->aabb_max[2] = 9.0f;
    cfg->coarse_brick = 9.0f;   /* one coarse brick covers the whole AABB. */
    cfg->levels = 2;            /* 9 m bricks + 3 m children. */
    cfg->fill_empty = 0;
    cfg->sdf = sdf;
    cfg->sdf_user = user;
}

/* Smallest pairwise distance in a probe set (O(n^2), fine for tests). */
static float min_pair_dist(const probe_set_t *s)
{
    float best = 1e30f;
    for (uint32_t i = 0; i < s->count; ++i)
        for (uint32_t j = i + 1; j < s->count; ++j) {
            float d = 0.0f;
            for (int a = 0; a < 3; ++a) {
                float t = s->positions[i * 3 + a] - s->positions[j * 3 + a];
                d += t * t;
            }
            if (d < best) best = d;
        }
    return sqrtf(best);
}

/* ---- happy path ---- */

/* Empty scene + fill_empty: the coarse lattice alone, with exact dedup count.
 * AABB spans 2x1x1 coarse bricks; the shared face plane dedups 16 probes. */
static int test_fill_empty_coarse_lattice(void)
{
    arena_t a; arena_init(&a, g_buf, sizeof g_buf);
    probe_brick_config_t cfg; config_basic(&cfg, sdf_empty, NULL);
    cfg.aabb_max[0] = 18.0f;    /* 2 bricks along x. */
    cfg.fill_empty = 1;
    probe_set_t set; probe_brick_t *bricks = NULL; uint32_t n_bricks = 0;
    ASSERT_TRUE(probe_brick_place(&cfg, &a, &set, &bricks, &n_bricks));
    ASSERT_INT_EQ(n_bricks, 2);
    ASSERT_INT_EQ(set.count, 4 * 4 * 4 * 2 - 16);
    ASSERT_INT_EQ(set.grid_dim[0], 0);   /* unstructured point set. */
    return 0;
}

/* Empty scene WITHOUT fill_empty: nothing near geometry -> zero probes, but
 * the call still succeeds (an empty region is a valid result). */
static int test_empty_scene_no_fill(void)
{
    arena_t a; arena_init(&a, g_buf, sizeof g_buf);
    probe_brick_config_t cfg; config_basic(&cfg, sdf_empty, NULL);
    probe_set_t set; probe_brick_t *bricks = NULL; uint32_t n_bricks = 0;
    ASSERT_TRUE(probe_brick_place(&cfg, &a, &set, &bricks, &n_bricks));
    ASSERT_INT_EQ(n_bricks, 0);
    ASSERT_INT_EQ(set.count, 0);
    return 0;
}

/* Floor plane: every kept brick passes the keep test; level-1 bricks exist and
 * only near the floor; probe density is higher near the floor than high up. */
static int test_floor_subdivision(void)
{
    arena_t a; arena_init(&a, g_buf, sizeof g_buf);
    probe_brick_config_t cfg; config_basic(&cfg, sdf_floor, NULL);
    cfg.aabb_max[1] = 18.0f;    /* two coarse bricks of air above the floor. */
    probe_set_t set; probe_brick_t *bricks = NULL; uint32_t n_bricks = 0;
    ASSERT_TRUE(probe_brick_place(&cfg, &a, &set, &bricks, &n_bricks));
    ASSERT_TRUE(n_bricks > 0);
    int n_fine = 0;
    for (uint32_t b = 0; b < n_bricks; ++b) {
        float half = bricks[b].size * 0.5f;
        float half_diag = half * sqrtf(3.0f);
        float c[3] = { bricks[b].min[0] + half, bricks[b].min[1] + half,
                       bricks[b].min[2] + half };
        ASSERT_TRUE(fabsf(sdf_floor(c, NULL)) <= half_diag + 1e-4f);
        if (bricks[b].level == 1) {
            ++n_fine;
            /* A 3 m brick keeps iff |centre.y| <= 1.5*sqrt(3) ~ 2.6 m. */
            ASSERT_TRUE(bricks[b].min[1] + 1.5f <= 2.7f);
        }
    }
    ASSERT_TRUE(n_fine > 0);
    /* Density: more probes in the floor band than in the top band. */
    uint32_t low = 0, high = 0;
    for (uint32_t i = 0; i < set.count; ++i) {
        float y = set.positions[i * 3 + 1];
        if (y <= 4.5f) ++low; else if (y >= 13.5f) ++high;
    }
    ASSERT_TRUE(low > high);
    return 0;
}

/* Probe layout inside a brick: min + (i,j,k) * size/3, 64 probes, and the
 * brick's probe_idx table points at exactly those positions. */
static int test_brick_probe_layout(void)
{
    arena_t a; arena_init(&a, g_buf, sizeof g_buf);
    probe_brick_config_t cfg; config_basic(&cfg, sdf_empty, NULL);
    cfg.levels = 1;
    cfg.fill_empty = 1;
    probe_set_t set; probe_brick_t *bricks = NULL; uint32_t n_bricks = 0;
    ASSERT_TRUE(probe_brick_place(&cfg, &a, &set, &bricks, &n_bricks));
    ASSERT_INT_EQ(n_bricks, 1);
    ASSERT_INT_EQ(set.count, 64);
    float step = bricks[0].size / 3.0f;
    for (int k = 0; k < 4; ++k)
        for (int j = 0; j < 4; ++j)
            for (int i = 0; i < 4; ++i) {
                uint32_t pi = bricks[0].probe_idx[(k * 4 + j) * 4 + i];
                ASSERT_TRUE(pi < set.count);
                ASSERT_NEAR(set.positions[pi * 3 + 0], bricks[0].min[0] + step * i, 1e-4);
                ASSERT_NEAR(set.positions[pi * 3 + 1], bricks[0].min[1] + step * j, 1e-4);
                ASSERT_NEAR(set.positions[pi * 3 + 2], bricks[0].min[2] + step * k, 1e-4);
            }
    return 0;
}

/* Sphere: level-1 bricks hug the shell (their centres within half-diag of it). */
static int test_sphere_shell(void)
{
    arena_t a; arena_init(&a, g_buf, sizeof g_buf);
    sphere_t sph = { { 13.5f, 13.5f, 13.5f }, 4.0f };
    probe_brick_config_t cfg; config_basic(&cfg, sdf_sphere, &sph);
    cfg.aabb_max[0] = cfg.aabb_max[1] = cfg.aabb_max[2] = 27.0f;
    probe_set_t set; probe_brick_t *bricks = NULL; uint32_t n_bricks = 0;
    ASSERT_TRUE(probe_brick_place(&cfg, &a, &set, &bricks, &n_bricks));
    int n_fine = 0;
    for (uint32_t b = 0; b < n_bricks; ++b) {
        if (bricks[b].level != 1) continue;
        ++n_fine;
        float half = bricks[b].size * 0.5f;
        float c[3] = { bricks[b].min[0] + half, bricks[b].min[1] + half,
                       bricks[b].min[2] + half };
        ASSERT_TRUE(fabsf(sdf_sphere(c, &sph)) <= half * sqrtf(3.0f) + 1e-4f);
    }
    ASSERT_TRUE(n_fine > 0);
    return 0;
}

/* No two probes may coincide after dedup (face-shared + nested corners). */
static int test_dedup_no_coincident(void)
{
    arena_t a; arena_init(&a, g_buf, sizeof g_buf);
    probe_brick_config_t cfg; config_basic(&cfg, sdf_floor, NULL);
    probe_set_t set; probe_brick_t *bricks = NULL; uint32_t n_bricks = 0;
    ASSERT_TRUE(probe_brick_place(&cfg, &a, &set, &bricks, &n_bricks));
    ASSERT_TRUE(set.count > 0);
    ASSERT_TRUE(min_pair_dist(&set) > 1e-3f);
    return 0;
}

/* Same config twice -> bit-identical output (deterministic order). */
static int test_deterministic(void)
{
    arena_t a1; arena_init(&a1, g_buf, sizeof(g_buf) / 2);
    arena_t a2; arena_init(&a2, g_buf + sizeof(g_buf) / 2, sizeof(g_buf) / 2);
    probe_brick_config_t cfg; config_basic(&cfg, sdf_floor, NULL);
    probe_set_t s1, s2; probe_brick_t *b1 = NULL, *b2 = NULL; uint32_t n1 = 0, n2 = 0;
    ASSERT_TRUE(probe_brick_place(&cfg, &a1, &s1, &b1, &n1));
    ASSERT_TRUE(probe_brick_place(&cfg, &a2, &s2, &b2, &n2));
    ASSERT_INT_EQ(n1, n2);
    ASSERT_INT_EQ(s1.count, s2.count);
    ASSERT_TRUE(memcmp(s1.positions, s2.positions, (size_t)s1.count * 3 * sizeof(float)) == 0);
    return 0;
}

/* Buried cull: a slab's INTERIOR bricks (deep negative sdf) are dropped while
 * its face bricks stay. Slab occupies y in [0,3] of a 9m cell, 2 levels. */
typedef struct { float thick; } slab_t;
static float sdf_slab_t(const float p[3], void *user)
{
    float t = ((const slab_t *)user)->thick;
    float below = -p[1], above = p[1] - t;
    return below > above ? below : above;
}
static int test_buried_cull(void)
{
    arena_t a; arena_init(&a, g_buf, sizeof g_buf);
    slab_t sl = { 3.0f };
    probe_brick_config_t cfg; config_basic(&cfg, sdf_slab_t, &sl);
    cfg.buried_frac = 0.5f;   /* cull bricks with sdf < -0.5 * probe spacing. */
    probe_set_t set; probe_brick_t *bricks = NULL; uint32_t n_bricks = 0;
    ASSERT_TRUE(probe_brick_place(&cfg, &a, &set, &bricks, &n_bricks));
    ASSERT_TRUE(n_bricks > 0);
    for (uint32_t b = 0; b < n_bricks; ++b) {
        if (bricks[b].level != 1) continue;
        float half = bricks[b].size * 0.5f;
        float c[3] = { bricks[b].min[0] + half, bricks[b].min[1] + half,
                       bricks[b].min[2] + half };
        /* level-1 probe spacing = 1 m: no kept fine brick deeper than -0.5 m. */
        ASSERT_TRUE(sdf_slab_t(c, &sl) >= -0.5f - 1e-4f);
    }
    return 0;
}

/* Parent suppression: when every child of a coarse brick is kept, the parent
 * is NOT emitted (the index would never reference it). A fully-buried-band
 * slab filling the whole cell keeps all 27 children. */
static int test_parent_suppression(void)
{
    arena_t a; arena_init(&a, g_buf, sizeof g_buf);
    /* sdf = 0 everywhere: every brick at every level passes the keep test. */
    probe_brick_config_t cfg; config_basic(&cfg, sdf_zero, NULL);
    probe_set_t set; probe_brick_t *bricks = NULL; uint32_t n_bricks = 0;
    ASSERT_TRUE(probe_brick_place(&cfg, &a, &set, &bricks, &n_bricks));
    /* 27 children only -- the fully-covered parent is suppressed. */
    ASSERT_INT_EQ(n_bricks, 27);
    for (uint32_t b = 0; b < n_bricks; ++b) ASSERT_INT_EQ(bricks[b].level, 1);
    return 0;
}

/* ---- edge cases ---- */

/* AABB smaller than one coarse brick: still fully covered by 1 brick. */
static int test_small_aabb_one_brick(void)
{
    arena_t a; arena_init(&a, g_buf, sizeof g_buf);
    probe_brick_config_t cfg; config_basic(&cfg, sdf_empty, NULL);
    cfg.aabb_max[0] = 2.0f; cfg.aabb_max[1] = 2.0f; cfg.aabb_max[2] = 2.0f;
    cfg.levels = 1; cfg.fill_empty = 1;
    probe_set_t set; probe_brick_t *bricks = NULL; uint32_t n_bricks = 0;
    ASSERT_TRUE(probe_brick_place(&cfg, &a, &set, &bricks, &n_bricks));
    ASSERT_INT_EQ(n_bricks, 1);
    ASSERT_INT_EQ(set.count, 64);
    return 0;
}

/* ---- failure modes ---- */

static int test_null_and_bad_args(void)
{
    arena_t a; arena_init(&a, g_buf, sizeof g_buf);
    probe_brick_config_t cfg; config_basic(&cfg, sdf_empty, NULL);
    probe_set_t set; probe_brick_t *bricks = NULL; uint32_t n_bricks = 0;
    ASSERT_FALSE(probe_brick_place(NULL, &a, &set, &bricks, &n_bricks));
    ASSERT_FALSE(probe_brick_place(&cfg, NULL, &set, &bricks, &n_bricks));
    ASSERT_FALSE(probe_brick_place(&cfg, &a, NULL, &bricks, &n_bricks));
    ASSERT_FALSE(probe_brick_place(&cfg, &a, &set, NULL, &n_bricks));
    ASSERT_FALSE(probe_brick_place(&cfg, &a, &set, &bricks, NULL));
    cfg.sdf = NULL;
    ASSERT_FALSE(probe_brick_place(&cfg, &a, &set, &bricks, &n_bricks));
    config_basic(&cfg, sdf_empty, NULL);
    cfg.levels = 0;
    ASSERT_FALSE(probe_brick_place(&cfg, &a, &set, &bricks, &n_bricks));
    cfg.levels = PROBE_BRICK_MAX_LEVELS + 1;
    ASSERT_FALSE(probe_brick_place(&cfg, &a, &set, &bricks, &n_bricks));
    cfg.levels = 1;
    cfg.coarse_brick = 0.0f;
    ASSERT_FALSE(probe_brick_place(&cfg, &a, &set, &bricks, &n_bricks));
    return 0;
}

static int test_arena_exhaustion(void)
{
    static uint8_t tiny[256];
    arena_t a; arena_init(&a, tiny, sizeof tiny);
    probe_brick_config_t cfg; config_basic(&cfg, sdf_floor, NULL);
    probe_set_t set; probe_brick_t *bricks = NULL; uint32_t n_bricks = 0;
    ASSERT_FALSE(probe_brick_place(&cfg, &a, &set, &bricks, &n_bricks));
    return 0;
}

/* ---- runner ---- */

typedef int (*test_fn)(void);
typedef struct { const char *name; test_fn fn; } test_case_t;

int main(void)
{
    static const test_case_t tests[] = {
        { "fill_empty_coarse_lattice", test_fill_empty_coarse_lattice },
        { "empty_scene_no_fill",       test_empty_scene_no_fill },
        { "floor_subdivision",         test_floor_subdivision },
        { "brick_probe_layout",        test_brick_probe_layout },
        { "sphere_shell",              test_sphere_shell },
        { "dedup_no_coincident",       test_dedup_no_coincident },
        { "deterministic",             test_deterministic },
        { "buried_cull",               test_buried_cull },
        { "parent_suppression",        test_parent_suppression },
        { "small_aabb_one_brick",      test_small_aabb_one_brick },
        { "null_and_bad_args",         test_null_and_bad_args },
        { "arena_exhaustion",          test_arena_exhaustion },
    };
    int failed = 0;
    const int n = (int)(sizeof tests / sizeof tests[0]);
    for (int i = 0; i < n; ++i) {
        int rc = tests[i].fn();
        if (rc != 0) { fprintf(stderr, "FAIL %s\n", tests[i].name); ++failed; }
    }
    printf("probe_brick_tests: %d/%d passed\n", n - failed, n);
    return failed == 0 ? 0 : 1;
}
