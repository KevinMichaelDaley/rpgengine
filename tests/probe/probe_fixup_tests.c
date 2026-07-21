/**
 * @file probe_fixup_tests.c
 * @brief Unit tests for probe virtual-offset + validity fix-up (rpg-pjkb,
 *        feature 2). Written before the implementation (TDD phase 1).
 *
 * Survey basis (ref/probe_placement_survey.md): a probe embedded in or pressed
 * against geometry traces black and stamps dark seams -- so push its TRACE
 * ORIGIN out along the SDF gradient to a clearance, cap the push, and mark
 * probes that cannot escape INVALID so sampling can zero their weight.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/probe/place/probe_fixup.h"

#define ASSERT_TRUE(e) do { if (!(e)) { fprintf(stderr, \
    "  ASSERT_TRUE failed: %s (%s:%d)\n", #e, __FILE__, __LINE__); return 1; } } while (0)
#define ASSERT_FALSE(e) ASSERT_TRUE(!(e))
#define ASSERT_NEAR(a,b,eps) do { double _a=(double)(a),_b=(double)(b); \
    if (fabs(_a-_b)>(eps)) { fprintf(stderr, \
    "  ASSERT_NEAR failed: %g != %g (%s:%d)\n",_a,_b,__FILE__,__LINE__); return 1; } } while (0)

/* Halfspace floor at y=0 (positive above). */
static float sdf_floor(const float p[3], void *user)
{
    (void)user;
    return p[1];
}

/* Infinite slab: solid between y=0 and y=thick (user = &thick). Points inside
 * are at most thick/2 from a face, so a small max_push cannot escape. */
static float sdf_slab(const float p[3], void *user)
{
    float thick = *(const float *)user;
    float d_below = -p[1];              /* distance "outside" below the slab. */
    float d_above = p[1] - thick;       /* distance "outside" above the slab. */
    float outside = d_below > d_above ? d_below : d_above;
    return outside;                     /* negative strictly inside the slab. */
}

static void config_basic(probe_fixup_config_t *cfg,
                         float (*sdf)(const float[3], void *), void *user)
{
    memset(cfg, 0, sizeof *cfg);
    cfg->clearance = 0.15f;
    cfg->bias = 0.02f;
    cfg->max_push = 1.0f;
    cfg->sdf = sdf;
    cfg->sdf_user = user;
}

/* A probe hovering below clearance is pushed up to clearance + bias. */
static int test_pressed_probe_pushed_out(void)
{
    probe_fixup_config_t cfg; config_basic(&cfg, sdf_floor, NULL);
    float pos[3] = { 2.0f, 0.05f, -1.0f };
    float adj[3]; uint8_t valid = 0;
    ASSERT_TRUE(probe_fixup_apply(&cfg, pos, 1, adj, &valid));
    ASSERT_NEAR(adj[0], 2.0f, 1e-4);
    ASSERT_NEAR(adj[1], cfg.clearance + cfg.bias, 1e-3);
    ASSERT_NEAR(adj[2], -1.0f, 1e-4);
    ASSERT_TRUE(valid == 1);
    return 0;
}

/* A probe BELOW the floor (negative sdf) escapes upward through the surface. */
static int test_buried_probe_escapes(void)
{
    probe_fixup_config_t cfg; config_basic(&cfg, sdf_floor, NULL);
    float pos[3] = { 0.0f, -0.5f, 0.0f };
    float adj[3]; uint8_t valid = 0;
    ASSERT_TRUE(probe_fixup_apply(&cfg, pos, 1, adj, &valid));
    ASSERT_NEAR(adj[1], cfg.clearance + cfg.bias, 1e-3);
    ASSERT_TRUE(valid == 1);
    return 0;
}

/* A probe already at clearance is left untouched (bit-exact). */
static int test_clear_probe_untouched(void)
{
    probe_fixup_config_t cfg; config_basic(&cfg, sdf_floor, NULL);
    float pos[3] = { 1.0f, 3.0f, 2.0f };
    float adj[3]; uint8_t valid = 0;
    ASSERT_TRUE(probe_fixup_apply(&cfg, pos, 1, adj, &valid));
    ASSERT_TRUE(memcmp(pos, adj, sizeof pos) == 0);
    ASSERT_TRUE(valid == 1);
    return 0;
}

/* Deep inside a thick slab with a small max_push: cannot escape -> INVALID,
 * and the adjusted position stays finite (clamped push, no NaN). */
