/**
 * @file refl_bind.c
 * @brief Reflection-probe GPU upload + per-frame bind (see refl_bind.h).
 */
#include "ferrum/renderer/gi/refl_bind.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/renderer/gi/refl_atlas.h"
#include "ferrum/renderer/gl_constants.h"

#define RG_LOAD(dst, name)                                                    \
    do {                                                                      \
        void *p_ = loader->get_proc_address((name), loader->user_data);       \
        if (p_ == NULL) return false;                                         \
        memcpy(&(dst), &p_, sizeof(p_));                                      \
    } while (0)

bool refl_gpu_upload(refl_gpu_t *gpu, const gl_loader_t *loader,
                     const refl_probe_set_t *set,
                     float *const mips[REFL_PROBE_MAX_MIPS],
                     const float *depth)
{
    if (gpu == NULL)
        return false;
    memset(gpu, 0, sizeof(*gpu));
    gpu->gain = 1.0f;
    gpu->range = 14.0f;
    if (loader == NULL || loader->get_proc_address == NULL || set == NULL ||
        mips == NULL || set->count == 0u || set->mips == 0u)
        return false;

    void (*glGenTextures)(int32_t, uint32_t *);
    void (*glTexImage2D)(uint32_t, int32_t, int32_t, int32_t, int32_t,
                         int32_t, uint32_t, uint32_t, const void *);
    void (*glTexParameteri)(uint32_t, uint32_t, int32_t);
    void (*glGenBuffers)(int32_t, uint32_t *);
    void (*glBindBuffer)(uint32_t, uint32_t);
    void (*glBufferData)(uint32_t, ptrdiff_t, const void *, uint32_t);
    void (*glTexBuffer)(uint32_t, uint32_t, uint32_t);
    RG_LOAD(glGenTextures, "glGenTextures");
    RG_LOAD(glTexImage2D, "glTexImage2D");
    RG_LOAD(glTexParameteri, "glTexParameteri");
    RG_LOAD(glGenBuffers, "glGenBuffers");
    RG_LOAD(glBindBuffer, "glBindBuffer");
    RG_LOAD(glBufferData, "glBufferData");
    RG_LOAD(glTexBuffer, "glTexBuffer");
    RG_LOAD(gpu->glActiveTexture, "glActiveTexture");
    RG_LOAD(gpu->glBindTexture, "glBindTexture");
    RG_LOAD(gpu->glDeleteTextures, "glDeleteTextures");
    RG_LOAD(gpu->glDeleteBuffers, "glDeleteBuffers");

    /* Atlas: explicit prefiltered levels (NOT auto mips -- each level is a
     * different roughness band), trilinear across them. */
    glGenTextures(1, &gpu->atlas);
    gpu->glBindTexture(GL_TEXTURE_2D, gpu->atlas);
    for (uint32_t m = 0; m < set->mips; ++m) {
        if (mips[m] == NULL)
            return false;
        uint32_t w, h;
        refl_atlas_dims(set, m, &w, &h);
        glTexImage2D(GL_TEXTURE_2D, (int32_t)m, GL_RGBA16F, (int32_t)w,
                     (int32_t)h, 0, GL_RGBA, GL_FLOAT, mips[m]);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL,
                    (int32_t)set->mips - 1);

    /* Visibility-depth atlas (DDGI Chebyshev): RG32F, single level. */
    if (set->depth_res > 0u && depth != NULL) {
        glGenTextures(1, &gpu->depth_tex);
        gpu->glBindTexture(GL_TEXTURE_2D, gpu->depth_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F,
                     (int32_t)(set->tiles_x * set->depth_res),
                     (int32_t)(set->tiles_y * set->depth_res), 0, GL_RG,
                     GL_FLOAT, depth);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gpu->depth_res = set->depth_res;
    }

    /* Meta TBO: 2 RGBA32F texels per probe (load-time malloc, freed here). */
    float *meta = (float *)malloc((size_t)set->count * 8u * sizeof(float));
    if (meta == NULL)
        return false;
    for (uint32_t i = 0; i < set->count; ++i) {
        const refl_probe_t *p = &set->probes[i];
        float *t = &meta[(size_t)i * 8u];
        t[0] = p->pos[0];
        t[1] = p->pos[1];
        t[2] = p->pos[2];
        t[3] = p->ao;
        t[4] = (float)(p->tile % set->tiles_x) / (float)set->tiles_x;
        t[5] = (float)(p->tile / set->tiles_x) / (float)set->tiles_y;
        t[6] = 1.0f / (float)set->tiles_x;
        t[7] = 1.0f / (float)set->tiles_y;
    }
    glGenBuffers(1, &gpu->meta_buf);
    glBindBuffer(GL_TEXTURE_BUFFER, gpu->meta_buf);
    glBufferData(GL_TEXTURE_BUFFER,
                 (ptrdiff_t)((size_t)set->count * 8u * sizeof(float)), meta,
                 GL_STATIC_DRAW);
    free(meta);
    glGenTextures(1, &gpu->meta_tex);
    gpu->glBindTexture(GL_TEXTURE_BUFFER, gpu->meta_tex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, gpu->meta_buf);

    gpu->count = set->count;
    gpu->mips = set->mips;
    gpu->tile_res = set->tile_res;
    /* Influence radius: 1.2x the mean nearest-neighbour distance, so the
     * runtime reach always matches the BAKED spacing (a config override in
     * refl_range replaces this after upload). */
    if (set->count > 1u) {
        float acc = 0.0f;
        for (uint32_t i = 0; i < set->count; ++i) {
            float best = 1e30f;
            for (uint32_t j = 0; j < set->count; ++j) {
                if (j == i)
                    continue;
                float dx = set->probes[i].pos[0] - set->probes[j].pos[0];
                float dy = set->probes[i].pos[1] - set->probes[j].pos[1];
                float dz = set->probes[i].pos[2] - set->probes[j].pos[2];
                float d2 = dx*dx + dy*dy + dz*dz;
                if (d2 < best)
                    best = d2;
            }
            acc += sqrtf(best);
        }
        gpu->range = 1.2f * acc / (float)set->count;
    }
    return true;
}

