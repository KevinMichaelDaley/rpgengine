/**
 * @file refl_bind.h
 * @brief Runtime GPU side of the reflection probes (rpg-akwc): upload the
 *        baked .rprobe atlas (RGBA16F, explicit prefiltered mip chain) +
 *        per-probe meta TBO, and bind both for the PBR fragment shader's
 *        split-sum sampling.
 *
 * Meta layout: two RGBA32F texels per probe --
 *   texel 0: probe position xyz, ao;
 *   texel 1: atlas tile uv origin (u0, v0) and scale (su, sv) at mip 0.
 *
 * Ownership: upload owns the GL objects until destroy. Upload happens once
 * at level load (no per-frame allocation); bind is per-frame and only
 * touches texture units + uniforms.
 */
#ifndef FERRUM_RENDERER_GI_REFL_BIND_H
#define FERRUM_RENDERER_GI_REFL_BIND_H

#include <stdbool.h>

#include "ferrum/renderer/gi/refl_probe.h"
#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/shader_uniforms.h"

#ifdef __cplusplus
extern "C" {
#endif

/** GPU handles + layout mirror for binding. */
typedef struct refl_gpu {
    uint32_t atlas;      /**< RGBA16F 2D atlas with set->mips levels. */
    uint32_t depth_tex;  /**< RG32F octa visibility-depth atlas (1 level). */
    uint32_t meta_buf;   /**< TBO backing buffer. */
    uint32_t meta_tex;   /**< RGBA32F buffer texture over meta_buf. */
    uint32_t count;      /**< probe count (0 = disabled). */
    uint32_t mips;
    uint32_t tile_res;
    uint32_t depth_res;  /**< depth tile edge (0 = no visibility test). */
    float gain;          /**< artist gain applied in the shader. */
    float range;         /**< probe influence radius in metres. */
    /* GL entry points (loaded in upload). */
    void (*glActiveTexture)(uint32_t);
    void (*glBindTexture)(uint32_t, uint32_t);
    void (*glDeleteTextures)(int32_t, const uint32_t *);
    void (*glDeleteBuffers)(int32_t, const uint32_t *);
} refl_gpu_t;

/**
 * Create + fill the atlas and meta TBO from a loaded .rprobe payload
 * (@p set + @p mips as returned by refl_file_load). Returns false on NULL
 * args / GL failure; @p gpu is zeroed first, so a failed upload leaves the
 * feature cleanly disabled (count 0).
 */
bool refl_gpu_upload(refl_gpu_t *gpu, const gl_loader_t *loader,
                     const refl_probe_set_t *set,
                     float *const mips[REFL_PROBE_MAX_MIPS],
                     const float *depth);

/**
 * Bind atlas + meta + visibility-depth and set the u_refl_* uniforms on
 * @p program. @p unit_atlas / @p unit_meta / @p unit_depth are the texture
 * units to occupy. Safe on a zeroed @p gpu (binds nothing, u_refl_count 0).
 */
void refl_gpu_bind(const refl_gpu_t *gpu, shader_uniform_cache_t *cache,
                   const shader_program_t *program, uint32_t unit_atlas,
                   uint32_t unit_meta, uint32_t unit_depth);

/** Delete GL objects; NULL-safe, idempotent. */
void refl_gpu_destroy(refl_gpu_t *gpu);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_GI_REFL_BIND_H */
