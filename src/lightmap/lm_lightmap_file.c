/**
 * @file lm_lightmap_file.c
 * @brief Lightmap serialization (see lm_lightmap_file.h).
 */
#include "ferrum/lightmap/lm_lightmap_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char LM_MAGIC[4] = { 'F', 'L', 'M', '1' };

bool lm_lightmap_save(const lm_mesh_bake_result_t *result, const char *path)
{
    if (result == NULL || path == NULL)
        return false;
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;

    uint32_t hdr[4] = { result->atlas.width, result->atlas.height, 9u,
                        result->n_meshes };
    size_t npx = (size_t)result->atlas.width * result->atlas.height * 3u;
    float *scratch = malloc(npx * sizeof(float));
    bool ok = scratch != NULL &&
              fwrite(LM_MAGIC, 1, 4, f) == 4 &&
              fwrite(hdr, sizeof(uint32_t), 4, f) == 4;
    for (uint32_t c = 0; ok && c < 9u; ++c) {
        lm_mesh_bake_readback_sh(result, c, scratch);
        ok = fwrite(scratch, sizeof(float), npx, f) == npx;
    }
    if (ok && result->n_meshes > 0)
        ok = fwrite(result->rects, sizeof(lm_atlas_rect_t), result->n_meshes,
                    f) == result->n_meshes;
    free(scratch);
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
