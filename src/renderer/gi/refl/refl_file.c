/**
 * @file refl_file.c
 * @brief .rprobe save/load (see refl_file.h).
 */
#include "ferrum/renderer/gi/refl_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/renderer/gi/refl_atlas.h"

static const char RFP_MAGIC[4] = { 'R', 'F', 'P', '1' };

bool refl_file_save(const char *path, const refl_probe_set_t *set,
                    const float *const mips[])
{
    if (path == NULL || set == NULL || mips == NULL || set->mips == 0u ||
        set->mips > REFL_PROBE_MAX_MIPS || set->tiles_x == 0u ||
        set->tiles_y == 0u || set->tile_res == 0u)
        return false;
    FILE *f = fopen(path, "wb");
    if (f == NULL)
        return false;
    bool ok = fwrite(RFP_MAGIC, 1, 4, f) == 4;
    uint32_t hdr[5] = { set->count, set->tile_res, set->mips, set->tiles_x,
                        set->tiles_y };
    ok = ok && fwrite(hdr, sizeof hdr, 1, f) == 1;
    for (uint32_t i = 0; ok && i < set->count; ++i) {
        const refl_probe_t *p = &set->probes[i];
        ok = fwrite(p->pos, sizeof(float), 3, f) == 3 &&
             fwrite(&p->ao, sizeof(float), 1, f) == 1 &&
             fwrite(&p->tile, sizeof(uint32_t), 1, f) == 1;
    }
    for (uint32_t m = 0; ok && m < set->mips; ++m) {
        if (mips[m] == NULL) { ok = false; break; }
        uint32_t w, h;
        refl_atlas_dims(set, m, &w, &h);
        size_t n = (size_t)w * h * 4u;
        ok = fwrite(mips[m], sizeof(float), n, f) == n;
    }
    return (fclose(f) == 0) && ok;
}

bool refl_file_load(const char *path, refl_probe_set_t *set,
                    float *out_mips[REFL_PROBE_MAX_MIPS])
{
    if (path == NULL || set == NULL || set->probes == NULL ||
        out_mips == NULL)
        return false;
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return false;
    char magic[4];
    uint32_t hdr[5];
    bool ok = fread(magic, 1, 4, f) == 4 &&
              memcmp(magic, RFP_MAGIC, 4) == 0 &&
              fread(hdr, sizeof hdr, 1, f) == 1;
    uint32_t count = ok ? hdr[0] : 0u;
    ok = ok && hdr[1] > 0u && hdr[2] > 0u &&
         hdr[2] <= REFL_PROBE_MAX_MIPS && hdr[3] > 0u && hdr[4] > 0u &&
         count <= set->capacity && count <= hdr[3] * hdr[4];
    if (ok) {
        set->count = count;
        set->tile_res = hdr[1];
        set->mips = hdr[2];
        set->tiles_x = hdr[3];
        set->tiles_y = hdr[4];
    }
    for (uint32_t i = 0; ok && i < count; ++i) {
        refl_probe_t *p = &set->probes[i];
        ok = fread(p->pos, sizeof(float), 3, f) == 3 &&
             fread(&p->ao, sizeof(float), 1, f) == 1 &&
             fread(&p->tile, sizeof(uint32_t), 1, f) == 1;
    }
    float *bufs[REFL_PROBE_MAX_MIPS] = { 0 };
    for (uint32_t m = 0; ok && m < set->mips; ++m) {
        uint32_t w, h;
        refl_atlas_dims(set, m, &w, &h);
        size_t n = (size_t)w * h * 4u;
        bufs[m] = (float *)malloc(n * sizeof(float));
        ok = bufs[m] != NULL &&
             fread(bufs[m], sizeof(float), n, f) == n;
    }
    fclose(f);
    if (!ok) {
        for (uint32_t m = 0; m < REFL_PROBE_MAX_MIPS; ++m)
            free(bufs[m]);
        set->count = 0u;
        return false;
    }
    for (uint32_t m = 0; m < REFL_PROBE_MAX_MIPS; ++m)
        out_mips[m] = bufs[m];
    return true;
}
