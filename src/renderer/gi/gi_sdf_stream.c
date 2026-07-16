/**
 * @file gi_sdf_stream.c
 * @brief Streaming residency for the baked per-chunk SDF (see gi_sdf_stream.h).
 */
#include "ferrum/renderer/gi/gi_sdf_stream.h"

#include <glad/glad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/lightmap/lm_sdf_file.h"
#include "ferrum/renderer/gi/gi_sdf.h"

float gi_sdf_stream_sample(const gi_sdf_stream_t *s, const float p[3])
{
    if (s == NULL || p == NULL) return 1e30f;
    float d = 1e30f;
    for (int c = 0; c < s->n_chunks; ++c) {
        const gi_sdf_chunk_ram_t *r = &s->ram[c];
        float ds = gi_sdf_baked_sample(r->dist, r->dims, r->origin, r->voxel, p);
        if (ds < d) d = ds;
    }
    return d;
}

int gi_sdf_stream_load(gi_sdf_stream_t *s, const char *prefix)
{
    if (s == NULL || prefix == NULL)
        return -1;
    memset(s, 0, sizeof *s);

    /* Scan <prefix>_cNNN.sdf for a contiguous-from-0 or sparse set; collect all. */
    gi_sdf_chunk_ram_t tmp[4096];
    int loaded = 0;
    for (uint32_t cc = 0; cc < 100000u && loaded < 4096; ++cc) {
        char path[600];
        snprintf(path, sizeof path, "%s_c%03u.sdf", prefix, cc);
        FILE *f = fopen(path, "rb");
        if (f == NULL) continue;
        fclose(f);
        lm_sdf_data_t d;
        if (!lm_sdf_load(path, &d)) continue;
        tmp[loaded].dims[0] = d.dims[0]; tmp[loaded].dims[1] = d.dims[1];
        tmp[loaded].dims[2] = d.dims[2];
        tmp[loaded].voxel = d.voxel;
        tmp[loaded].origin[0] = d.origin[0]; tmp[loaded].origin[1] = d.origin[1];
        tmp[loaded].origin[2] = d.origin[2];
        tmp[loaded].dist = d.dist;           /* take ownership (don't free d). */
        tmp[loaded].albedo = d.albedo;       /* v2 albedo (NULL for v1); owned. */
        ++loaded;
    }
    if (loaded == 0)
        return -1;

    s->n_chunks = loaded;
    s->ram = malloc((size_t)loaded * sizeof(gi_sdf_chunk_ram_t));
    s->page = malloc((size_t)loaded * sizeof(int));
    if (s->ram == NULL || s->page == NULL) { gi_sdf_stream_destroy(s); return -1; }
    memcpy(s->ram, tmp, (size_t)loaded * sizeof(gi_sdf_chunk_ram_t));
    for (int c = 0; c < loaded; ++c) s->page[c] = -1;
    for (int i = 0; i < GI_SDF_MAX_RESIDENT; ++i) {
        s->slot_chunk[i] = -1; s->slot_used[i] = -1;
        glGenTextures(1, &s->tex[i]);
        /* Allocate to the largest chunk dims so every chunk fits any slot. */
    }
    int mx[3] = { 1, 1, 1 };
    for (int c = 0; c < loaded; ++c)
        for (int a = 0; a < 3; ++a)
            if (s->ram[c].dims[a] > mx[a]) mx[a] = s->ram[c].dims[a];
    s->slot_dims[0] = mx[0]; s->slot_dims[1] = mx[1]; s->slot_dims[2] = mx[2];
    /* Scratch to interleave a chunk's (albedo.rgb, dist) -> RGBA at upload time. */
    s->upload_rgba = malloc((size_t)mx[0] * mx[1] * mx[2] * 4 * sizeof(float));
    if (s->upload_rgba == NULL) { gi_sdf_stream_destroy(s); return -1; }
    for (int i = 0; i < GI_SDF_MAX_RESIDENT; ++i) {
        glBindTexture(GL_TEXTURE_3D, s->tex[i]);
        /* RGBA32F: rgb = voxelised static albedo, a = signed distance. Mipmapped
         * so a GI cone hit reads a footprint-appropriate albedo LOD. */
        glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA32F, mx[0], mx[1], mx[2], 0,
                     GL_RGBA, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    }
    fprintf(stderr, "gi_sdf_stream: %d SDF chunks cached, %d resident slots (max dims %dx%dx%d)\n",
            loaded, GI_SDF_MAX_RESIDENT, mx[0], mx[1], mx[2]);
    return loaded;
}

