#ifndef FERRUM_RENDERER_FORWARD_PLUS_H
#define FERRUM_RENDERER_FORWARD_PLUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ferrum/renderer/cluster_grid.h"
#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/light.h"
#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/shader_uniforms.h"

/** @file
 * @brief Forward+ GPU light buffers: uploads the cluster grid + light data to
 *        texture buffers and binds them so the PBR shader shades each fragment
 *        from only its cluster's lights.
 *
 * Four texture buffers: light data (RGBA32F, 4 texels/light), per-cluster offset
 * and count (R32I), and the flat light-index list (R32I). @ref forward_plus_bind
 * points the PBR shader's cluster samplers + dims/screen/near/far at them and
 * sets u_clustered. Pack a light into the 16-float layout the shader expects
 * with @ref forward_plus_pack_light before uploading.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Forward+ light-buffer state (4 buffer objects + 4 buffer textures). */
typedef struct forward_plus {
    void (*glGenBuffers)(int32_t n, uint32_t *b);
    void (*glDeleteBuffers)(int32_t n, const uint32_t *b);
    void (*glBindBuffer)(uint32_t target, uint32_t buffer);
    void (*glBufferData)(uint32_t target, size_t size, const void *data, uint32_t usage);
    void (*glGenTextures)(int32_t n, uint32_t *t);
    void (*glDeleteTextures)(int32_t n, const uint32_t *t);
    void (*glBindTexture)(uint32_t target, uint32_t texture);
    void (*glActiveTexture)(uint32_t unit);
    void (*glTexBuffer)(uint32_t target, uint32_t internalformat, uint32_t buffer);
    uint32_t buffers[4];  /**< light_data, offset, count, index. */
    uint32_t textures[4];
} forward_plus_t;

/** Pack a light into the 16-float texel layout the PBR shader unpacks:
 *  t0=(type,pos.xyz) t1=(dir.xyz,range) t2=(colour*intensity, cos_inner)
 *  t3=(cos_outer,0,0,0). */
static inline void forward_plus_pack_light(const render_light_t *l, float out[16])
{
    out[0] = (float)l->kind;
    out[1] = l->position[0]; out[2] = l->position[1]; out[3] = l->position[2];
    out[4] = l->direction[0]; out[5] = l->direction[1]; out[6] = l->direction[2];
    out[7] = l->range;
    out[8] = l->color[0] * l->intensity;
    out[9] = l->color[1] * l->intensity;
    out[10] = l->color[2] * l->intensity;
    out[11] = l->cos_inner;
    out[12] = l->cos_outer;
    out[13] = 0.0f; out[14] = 0.0f; out[15] = 0.0f;
}

/**
 * @brief Create the forward+ buffers (binds GL entry points from @p loader).
 * @return true on success, false if a GL entry point is missing.
 */
bool forward_plus_init(forward_plus_t *fp, const gl_loader_t *loader);

/**
 * @brief Upload the built @p grid and packed @p light_data (n_lights*16 floats)
 *        to the texture buffers.
 */
void forward_plus_upload(forward_plus_t *fp, const cluster_grid_t *grid,
                         const float *light_data, uint32_t n_lights);

/**
 * @brief Bind the forward+ buffers to texture units and set the PBR shader's
 *        cluster uniforms (samplers, dims, screen size, near/far, u_clustered).
 *        Call after binding the shader, before drawing.
 */
void forward_plus_bind(forward_plus_t *fp, shader_uniform_cache_t *cache,
                       const shader_program_t *program,
                       const cluster_config_t *config, float screen_w,
                       float screen_h);

/**
 * @brief Release the forward+ buffers/textures. NULL-safe.
 */
void forward_plus_destroy(forward_plus_t *fp);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_FORWARD_PLUS_H */
