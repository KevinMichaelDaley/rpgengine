/**
 * @file lm_sky_tests.c
 * @brief Unit tests for the environment-sky sampler (lm_sky).
 */
#include <math.h>
#include <stdio.h>

#include "ferrum/lightmap/lm_sky.h"

#define ASSERT_TRUE(c)                                                        \
    do { if (!(c)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
#define NEAR(a, b) (fabsf((a) - (b)) < 1e-3f)

/* A constant sky returns its colour for every direction. */
static int test_constant(void)
{
    lm_sky_t sky = { LM_SKY_CONSTANT, { 0.2f, 0.4f, 0.8f }, NULL, 0.0f };
    vec3_t up = lm_sky_sample(&sky, (vec3_t){ 0, 1, 0 });
    vec3_t side = lm_sky_sample(&sky, (vec3_t){ 1, 0, 0 });
    ASSERT_TRUE(NEAR(up.x, 0.2f) && NEAR(up.y, 0.4f) && NEAR(up.z, 0.8f));
    ASSERT_TRUE(NEAR(side.x, 0.2f) && NEAR(side.y, 0.4f) && NEAR(side.z, 0.8f));
    return 0;
}

/* NULL sky is safe and returns black. */
static int test_null(void)
{
    vec3_t c = lm_sky_sample(NULL, (vec3_t){ 0, 1, 0 });
    ASSERT_TRUE(NEAR(c.x, 0.0f) && NEAR(c.y, 0.0f) && NEAR(c.z, 0.0f));
    return 0;
}

/* An equirect HDRI: top row red, bottom row blue. +Y samples top, -Y bottom.
 * The image is treated as linear (srgb=false) and the sky colour is a gain. */
static int test_hdri_vertical(void)
{
    /* 2x2 image: row 0 (top, v small) red, row 1 (bottom, v large) blue. */
    static const uint8_t px[] = {
        255, 0, 0,  255, 0, 0,   /* top row  */
        0, 0, 255,  0, 0, 255,   /* bottom row */
    };
    lm_image_t img = { px, 2, 2, 3, false };
    lm_sky_t sky = { LM_SKY_HDRI, { 1, 1, 1 }, &img, 0.0f };

    /* Sample at +/-45 deg latitude, which lands on each row's centre (v=0.25,
     * 0.75) rather than the pole where wrapped-bilinear blends both rows. */
    float s = 0.70710678f;
    vec3_t up = lm_sky_sample(&sky, (vec3_t){ 0, s, s });    /* v -> 0.25 (top) */
    vec3_t down = lm_sky_sample(&sky, (vec3_t){ 0, -s, s }); /* v -> 0.75 (bottom) */
    ASSERT_TRUE(up.x > 0.9f && up.z < 0.1f);   /* red */
    ASSERT_TRUE(down.z > 0.9f && down.x < 0.1f);/* blue */
    return 0;
}

/* The sky colour scales an HDRI sample (used as an exposure/gain). */
static int test_hdri_gain(void)
{
    static const uint8_t px[] = { 255, 255, 255 };
    lm_image_t img = { px, 1, 1, 3, false };
    lm_sky_t sky = { LM_SKY_HDRI, { 2.0f, 2.0f, 2.0f }, &img, 0.0f };
    vec3_t c = lm_sky_sample(&sky, (vec3_t){ 0, 1, 0 });
    ASSERT_TRUE(NEAR(c.x, 2.0f) && NEAR(c.y, 2.0f) && NEAR(c.z, 2.0f));
    return 0;
}

struct tc { const char *name; int (*fn)(void); };
static struct tc TESTS[] = {
    { "constant", test_constant },
    { "null", test_null },
    { "hdri_vertical", test_hdri_vertical },
    { "hdri_gain", test_hdri_gain },
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
