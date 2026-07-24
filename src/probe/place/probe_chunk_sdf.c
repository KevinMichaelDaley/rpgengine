/**
 * @file probe_chunk_sdf.c
 * @brief CPU sampler over baked _cNNN.sdf chunks (see probe_chunk_sdf.h).
 *
 * Offline-tool context: malloc is acceptable here (this never runs per-frame
 * or on a fiber). Sampling mirrors the compute shader's scene_sdf: voxel
 * centres at origin + (i + 0.5) * voxel, trilinear filtering with clamped
 * cell indices, min across every chunk whose box contains the point.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/probe/place/probe_chunk_sdf.h"

#define CHUNK_SDF_MAX 256          /* matches the streamer's scan cap. */
#define CHUNK_SDF_FAR 1e9f

/* Trilinear sample of one chunk at p; caller guarantees p is inside its box. */
static float chunk_trilinear(const lm_sdf_data_t *c, const float p[3])
{
    float g[3];
    int32_t i0[3];
    for (int a = 0; a < 3; ++a) {
        /* Voxel-centre convention: sample space g=0 sits at origin + 0.5*voxel. */
        g[a] = (p[a] - c->origin[a]) / c->voxel - 0.5f;
        float max_g = (float)(c->dims[a] - 1);
        if (g[a] < 0.0f) g[a] = 0.0f;
        if (g[a] > max_g) g[a] = max_g;
        i0[a] = (int32_t)g[a];
        if (i0[a] > c->dims[a] - 2) i0[a] = c->dims[a] - 2;
        if (i0[a] < 0) i0[a] = 0;
    }
    float fx = g[0] - (float)i0[0], fy = g[1] - (float)i0[1], fz = g[2] - (float)i0[2];
    float acc = 0.0f;
    for (int corner = 0; corner < 8; ++corner) {
        int32_t x = i0[0] + (corner & 1), y = i0[1] + ((corner >> 1) & 1),
                z = i0[2] + ((corner >> 2) & 1);
        float w = ((corner & 1) ? fx : 1.0f - fx) *
                  (((corner >> 1) & 1) ? fy : 1.0f - fy) *
                  (((corner >> 2) & 1) ? fz : 1.0f - fz);
        acc += w * c->dist[((size_t)z * c->dims[1] + y) * c->dims[0] + x];
    }
    return acc;
}

bool probe_chunk_sdf_open(const char *prefix, probe_chunk_sdf_t *out)
{
    if (prefix == NULL || out == NULL) return false;
    memset(out, 0, sizeof *out);

    lm_sdf_data_t *chunks = malloc((size_t)CHUNK_SDF_MAX * sizeof(lm_sdf_data_t));
    if (chunks == NULL) return false;
    uint32_t *file_no = malloc((size_t)CHUNK_SDF_MAX * sizeof(uint32_t));
    if (file_no == NULL) { free(chunks); return false; }
    uint32_t n = 0;
    /* Chunk numbering is SPARSE (empty cells skip an index): scan through
     * gaps like the runtime streamer instead of stopping at the first
     * miss, keeping each chunk's source file number for sidecar naming. */
    int misses = 0;
    for (int i = 0; i < 100000 && n < CHUNK_SDF_MAX && misses < 4096; ++i) {
        char path[512];
        snprintf(path, sizeof path, "%s_c%03d.sdf", prefix, i);
        if (!lm_sdf_load(path, &chunks[n])) { ++misses; continue; }
        misses = 0;
        file_no[n] = (uint32_t)i;
        ++n;
    }
    if (n == 0) { free(chunks); free(file_no); return false; }
    out->chunks = chunks;
    out->count = n;
    out->file_no = file_no;
    return true;
}

float probe_chunk_sdf_sample(const float p[3], void *user)
{
    const probe_chunk_sdf_t *cs = (const probe_chunk_sdf_t *)user;
    float best = CHUNK_SDF_FAR;
    for (uint32_t i = 0; i < cs->count; ++i) {
        const lm_sdf_data_t *c = &cs->chunks[i];
        int inside = 1;
        for (int a = 0; a < 3; ++a) {
            float span = (float)c->dims[a] * c->voxel;
            if (p[a] < c->origin[a] || p[a] > c->origin[a] + span) { inside = 0; break; }
        }
        if (!inside) continue;
        float d = chunk_trilinear(c, p);
        if (d < best) best = d;
    }
    return best;
}

void probe_chunk_sdf_close(probe_chunk_sdf_t *cs)
{
    if (cs == NULL) return;
    for (uint32_t i = 0; i < cs->count; ++i) lm_sdf_data_free(&cs->chunks[i]);
    free(cs->chunks);
    free(cs->file_no);
    memset(cs, 0, sizeof *cs);
}
