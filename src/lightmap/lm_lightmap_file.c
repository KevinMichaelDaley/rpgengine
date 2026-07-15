/**
 * @file lm_lightmap_file.c
 * @brief Lightmap serialization (see lm_lightmap_file.h).
 */
#include "ferrum/lightmap/lm_lightmap_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char LM_MAGIC[4] = { 'F', 'L', 'M', '1' };

/* Texels of luxel value bled into the atlas gutter so bilinear filtering at an
 * island edge never samples the unwritten (black) background -> UV seams. */
#define LM_DILATE_PASSES 4u

/* Build @p src: for every atlas texel, the index of the nearest COVERED texel
 * (a luxel) within LM_DILATE_PASSES rings, or -1. Covered texels map to
 * themselves. @p prev is scratch (atlas-sized). One map, shared by all 9 coeffs
 * so a gutter texel gets a consistent SH from a single source luxel. */
static void lm_build_fill_map(const lm_mesh_bake_result_t *r, int32_t *src,
                              int32_t *prev)
{
    uint32_t w = r->atlas.width, h = r->atlas.height;
    size_t n = (size_t)w * h;
    for (size_t i = 0; i < n; ++i)
        src[i] = -1;
    for (uint32_t i = 0; i < r->n_luxels; ++i) {
        size_t t = (size_t)r->atlas_y[i] * w + r->atlas_x[i];
        if (t < n)
            src[t] = (int32_t)t;
    }
    for (uint32_t p = 0; p < LM_DILATE_PASSES; ++p) {
        memcpy(prev, src, n * sizeof(int32_t)); /* read pass-start, write src */
        for (uint32_t y = 0; y < h; ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                size_t t = (size_t)y * w + x;
                if (prev[t] >= 0)
                    continue;
                int32_t s = -1;
                if (x > 0 && prev[t - 1] >= 0) s = prev[t - 1];
                else if (x + 1 < w && prev[t + 1] >= 0) s = prev[t + 1];
                else if (y > 0 && prev[t - w] >= 0) s = prev[t - w];
                else if (y + 1 < h && prev[t + w] >= 0) s = prev[t + w];
                if (s >= 0)
                    src[t] = s;
            }
        }
    }
}

bool lm_lightmap_save(const lm_mesh_bake_result_t *result, const char *path)
{
    if (result == NULL || path == NULL)
        return false;
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;

    uint32_t hdr[4] = { result->atlas.width, result->atlas.height, 9u,
                        result->n_meshes };
    size_t nt = (size_t)result->atlas.width * result->atlas.height;
    size_t npx = nt * 3u;
    float *scratch = malloc(npx * sizeof(float));
    int32_t *src = malloc(nt * sizeof(int32_t));
    int32_t *prev = malloc(nt * sizeof(int32_t));
    bool ok = scratch != NULL && src != NULL && prev != NULL &&
              fwrite(LM_MAGIC, 1, 4, f) == 4 &&
              fwrite(hdr, sizeof(uint32_t), 4, f) == 4;
    if (ok)
        lm_build_fill_map(result, src, prev);
    for (uint32_t c = 0; ok && c < 9u; ++c) {
        lm_mesh_bake_readback_sh(result, c, scratch);
        /* Bleed each gutter texel from its nearest luxel. */
        for (size_t t = 0; t < nt; ++t) {
            int32_t s = src[t];
            if (s >= 0 && (size_t)s != t) {
                scratch[t * 3]     = scratch[(size_t)s * 3];
                scratch[t * 3 + 1] = scratch[(size_t)s * 3 + 1];
                scratch[t * 3 + 2] = scratch[(size_t)s * 3 + 2];
            }
        }
        ok = fwrite(scratch, sizeof(float), npx, f) == npx;
    }
    if (ok && result->n_meshes > 0)
        ok = fwrite(result->rects, sizeof(lm_atlas_rect_t), result->n_meshes,
                    f) == result->n_meshes;
    free(scratch);
    free(src);
    free(prev);
    fclose(f);
    return ok;
}

bool lm_lightmap_load(const char *path, lm_lightmap_data_t *out)
{
    if (path == NULL || out == NULL)
        return false;
    memset(out, 0, sizeof(*out));
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;

    char magic[4];
    uint32_t hdr[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, LM_MAGIC, 4) != 0 ||
        fread(hdr, sizeof(uint32_t), 4, f) != 4 || hdr[2] != 9u) {
        fclose(f);
        return false;
    }
    out->atlas_w = hdr[0];
    out->atlas_h = hdr[1];
    out->n_meshes = hdr[3];
    size_t npx = (size_t)out->atlas_w * out->atlas_h * 3u;

    bool ok = true;
    for (uint32_t c = 0; c < 9u; ++c) {
        out->coeffs[c] = malloc(npx * sizeof(float));
        if (!out->coeffs[c] || fread(out->coeffs[c], sizeof(float), npx, f) != npx) {
            ok = false;
            break;
        }
    }
    if (ok && out->n_meshes > 0) {
        out->rects = malloc((size_t)out->n_meshes * sizeof(lm_atlas_rect_t));
        ok = out->rects != NULL &&
             fread(out->rects, sizeof(lm_atlas_rect_t), out->n_meshes, f) ==
                 out->n_meshes;
    }
    fclose(f);
    if (!ok)
        lm_lightmap_data_free(out);
    return ok;
}

void lm_lightmap_data_free(lm_lightmap_data_t *data)
{
    if (data == NULL)
        return;
    for (uint32_t c = 0; c < 9u; ++c)
        free(data->coeffs[c]);
    free(data->rects);
    memset(data, 0, sizeof(*data));
}
