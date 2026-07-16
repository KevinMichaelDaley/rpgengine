/**
 * @file lm_denoise_tests.c
 * @brief Unit tests for lm_denoise: the OpenImageDenoise wrapper that cleans a
 *        baked HDR lightmap atlas image.
 *
 * The tests are written to pass under BOTH build variants:
 *   - real (OIDN=1): lm_denoise_available() == true, denoising actually reduces
 *     Monte-Carlo noise while preserving the signal mean.
 *   - stub (default): lm_denoise_available() == false, denoise is a well-defined
 *     no-op passthrough that returns OK and leaves the buffer untouched.
 * Argument-validation behaviour is identical in both variants.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/lightmap/lm_denoise.h"

#define ASSERT_TRUE(cond)                                                    \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

/* Deterministic [0,1) pseudo-noise (LCG) — no rand(), reproducible. */
static float lcg_next(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return (float)(*state >> 8) / (float)(1u << 24);
}

/* Fill w*h*3 with a constant colour plus symmetric per-pixel noise. */
static void fill_noisy(float *rgb, uint32_t w, uint32_t h, const float base[3],
                       float amp, uint32_t seed)
{
    uint32_t st = seed;
    size_t n = (size_t)w * h;
    for (size_t i = 0; i < n; ++i) {
        for (int c = 0; c < 3; ++c)
            rgb[i * 3 + c] = base[c] + amp * (lcg_next(&st) - 0.5f) * 2.0f;
    }
}

/* Mean and mean-squared-deviation-from-`base` across all channels. */
static void stats(const float *rgb, uint32_t w, uint32_t h, const float base[3],
                  float *out_mean, float *out_var)
{
    size_t n = (size_t)w * h;
    double sum = 0.0, sqd = 0.0;
    for (size_t i = 0; i < n; ++i) {
        for (int c = 0; c < 3; ++c) {
            float v = rgb[i * 3 + c];
            sum += v;
            double d = v - base[c];
            sqd += d * d;
        }
    }
    double cnt = (double)n * 3.0;
    *out_mean = (float)(sum / cnt);
    *out_var = (float)(sqd / cnt);
}

/* ---- Failure modes: identical under real and stub builds. ---------------- */
static int test_null_and_zero_args(void)
{
    float px[3] = { 1.0f, 1.0f, 1.0f };
    lm_denoise_image_t img = { px, NULL, NULL, 1, 1 };

    ASSERT_TRUE(lm_denoise_image(NULL) == LM_DENOISE_INVALID_ARG);

    lm_denoise_image_t no_rgb = { NULL, NULL, NULL, 1, 1 };
    ASSERT_TRUE(lm_denoise_image(&no_rgb) == LM_DENOISE_INVALID_ARG);

    lm_denoise_image_t zero_w = { px, NULL, NULL, 0, 1 };
    ASSERT_TRUE(lm_denoise_image(&zero_w) == LM_DENOISE_INVALID_ARG);

    lm_denoise_image_t zero_h = { px, NULL, NULL, 1, 0 };
    ASSERT_TRUE(lm_denoise_image(&zero_h) == LM_DENOISE_INVALID_ARG);

    /* A valid 1x1 image must be accepted (OK) whichever backend is present. */
    lm_denoise_status_t s = lm_denoise_image(&img);
    ASSERT_TRUE(s == LM_DENOISE_OK);
    return 0;
}

/* ---- Core behaviour: variant-aware. -------------------------------------- */
static int test_denoise_behaviour(void)
{
    const uint32_t W = 64, H = 64;
    const float base[3] = { 0.40f, 0.55f, 0.25f };
    float *rgb = malloc((size_t)W * H * 3 * sizeof(float));
    float *orig = malloc((size_t)W * H * 3 * sizeof(float));
    ASSERT_TRUE(rgb && orig);

    fill_noisy(rgb, W, H, base, 0.25f, 0x1234u);
    memcpy(orig, rgb, (size_t)W * H * 3 * sizeof(float));

    float mean_before, var_before;
    stats(rgb, W, H, base, &mean_before, &var_before);

    lm_denoise_image_t img = { rgb, NULL, NULL, W, H };
    lm_denoise_status_t s = lm_denoise_image(&img);
    ASSERT_TRUE(s == LM_DENOISE_OK);

    float mean_after, var_after;
    stats(rgb, W, H, base, &mean_after, &var_after);

    if (lm_denoise_available()) {
        /* Real OIDN: noise should be substantially suppressed, signal kept. */
        ASSERT_TRUE(var_after < var_before * 0.5f);
        ASSERT_TRUE(fabsf(mean_after - mean_before) < 0.05f);
    } else {
        /* Stub: exact passthrough — buffer unchanged. */
        ASSERT_TRUE(memcmp(rgb, orig, (size_t)W * H * 3 * sizeof(float)) == 0);
    }

    free(rgb);
    free(orig);
    return 0;
}

/* ---- Aux buffers accepted (albedo + normal), no crash, OK. --------------- */
static int test_aux_buffers(void)
{
    const uint32_t W = 32, H = 32;
    size_t n3 = (size_t)W * H * 3;
    float *rgb = malloc(n3 * sizeof(float));
    float *alb = malloc(n3 * sizeof(float));
    float *nrm = malloc(n3 * sizeof(float));
    ASSERT_TRUE(rgb && alb && nrm);

    const float base[3] = { 0.5f, 0.5f, 0.5f };
    fill_noisy(rgb, W, H, base, 0.2f, 0x99u);
    for (size_t i = 0; i < (size_t)W * H; ++i) {
        alb[i * 3] = 0.6f; alb[i * 3 + 1] = 0.6f; alb[i * 3 + 2] = 0.6f;
        nrm[i * 3] = 0.0f; nrm[i * 3 + 1] = 0.0f; nrm[i * 3 + 2] = 1.0f;
    }

    lm_denoise_image_t img = { rgb, alb, nrm, W, H };
    ASSERT_TRUE(lm_denoise_image(&img) == LM_DENOISE_OK);

    free(rgb);
    free(alb);
    free(nrm);
    return 0;
}

int main(void)
{
    printf("lm_denoise_tests (backend: %s)\n",
           lm_denoise_available() ? "OIDN" : "stub");
    int rc = 0;
    rc |= test_null_and_zero_args();
    rc |= test_denoise_behaviour();
    rc |= test_aux_buffers();
    if (rc == 0)
        printf("  OK: all lm_denoise tests passed\n");
    return rc;
}
