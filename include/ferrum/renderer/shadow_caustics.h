/**
 * @file shadow_caustics.h
 * @brief Light-space caustics for translucent sun shadows (rpg-kbqd).
 *
 * A GL 4.3 compute pass (runtime-gated, like gi_probe_gpu) consumes the CSM
 * translucency mask (rpg-29zj): for every translucent texel it reconstructs
 * the glass surface position along that texel's light ray (ortho unproject +
 * the CSM's spherical eye-distance convention), traces several hash-jittered
 * rays within a scattering cone through the resident SDF chunks, and SPLATS
 * the transmitted energy (alpha * tint, r32ui fixed point via imageAtomicAdd)
 * into a light-space caustic map at the texel where each ray LANDS. A resolve
 * pass converts the fixed-point accumulator to a filterable RGBA16F array
 * (one layer per cascade) that the receiver projects INSTEAD of the flat
 * tint, still gated on the mask depth test -- focus patterns emerge where
 * geometry converges the rays, and total energy matches the glass coverage
 * regardless of scatter radius (splats clamp to the map edge).
 *
 * Ownership: owns its two compute programs and both textures; frees them in
 * @ref shadow_caustics_destroy. Mask textures and SDF chunk textures are
 * BORROWED per call / per set. All functions are NULL-safe no-ops except
 * init. Not thread-safe; call on the render thread with a current context.
 */
#ifndef FERRUM_RENDERER_SHADOW_CAUSTICS_H
#define FERRUM_RENDERER_SHADOW_CAUSTICS_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/renderer/gl_loader.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Hard cap on resident SDF chunks traced per ray (mirrors GI_SDF_MAX_RESIDENT). */
#define SHADOW_CAUSTICS_MAX_SDF 16

/** Setup parameters for @ref shadow_caustics_init (all by value). */
typedef struct shadow_caustics_config {
    const gl_loader_t *loader;      /**< GL entry-point loader (non-NULL). */
    uint32_t           resolution;  /**< caustic map resolution (match the mask). */
    uint32_t           cascades;    /**< cascade count (1..8). */
    uint32_t           samples;     /**< rays per translucent texel (0 -> 8). */
    float              scatter;     /**< scatter disk radius (m) at scatter_dist
                                     *   along the ray; 0 = perfectly specular. */
    float              scatter_dist;/**< disk distance (m) along the ray (0 -> 1). */
    float              max_dist;    /**< ray march cap in metres (0 -> 64). */
} shadow_caustics_config_t;

/** Caustics state: trace + resolve compute programs, the r32ui fixed-point
 *  accumulator (3 channel layers per cascade) and the resolved RGBA16F map. */
typedef struct shadow_caustics {
    uint32_t prog_trace;    /**< compute: mask -> jittered SDF rays -> splats. */
    uint32_t prog_resolve;  /**< compute: clear (mode 0) / accum -> map (mode 1). */
    uint32_t accum_tex;     /**< R32UI 2D array, layers = cascades*3 (r,g,b). */
    uint32_t map_tex;       /**< RGBA16F 2D array, layers = cascades. */
    uint32_t resolution;
    uint32_t cascades;
    uint32_t samples;
    float    scatter;
    float    scatter_dist;
    float    max_dist;

    /* Borrowed resident SDF chunk set (see shadow_caustics_set_sdf). */
    uint32_t sdf_tex[SHADOW_CAUSTICS_MAX_SDF];
    float    sdf_origin[SHADOW_CAUSTICS_MAX_SDF][3];
    float    sdf_dim[SHADOW_CAUSTICS_MAX_SDF][3];
    float    sdf_vox[SHADOW_CAUSTICS_MAX_SDF];
    uint32_t sdf_count;

    /* Cached uniform locations (trace program unless noted). */
    struct {
        int32_t vp, inv_vp, eye, far, res, samples, scatter, scatter_dist;
        int32_t max_dist, cascade, seed, mask_layer, sdf_count;
        int32_t sdf[SHADOW_CAUSTICS_MAX_SDF];
        int32_t sdf_origin[SHADOW_CAUSTICS_MAX_SDF];
        int32_t sdf_dim[SHADOW_CAUSTICS_MAX_SDF];
        int32_t sdf_vox[SHADOW_CAUSTICS_MAX_SDF];
        int32_t rz_mode, rz_cascade, rz_res;      /* resolve program. */
    } loc;

    /* Runtime-gated compute entry points (loaded via the config loader). */
    void (*DispatchCompute)(uint32_t x, uint32_t y, uint32_t z);
    void (*MemoryBarrier)(uint32_t barriers);
    void (*BindImageTexture)(uint32_t unit, uint32_t texture, int32_t level,
                             uint8_t layered, int32_t layer, uint32_t access,
                             uint32_t format);
} shadow_caustics_t;

/**
 * @brief Compile the compute programs and allocate the accumulator + map.
 *        Returns false (state zeroed, safe to destroy) when compute is
 *        unavailable (no GL 4.3) or the config is invalid -- the caller
 *        falls back to the flat mask tint.
 */
bool shadow_caustics_init(shadow_caustics_t *c,
                          const shadow_caustics_config_t *config);

/** @brief Release all GL resources. NULL-safe, idempotent. */
void shadow_caustics_destroy(shadow_caustics_t *c);

/**
 * @brief Set the resident SDF chunk set traced by the bake (parallel arrays;
 *        entries past SHADOW_CAUSTICS_MAX_SDF are ignored). Textures are
 *        BORROWED; pass count 0 (or NULL arrays) to trace empty space --
 *        rays then land at max_dist, which for an ortho light means their
 *        own texel (no caustic focusing, energy still conserved).
 */
void shadow_caustics_set_sdf(shadow_caustics_t *c, const uint32_t *textures,
                             const float (*origins)[3], const float (*dims)[3],
                             const float *voxels, uint32_t count);

/**
 * @brief Bake one cascade's caustic map from its translucency mask layer
 *        (@p mask_layer indexes the mask arrays -- the CSM atlas packs one
 *        layer per cascade at a base offset): clear the accumulator, trace +
 *        splat, resolve to the RGBA16F map.
 *        @p vp is the cascade light matrix (column-major), @p eye / @p far
 *        the CSM virtual eye and distance normaliser. Out-of-range cascade
 *        or a zeroed/uninitialised @p c is a safe no-op.
 */
void shadow_caustics_bake(shadow_caustics_t *c, uint32_t mask_color_tex,
                          uint32_t mask_depth_tex, uint32_t mask_layer,
                          uint32_t cascade, const float vp[16],
                          const float eye[3], float far_plane);

/**
 * @brief Bind the resolved caustic map (sampler2DArray) on @p unit and point
 *        @p program's "u_csm_caustic" + "u_caustic_on" uniforms at it via the
 *        caller's cache. NULL / uninitialised @p c binds nothing and sets
 *        u_caustic_on = 0 (still assigns the sampler unit).
 */
struct shader_uniform_cache;
struct shader_program;
void shadow_caustics_bind(const shadow_caustics_t *c,
                          struct shader_uniform_cache *cache,
                          const struct shader_program *program, uint32_t unit);

/** @brief The resolved RGBA16F caustic map texture (0 when uninitialised). */
uint32_t shadow_caustics_map_texture(const shadow_caustics_t *c);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_SHADOW_CAUSTICS_H */
