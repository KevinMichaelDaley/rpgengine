/**
 * @file lm_image.c
 * @brief Wrapped bilinear CPU image sampling (see lm_image.h).
 */
#include "ferrum/lightmap/lm_image.h"

#include <math.h>
#include <stddef.h>

static float lm_srgb_to_linear(float c)
{
    return (c <= 0.04045f) ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

/* Wrap an integer coordinate into [0, n). */
static uint32_t lm_wrap(int v, uint32_t n)
{
    int m = v % (int)n;
    if (m < 0) m += (int)n;
    return (uint32_t)m;
}

/* Fetch a texel as linear RGB. */
static vec3_t lm_texel(const lm_image_t *img, uint32_t x, uint32_t y)
{
    const uint8_t *p = &img->pixels[((size_t)y * img->width + x) * img->channels];
    vec3_t c = { p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f };
    if (img->srgb) {
        c.x = lm_srgb_to_linear(c.x);
        c.y = lm_srgb_to_linear(c.y);
        c.z = lm_srgb_to_linear(c.z);
    }
    return c;
}

vec3_t lm_image_sample(const lm_image_t *img, float u, float v)
{
    if (img == NULL || img->pixels == NULL || img->width == 0 || img->height == 0)
        return (vec3_t){ 0.0f, 0.0f, 0.0f };

    /* Reject non-finite coordinates (a degenerate-triangle barycentric UV can be
     * NaN); a NaN here would propagate through every value that samples it. */
    if (!isfinite(u) || !isfinite(v))
        return (vec3_t){ 0.0f, 0.0f, 0.0f };

    /* Sample at texel centres; wrap. */
    float fx = u * (float)img->width - 0.5f;
    float fy = v * (float)img->height - 0.5f;
    int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
    float tx = fx - (float)x0, ty = fy - (float)y0;
    uint32_t xa = lm_wrap(x0, img->width), xb = lm_wrap(x0 + 1, img->width);
    uint32_t ya = lm_wrap(y0, img->height), yb = lm_wrap(y0 + 1, img->height);

    vec3_t c00 = lm_texel(img, xa, ya), c10 = lm_texel(img, xb, ya);
    vec3_t c01 = lm_texel(img, xa, yb), c11 = lm_texel(img, xb, yb);
    vec3_t top = vec3_add(vec3_scale(c00, 1.0f - tx), vec3_scale(c10, tx));
    vec3_t bot = vec3_add(vec3_scale(c01, 1.0f - tx), vec3_scale(c11, tx));
    return vec3_add(vec3_scale(top, 1.0f - ty), vec3_scale(bot, ty));
}