int gi_sdf_stream_boxes(const gi_sdf_stream_t *s, float *out_min, float *out_max)
{
    if (s == NULL || out_min == NULL || out_max == NULL)
        return 0;
    for (int c = 0; c < s->n_chunks; ++c) {
        const gi_sdf_chunk_ram_t *r = &s->ram[c];
        for (int a = 0; a < 3; ++a) {
            out_min[c*3+a] = r->origin[a];
            out_max[c*3+a] = r->origin[a] + (float)r->dims[a] * r->voxel;
        }
    }
    return s->n_chunks;
}

/* Upload chunk c into resident slot. */
static void sdf_upload(gi_sdf_stream_t *s, int c, int slot)
{
    const gi_sdf_chunk_ram_t *r = &s->ram[c];
    size_t n = (size_t)r->dims[0] * r->dims[1] * r->dims[2];
    /* Interleave (albedo.rgb, dist) into the RGBA scratch. Chunks with no baked
     * albedo (v1) fall back to a neutral mid-grey so the bounce isn't black. */
    float *rgba = s->upload_rgba;
    for (size_t i = 0; i < n; ++i) {
        if (r->albedo != NULL) {
            rgba[i*4+0] = r->albedo[i*3+0];
            rgba[i*4+1] = r->albedo[i*3+1];
            rgba[i*4+2] = r->albedo[i*3+2];
        } else {
            rgba[i*4+0] = rgba[i*4+1] = rgba[i*4+2] = 0.5f;
        }
        rgba[i*4+3] = r->dist[i];
    }
    glBindTexture(GL_TEXTURE_3D, s->tex[slot]);
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, r->dims[0], r->dims[1], r->dims[2],
                    GL_RGBA, GL_FLOAT, rgba);
    glGenerateMipmap(GL_TEXTURE_3D);
    s->page[c] = slot;
    s->slot_chunk[slot] = c;
}

/* Ensure chunk c resident (LRU evict). Returns its slot. */
static int sdf_touch(gi_sdf_stream_t *s, int c)
{
    if (c < 0 || c >= s->n_chunks) return -1;
    if (s->page[c] >= 0) { s->slot_used[s->page[c]] = s->frame; return s->page[c]; }
    int slot = -1;
    for (int i = 0; i < GI_SDF_MAX_RESIDENT; ++i)
        if (s->slot_chunk[i] < 0) { slot = i; break; }
    if (slot < 0) {
        int oldest = s->frame + 1;
        for (int i = 0; i < GI_SDF_MAX_RESIDENT; ++i)
            if (s->slot_used[i] < oldest) { oldest = s->slot_used[i]; slot = i; }
        if (s->slot_chunk[slot] >= 0) s->page[s->slot_chunk[slot]] = -1;
    }
    sdf_upload(s, c, slot);
    s->slot_used[slot] = s->frame;
    return slot;
}

void gi_sdf_stream_page(gi_sdf_stream_t *s, const uint8_t *visible)
{
    if (s == NULL || visible == NULL) return;
    ++s->frame;
    for (int c = 0; c < s->n_chunks; ++c)
        if (visible[c]) sdf_touch(s, c);
    /* Record the resident set this frame (for the probe compute to bind). */
    s->resident = 0;
    for (int i = 0; i < GI_SDF_MAX_RESIDENT; ++i)
        if (s->slot_chunk[i] >= 0 && s->resident < GI_SDF_MAX_RESIDENT)
            s->resident_slot[s->resident++] = i;
}

void gi_sdf_stream_destroy(gi_sdf_stream_t *s)
{
    if (s == NULL) return;
    if (s->ram) {
        for (int c = 0; c < s->n_chunks; ++c) {
            free(s->ram[c].dist);
            free(s->ram[c].albedo);
        }
        free(s->ram);
    }
    for (int i = 0; i < GI_SDF_MAX_RESIDENT; ++i)
        if (s->tex[i]) glDeleteTextures(1, &s->tex[i]);
    free(s->upload_rgba);
    free(s->page);
    memset(s, 0, sizeof *s);
}
