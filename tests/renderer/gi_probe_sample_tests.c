/**
 * @file gi_probe_sample_tests.c
 * @brief Unit tests for the nearest-probe SH sampler (rpg-q82b): distance-
 *        weighted blend of nearby probes' SH, reconstructed to irradiance.
 */
#include <math.h>
#include <stdio.h>

#include "ferrum/renderer/gi/gi_probe_sample.h"

#define ASSERT_TRUE(cond)                                                    \
    do { if (!(cond)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
                               #cond); return 1; } } while (0)

/* Set probe i's R-channel DC term (band 0) so it reconstructs ~uniform red. */
static void set_dc_red(gi_probe_set_t *set, uint32_t i, float dc)
{
    for (int c = 0; c < 27; ++c) set->sh[i * 27 + c] = 0.0f;
    set->sh[i * 27 + 0 * 9 + 0] = dc;   /* R, band 0. */
}

static int test_nearest_weighting(void)
{
    float pos[2 * 3], sh[2 * 27];
    gi_probe_set_t set;
    gi_probe_set_init(&set, pos, sh, 2u);
    gi_probe_add(&set, 0, 0, 0);        /* probe 0: bright. */
    gi_probe_add(&set, 10, 0, 0);       /* probe 1: dark. */
    set_dc_red(&set, 0, 3.0f);
    set_dc_red(&set, 1, 0.0f);

    float amin[3] = { 0, 0, 0 }, amax[3] = { 10, 1, 1 };
    uint32_t cs[64], pidx[2];
    gi_probe_grid_t grid;
    ASSERT_TRUE(gi_probe_grid_build(&grid, &set, amin, amax, 6.0f, cs, 64u, pidx, 2u));

    float n[3] = { 0, 1, 0 };
    float near0[3], near1[3];
    float p_near0[3] = { 1, 0, 0 }, p_near1[3] = { 9, 0, 0 };
    ASSERT_TRUE(gi_probe_sample(&set, &grid, p_near0, n, near0));
    ASSERT_TRUE(gi_probe_sample(&set, &grid, p_near1, n, near1));

    /* Near the bright probe -> much brighter red than near the dark probe. */
    ASSERT_TRUE(near0[0] > 0.1f);
    ASSERT_TRUE(near0[0] > near1[0] * 3.0f);
    return 0;
}

static int test_no_probe_nearby(void)
{
    float pos[1 * 3], sh[1 * 27];
    gi_probe_set_t set;
    gi_probe_set_init(&set, pos, sh, 1u);
    gi_probe_add(&set, 0, 0, 0);
    set_dc_red(&set, 0, 5.0f);

    float amin[3] = { 0, 0, 0 }, amax[3] = { 30, 30, 30 };
    uint32_t cs[512], pidx[1];   /* 6^3 cells + 1. */
    gi_probe_grid_t grid;
    ASSERT_TRUE(gi_probe_grid_build(&grid, &set, amin, amax, 5.0f, cs, 512u, pidx, 1u));

    /* Query far from any probe (cells away) -> no contribution, returns false. */
    float n[3] = { 0, 1, 0 }, out[3] = { -1, -1, -1 };
    float far[3] = { 25, 25, 25 };
    ASSERT_TRUE(!gi_probe_sample(&set, &grid, far, n, out));
    ASSERT_TRUE(out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f);
    return 0;
}

int main(void)
{
    int rc = 0;
    rc |= test_nearest_weighting();
    rc |= test_no_probe_nearby();
    if (rc == 0)
        printf("  OK: all gi_probe_sample tests passed\n");
    return rc;
}
