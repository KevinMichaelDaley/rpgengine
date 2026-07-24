/**
 * @file refl_filter.c
 * @brief Progressive octahedral prefilter (see refl_filter.h).
 */
#include "ferrum/renderer/gi/refl_filter.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "ferrum/renderer/gi/refl_octa.h"

/* Direction of texel centre (x, y) in a res x res octa image. */
static void texel_dir(uint32_t res, int32_t x, int32_t y, float d[3])
{
    float uv[2] = { ((float)x + 0.5f) / (float)res,
                    ((float)y + 0.5f) / (float)res };
    refl_octa_decode(uv, d);
}

void refl_filter_smooth(float *img, uint32_t res, float sharpness,
                        float *tmp)
{
    if (img == NULL || tmp == NULL || res == 0u)
        return;
    size_t bytes = (size_t)res * res * 4u * sizeof(float);
    memcpy(tmp, img, bytes);
    for (int32_t y = 0; y < (int32_t)res; ++y)
        for (int32_t x = 0; x < (int32_t)res; ++x) {
            float dc[3];
            texel_dir(res, x, y, dc);
            float acc[4] = { 0, 0, 0, 0 };
            float wsum = 0.0f;
            for (int32_t j = -1; j <= 1; ++j)
                for (int32_t i = -1; i <= 1; ++i) {
                    int32_t sx = x + i, sy = y + j;
                    if (sx < 0) sx = 0;
                    if (sy < 0) sy = 0;
                    if (sx >= (int32_t)res) sx = (int32_t)res - 1;
                    if (sy >= (int32_t)res) sy = (int32_t)res - 1;
                    float dn[3];
                    texel_dir(res, sx, sy, dn);
                    float dt = dc[0]*dn[0] + dc[1]*dn[1] + dc[2]*dn[2];
                    if (dt < 0.0f)
                        dt = 0.0f;
                    float w = powf(dt, sharpness);
                    const float *p =
                        &tmp[((size_t)sy * res + (size_t)sx) * 4u];
                    for (int c = 0; c < 4; ++c)
                        acc[c] += p[c] * w;
                    wsum += w;
                }
            float *o = &img[((size_t)y * res + (size_t)x) * 4u];
            if (wsum > 1e-8f)
                for (int c = 0; c < 4; ++c)
                    o[c] = acc[c] / wsum;
        }
}

void refl_filter_downsample(const float *src, uint32_t sres, float *dst)
{
    if (src == NULL || dst == NULL || sres < 2u)
        return;
    uint32_t dres = sres / 2u;
    for (uint32_t y = 0; y < dres; ++y)
        for (uint32_t x = 0; x < dres; ++x) {
            float *o = &dst[((size_t)y * dres + x) * 4u];
            for (int c = 0; c < 4; ++c)
                o[c] = 0.0f;
            for (uint32_t j = 0; j < 2u; ++j)
                for (uint32_t i = 0; i < 2u; ++i) {
                    const float *p =
                        &src[(((size_t)y * 2u + j) * sres +
                              ((size_t)x * 2u + i)) * 4u];
                    for (int c = 0; c < 4; ++c)
                        o[c] += p[c] * 0.25f;
                }
        }
}