static int test_unescapable_probe_invalid(void)
{
    float thick = 6.0f;
    probe_fixup_config_t cfg; config_basic(&cfg, sdf_slab, &thick);
    cfg.max_push = 0.5f;
    float pos[3] = { 0.0f, 3.0f, 0.0f };   /* dead centre: 3 m from either face. */
    float adj[3]; uint8_t valid = 1;
    ASSERT_TRUE(probe_fixup_apply(&cfg, pos, 1, adj, &valid));
    ASSERT_TRUE(valid == 0);
    for (int a = 0; a < 3; ++a) ASSERT_TRUE(isfinite(adj[a]));
    return 0;
}

/* Batch: mixed probes processed independently, deterministic across runs. */
static int test_batch_and_determinism(void)
{
    probe_fixup_config_t cfg; config_basic(&cfg, sdf_floor, NULL);
    float pos[9] = { 0, 0.05f, 0,   5, 4.0f, 5,   -2, -0.3f, 1 };
    float adj1[9], adj2[9]; uint8_t v1[3], v2[3];
    ASSERT_TRUE(probe_fixup_apply(&cfg, pos, 3, adj1, v1));
    ASSERT_TRUE(probe_fixup_apply(&cfg, pos, 3, adj2, v2));
    ASSERT_TRUE(memcmp(adj1, adj2, sizeof adj1) == 0);
    ASSERT_TRUE(memcmp(v1, v2, sizeof v1) == 0);
    ASSERT_TRUE(v1[0] == 1 && v1[1] == 1 && v1[2] == 1);
    ASSERT_NEAR(adj1[4], 4.0f, 1e-6);       /* clear probe untouched. */
    ASSERT_NEAR(adj1[7], cfg.clearance + cfg.bias, 1e-3); /* buried escapes. */
    return 0;
}

/* clearance <= 0 disables the pass: positions copied verbatim, all valid. */
static int test_zero_clearance_noop(void)
{
    probe_fixup_config_t cfg; config_basic(&cfg, sdf_floor, NULL);
    cfg.clearance = 0.0f;
    float pos[6] = { 0, -0.5f, 0,   1, 0.01f, 1 };
    float adj[6]; uint8_t v[2];
    ASSERT_TRUE(probe_fixup_apply(&cfg, pos, 2, adj, v));
    ASSERT_TRUE(memcmp(pos, adj, sizeof pos) == 0);
    ASSERT_TRUE(v[0] == 1 && v[1] == 1);
    return 0;
}

/* count == 0 succeeds and touches nothing. */
static int test_zero_count(void)
{
    probe_fixup_config_t cfg; config_basic(&cfg, sdf_floor, NULL);
    ASSERT_TRUE(probe_fixup_apply(&cfg, NULL, 0, NULL, NULL));
    return 0;
}

static int test_null_args(void)
{
    probe_fixup_config_t cfg; config_basic(&cfg, sdf_floor, NULL);
    float pos[3] = { 0, 1, 0 }, adj[3]; uint8_t v;
    ASSERT_FALSE(probe_fixup_apply(NULL, pos, 1, adj, &v));
    ASSERT_FALSE(probe_fixup_apply(&cfg, NULL, 1, adj, &v));
    ASSERT_FALSE(probe_fixup_apply(&cfg, pos, 1, NULL, &v));
    ASSERT_FALSE(probe_fixup_apply(&cfg, pos, 1, adj, NULL));
    cfg.sdf = NULL;
    ASSERT_FALSE(probe_fixup_apply(&cfg, pos, 1, adj, &v));
    return 0;
}

typedef int (*test_fn)(void);
typedef struct { const char *name; test_fn fn; } test_case_t;

int main(void)
{
    static const test_case_t tests[] = {
        { "pressed_probe_pushed_out",  test_pressed_probe_pushed_out },
        { "buried_probe_escapes",      test_buried_probe_escapes },
        { "clear_probe_untouched",     test_clear_probe_untouched },
        { "unescapable_probe_invalid", test_unescapable_probe_invalid },
        { "batch_and_determinism",     test_batch_and_determinism },
        { "zero_clearance_noop",       test_zero_clearance_noop },
        { "zero_count",                test_zero_count },
        { "null_args",                 test_null_args },
    };
    int failed = 0;
    const int n = (int)(sizeof tests / sizeof tests[0]);
    for (int i = 0; i < n; ++i) {
        int rc = tests[i].fn();
        if (rc != 0) { fprintf(stderr, "FAIL %s\n", tests[i].name); ++failed; }
    }
    printf("probe_fixup_tests: %d/%d passed\n", n - failed, n);
    return failed == 0 ? 0 : 1;
}
