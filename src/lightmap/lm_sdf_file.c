/**
 * @file lm_sdf_file.c
 * @brief Per-chunk baked SDF serialization (see lm_sdf_file.h).
 */
#include "ferrum/lightmap/lm_sdf_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char LM_SDF_MAGIC[4] = { 'F', 'S', 'D', 'F' };
#define LM_SDF_VERSION_DIST 1u  /**< distance only. */
#define LM_SDF_VERSION_RGBA 2u  /**< distance + per-voxel RGB albedo. */

/* Shared writer: header + distances, then the RGB albedo when @p albedo != NULL
 * (which selects v2). Returns false on a NULL/invalid argument or IO error. */
static bool lm_sdf_write(const char *path, const int32_t dims[3], float voxel,
                         const float origin[3], const float *dist,
                         const float *albedo)
{
    if (path == NULL || dims == NULL || origin == NULL || dist == NULL)
        return false;
    if (dims[0] < 1 || dims[1] < 1 || dims[2] < 1)
        return false;
    size_t n = (size_t)dims[0] * (size_t)dims[1] * (size_t)dims[2];

    FILE *f = fopen(path, "wb");
    if (f == NULL)
        return false;
    uint32_t version = albedo != NULL ? LM_SDF_VERSION_RGBA : LM_SDF_VERSION_DIST;
    bool ok = fwrite(LM_SDF_MAGIC, 1, 4, f) == 4 &&
              fwrite(&version, sizeof version, 1, f) == 1 &&
              fwrite(dims, sizeof(int32_t), 3, f) == 3 &&
              fwrite(&voxel, sizeof voxel, 1, f) == 1 &&
              fwrite(origin, sizeof(float), 3, f) == 3 &&
              fwrite(dist, sizeof(float), n, f) == n;
    if (ok && albedo != NULL)
        ok = fwrite(albedo, sizeof(float), n * 3, f) == n * 3;
    fclose(f);
    return ok;
}

bool lm_sdf_save(const char *path, const int32_t dims[3], float voxel,
                 const float origin[3], const float *dist)
{
    return lm_sdf_write(path, dims, voxel, origin, dist, NULL);
}

bool lm_sdf_save_rgba(const char *path, const int32_t dims[3], float voxel,
                      const float origin[3], const float *dist,
                      const float *albedo)
{
    if (albedo == NULL)
        return false;
    return lm_sdf_write(path, dims, voxel, origin, dist, albedo);
}

bool lm_sdf_load(const char *path, lm_sdf_data_t *out)
{
    if (path == NULL || out == NULL)
        return false;
    memset(out, 0, sizeof(*out));
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return false;

    char magic[4];
    uint32_t version;
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, LM_SDF_MAGIC, 4) != 0 ||
        fread(&version, sizeof version, 1, f) != 1 ||
        (version != LM_SDF_VERSION_DIST && version != LM_SDF_VERSION_RGBA) ||
        fread(out->dims, sizeof(int32_t), 3, f) != 3 ||
        fread(&out->voxel, sizeof(float), 1, f) != 1 ||
        fread(out->origin, sizeof(float), 3, f) != 3 ||
        out->dims[0] < 1 || out->dims[1] < 1 || out->dims[2] < 1) {
        fclose(f);
        return false;
    }
    size_t n = (size_t)out->dims[0] * (size_t)out->dims[1] * (size_t)out->dims[2];
    out->dist = malloc(n * sizeof(float));
    bool ok = out->dist != NULL && fread(out->dist, sizeof(float), n, f) == n;
    /* v2 carries the per-voxel RGB albedo after the distances. */
    if (ok && version == LM_SDF_VERSION_RGBA) {
        out->albedo = malloc(n * 3 * sizeof(float));
        ok = out->albedo != NULL &&
             fread(out->albedo, sizeof(float), n * 3, f) == n * 3;
    }
    fclose(f);
    if (!ok)
        lm_sdf_data_free(out);
    return ok;
}

void lm_sdf_data_free(lm_sdf_data_t *data)
{
    if (data == NULL)
        return;
    free(data->dist);
    free(data->albedo);
    memset(data, 0, sizeof(*data));
}
