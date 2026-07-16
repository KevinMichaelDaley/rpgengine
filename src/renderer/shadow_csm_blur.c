/**
 * @file shadow_csm_blur.c
 * @brief CPU Gaussian pre-blur of the baked EVSM cascade moments (see
 *        shadow_csm.h).
 *
 * A raw EVSM moment map is near-binary, so mipmapping it alone yields a hard
 * edge -- there is no variance to spread into a penumbra. EVSM moments ARE
 * linearly filterable (that is the whole point of the exp warp), so we Gaussian-
 * blur the two moments before building the mip chain: the blur injects the
 * variance that the receiver's Chebyshev bound turns into a soft edge, and the
 * mips then let the receiver widen it with distance.
 *
 * This runs ONCE per static bake (the sun/world are static, so the result is
 * cached), reading the whole RG32F array back, separably blurring each layer,
 * and uploading it. Offline: uses malloc (never per frame).
 */
#include "ferrum/renderer/shadow_csm.h"

#include <math.h>
#include <stdlib.h>

#include "ferrum/renderer/gl_constants.h"

/* Separable Gaussian radius (texels) of the moment pre-blur = the base penumbra
 * width before distance-based mip widening. */
#define CSM_BLUR_RADIUS 5
#define CSM_BLUR_SIGMA  2.2f

/* Blur @p src (w*h, 2 floats/texel) into @p dst along X (dx=1) or Y (dx=0),
 * clamping at the edges, with the normalised weights @p wt[0..radius]. */
static void blur_axis(const float *src, float *dst, int w, int h, int dx,
                      const float *wt)
{
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float a0 = src[(y * w + x) * 2] * wt[0];
            float a1 = src[(y * w + x) * 2 + 1] * wt[0];
            for (int r = 1; r <= CSM_BLUR_RADIUS; ++r) {
                int xn = dx ? x - r : x, yn = dx ? y : y - r;
                int xp = dx ? x + r : x, yp = dx ? y : y + r;
                if (xn < 0) xn = 0; if (yn < 0) yn = 0;
                if (xp >= w) xp = w - 1; if (yp >= h) yp = h - 1;
                const float *sn = &src[(yn * w + xn) * 2];
                const float *sp = &src[(yp * w + xp) * 2];
                a0 += (sn[0] + sp[0]) * wt[r];
                a1 += (sn[1] + sp[1]) * wt[r];
            }
            dst[(y * w + x) * 2] = a0;
            dst[(y * w + x) * 2 + 1] = a1;
        }
    }
}

void shadow_csm_blur_moments(shadow_csm_t *csm)
{
    if (csm == NULL || csm->glGetTexImage == NULL ||
        csm->glTexSubImage3D == NULL)
        return;
    int w = (int)csm->static_res, h = (int)csm->static_res;
    int layers = (int)csm->cascades;
    size_t layer_n = (size_t)w * h * 2;

    /* Normalised 1D Gaussian weights (index 0 = centre). */
    float wt[CSM_BLUR_RADIUS + 1], sum;
    wt[0] = 1.0f; sum = 1.0f;
    for (int r = 1; r <= CSM_BLUR_RADIUS; ++r) {
        wt[r] = expf(-(float)(r * r) / (2.0f * CSM_BLUR_SIGMA * CSM_BLUR_SIGMA));
        sum += 2.0f * wt[r];
    }
    for (int r = 0; r <= CSM_BLUR_RADIUS; ++r) wt[r] /= sum;

    float *all = malloc(layer_n * (size_t)layers * sizeof(float));
    float *tmp = malloc(layer_n * sizeof(float));
    if (all == NULL || tmp == NULL) { free(all); free(tmp); return; }

    csm->glActiveTexture(GL_TEXTURE0);
    csm->glBindTexture(GL_TEXTURE_2D_ARRAY, csm->static_atlas.texture);
    csm->glGetTexImage(GL_TEXTURE_2D_ARRAY, 0, GL_RG, GL_FLOAT, all);

    for (int l = 0; l < layers; ++l) {
        float *layer = all + (size_t)l * layer_n;
        blur_axis(layer, tmp, w, h, 1, wt);   /* horizontal -> tmp */
        blur_axis(tmp, layer, w, h, 0, wt);   /* vertical  -> layer */
    }

    csm->glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, w, h, layers,
                         GL_RG, GL_FLOAT, all);
    free(all);
    free(tmp);
}
