/**
 * @file sdf_field_tests.c
 * @brief Unit tests for sdf_field: a sampled signed-distance grid + trilinear
 *        sampling + resample/downsample to a coarser grid (rpg-fzht medium/far
 *        far-field SDFs). Pure computation, no GL.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "ferrum/renderer/chunk/sdf_field.h"

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); ++g_fail; } \
                              else printf("ok: %s\n", msg); } while (0)
static int near(float a, float b, float eps) { float d = a - b; return d > -eps && d < eps; }

/* Analytic SDF: signed distance to the plane y = 5 (negative below). */
static float plane_sdf(float x, float y, float z) { (void)x; (void)z; return y - 5.0f; }

int main(void)
{
    /* Fine field: 20^3 over [0,10]^3, voxel 0.5, filled from the analytic plane. */
    int fd[3] = { 20, 20, 20 };
    float fmin[3] = { 0, 0, 0 }; float fvox = 0.5f;
    float *fdat = malloc((size_t)fd[0]*fd[1]*fd[2] * sizeof(float));
    for (int z = 0; z < fd[2]; ++z) for (int y = 0; y < fd[1]; ++y) for (int x = 0; x < fd[0]; ++x) {
        float wx = fmin[0]+((float)x+0.5f)*fvox, wy = fmin[1]+((float)y+0.5f)*fvox, wz = fmin[2]+((float)z+0.5f)*fvox;
        fdat[(size_t)(z*fd[1]+y)*fd[0]+x] = plane_sdf(wx, wy, wz);
    }
    sdf_field_t src = { { fd[0],fd[1],fd[2] }, { fmin[0],fmin[1],fmin[2] }, fvox, fdat };

    /* Sampling recovers the analytic value at interior points (trilinear exact on
     * a linear field). */
    CHECK(near(sdf_field_sample(&src, 5, 5, 5), 0.0f, 1e-3f), "sample at plane (y=5) ~ 0");
    CHECK(near(sdf_field_sample(&src, 5, 8, 5), 3.0f, 1e-3f), "sample above plane ~ +3");
    CHECK(near(sdf_field_sample(&src, 5, 2, 5), -3.0f, 1e-3f), "sample below plane ~ -3");

    /* Outside the grid -> large positive sentinel (not garbage). */
    CHECK(sdf_field_sample(&src, -5, 5, 5) > 1e8f, "sample outside -> large positive");

    /* Resample/downsample into a coarse 5^3 grid over the same region (voxel 2).
     * A linear field downsamples exactly at the coarse cell centres. */
    int cd[3] = { 5, 5, 5 }; float cvox = 2.0f;
    float *cdat = malloc((size_t)cd[0]*cd[1]*cd[2] * sizeof(float));
    sdf_field_t dst = { { cd[0],cd[1],cd[2] }, { 0,0,0 }, cvox, cdat };
    sdf_field_resample(&src, &dst);

    int bad = 0;
    for (int z = 0; z < cd[2]; ++z) for (int y = 0; y < cd[1]; ++y) for (int x = 0; x < cd[0]; ++x) {
        float wy = ((float)y+0.5f)*cvox;                 /* coarse cell-centre y */
        float got = cdat[(size_t)(z*cd[1]+y)*cd[0]+x];
        /* interior coarse cells (their sample stays inside the fine grid) match. */
        if (wy >= 0.75f && wy <= 9.25f && !near(got, wy - 5.0f, 0.2f)) ++bad;
    }
    CHECK(bad == 0, "downsampled coarse field matches analytic at interior cells");

    /* sdf_field_cells helper. */
    CHECK(sdf_field_cells(&dst) == 125u, "cells(5^3) == 125");

    free(fdat); free(cdat);
    printf(g_fail ? "\n%d FAILURES\n" : "\nALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
