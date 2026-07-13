#include "ferrum/renderer/forward_plus.h"

#include <string.h>

#include "ferrum/renderer/gl_constants.h"

static void *fp_proc(const gl_loader_t *loader, const char *name)
{
    if (loader == NULL || loader->get_proc_address == NULL) {
        return NULL;
    }
    return loader->get_proc_address(name, loader->user_data);
}

#define FP_LOAD(field, name)                          \
    do {                                              \
        void *raw = fp_proc(loader, name);            \
        if (raw == NULL) {                            \
            return false;                             \
        }                                             \
        memcpy(&(field), &raw, sizeof(field));        \
    } while (0)

bool forward_plus_init(forward_plus_t *fp, const gl_loader_t *loader)
{
    if (fp == NULL || loader == NULL) {
        return false;
    }
    memset(fp, 0, sizeof(*fp));
    FP_LOAD(fp->glGenBuffers, "glGenBuffers");
    FP_LOAD(fp->glDeleteBuffers, "glDeleteBuffers");
    FP_LOAD(fp->glBindBuffer, "glBindBuffer");
    FP_LOAD(fp->glBufferData, "glBufferData");
    FP_LOAD(fp->glGenTextures, "glGenTextures");
    FP_LOAD(fp->glDeleteTextures, "glDeleteTextures");
    FP_LOAD(fp->glBindTexture, "glBindTexture");
    FP_LOAD(fp->glActiveTexture, "glActiveTexture");
    FP_LOAD(fp->glTexBuffer, "glTexBuffer");
    fp->glGenBuffers(4, fp->buffers);
    fp->glGenTextures(4, fp->textures);
    return true;
}

/* Upload one buffer + attach it to its buffer texture with @p format. */
static void fp_upload_one(forward_plus_t *fp, int slot, uint32_t format,
                          const void *data, size_t bytes)
{
    fp->glBindBuffer(GL_TEXTURE_BUFFER, fp->buffers[slot]);
    fp->glBufferData(GL_TEXTURE_BUFFER, bytes, data, GL_STATIC_DRAW);
    fp->glBindTexture(GL_TEXTURE_BUFFER, fp->textures[slot]);
    fp->glTexBuffer(GL_TEXTURE_BUFFER, format, fp->buffers[slot]);
}

void forward_plus_upload(forward_plus_t *fp, const cluster_grid_t *grid,
                         const float *light_data, uint32_t n_lights)
{
    if (fp == NULL || grid == NULL) {
        return;
    }
    uint32_t nidx = grid->index_count > 0 ? grid->index_count : 1u;
    fp_upload_one(fp, 0, GL_RGBA32F, light_data,
                  (size_t)n_lights * 16u * sizeof(float));
    fp_upload_one(fp, 1, GL_R32I, grid->offsets,
                  (size_t)grid->cluster_total * sizeof(uint32_t));
    fp_upload_one(fp, 2, GL_R32I, grid->counts,
                  (size_t)grid->cluster_total * sizeof(uint32_t));
    fp_upload_one(fp, 3, GL_R32I, grid->indices, (size_t)nidx * sizeof(uint32_t));
}

void forward_plus_bind(forward_plus_t *fp, shader_uniform_cache_t *cache,
                       const shader_program_t *program,
                       const cluster_config_t *config, float screen_w,
                       float screen_h)
{
    if (fp == NULL || cache == NULL || program == NULL || config == NULL) {
        return;
    }
    /* Bind the four buffer textures to units 16..19 (past material 0..6 and the
     * SH lightmap 7..15). */
    static const char *const samplers[4] = { "u_light_data", "u_cluster_offset",
                                             "u_cluster_count", "u_light_index" };
    for (int k = 0; k < 4; ++k) {
        fp->glActiveTexture(GL_TEXTURE0 + 16u + (uint32_t)k);
        fp->glBindTexture(GL_TEXTURE_BUFFER, fp->textures[k]);
        shader_uniform_set_int(cache, program, samplers[k], 16 + k);
    }
    shader_uniform_set_int(cache, program, "u_clustered", 1);
    float dims[3] = { (float)config->tiles_x, (float)config->tiles_y,
                      (float)config->slices };
    float screen[3] = { screen_w, screen_h, 0.0f };
    shader_uniform_set_vec3(cache, program, "u_cluster_dims", dims);
    shader_uniform_set_vec3(cache, program, "u_screen", screen);
    shader_uniform_set_float(cache, program, "u_cluster_near", config->near_plane);
    shader_uniform_set_float(cache, program, "u_cluster_far", config->far_plane);
}

void forward_plus_destroy(forward_plus_t *fp)
{
    if (fp == NULL || fp->glDeleteBuffers == NULL) {
        return;
    }
    fp->glDeleteBuffers(4, fp->buffers);
    fp->glDeleteTextures(4, fp->textures);
}