void refl_gpu_bind(const refl_gpu_t *gpu, shader_uniform_cache_t *cache,
                   const shader_program_t *program, uint32_t unit_atlas,
                   uint32_t unit_meta, uint32_t unit_depth)
{
    if (cache == NULL || program == NULL)
        return;
    int32_t count = 0;
    if (gpu != NULL && gpu->count > 0u && gpu->glActiveTexture != NULL) {
        gpu->glActiveTexture(GL_TEXTURE0 + unit_atlas);
        gpu->glBindTexture(GL_TEXTURE_2D, gpu->atlas);
        gpu->glActiveTexture(GL_TEXTURE0 + unit_meta);
        gpu->glBindTexture(GL_TEXTURE_BUFFER, gpu->meta_tex);
        if (gpu->depth_tex != 0u) {
            gpu->glActiveTexture(GL_TEXTURE0 + unit_depth);
            gpu->glBindTexture(GL_TEXTURE_2D, gpu->depth_tex);
        }
        gpu->glActiveTexture(GL_TEXTURE0);
        shader_uniform_set_float(cache, program, "u_refl_depth_res",
                                 (float)gpu->depth_res);
        count = (int32_t)gpu->count;
        shader_uniform_set_float(cache, program, "u_refl_mips",
                                 (float)gpu->mips);
        shader_uniform_set_float(cache, program, "u_refl_tile_res",
                                 (float)gpu->tile_res);
        shader_uniform_set_float(cache, program, "u_refl_gain", gpu->gain);
        shader_uniform_set_float(cache, program, "u_refl_range",
                                 gpu->range);
    }
    shader_uniform_set_int(cache, program, "u_refl_atlas",
                           (int32_t)unit_atlas);
    shader_uniform_set_int(cache, program, "u_refl_meta",
                           (int32_t)unit_meta);
    shader_uniform_set_int(cache, program, "u_refl_depth",
                           (int32_t)unit_depth);
    shader_uniform_set_int(cache, program, "u_refl_count", count);
}

void refl_gpu_destroy(refl_gpu_t *gpu)
{
    if (gpu == NULL)
        return;
    if (gpu->glDeleteTextures && gpu->atlas)
        gpu->glDeleteTextures(1, &gpu->atlas);
    if (gpu->glDeleteTextures && gpu->depth_tex)
        gpu->glDeleteTextures(1, &gpu->depth_tex);
    if (gpu->glDeleteTextures && gpu->meta_tex)
        gpu->glDeleteTextures(1, &gpu->meta_tex);
    if (gpu->glDeleteBuffers && gpu->meta_buf)
        gpu->glDeleteBuffers(1, &gpu->meta_buf);
    gpu->atlas = gpu->depth_tex = gpu->meta_tex = gpu->meta_buf = 0;
    gpu->count = 0u;
}
