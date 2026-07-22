/**
 * @file shadow_csm.h
 * @brief Cascaded shadow maps for a stationary directional light (rpg-fsvq).
 *
 * The cascades are VIEW-INDEPENDENT: because the static map is baked once and
 * cached forever (the sun is stationary and the player may roam the whole
 * scene), fitting the cascades to the camera frustum would clip out casters
 * behind the view (a back wall would stop shadowing). Instead the cascades
 * partition the LIGHT frustum SPATIALLY: the scene is projected into a shared
 * light-space XY AABB, which is gridded into @ref cascades tiles (see @ref
 * shadow_csm_grid_dims), each cascade fit to one small tile so its static_res
 * texels give high ground density across the WHOLE scene, not just its centre.
 * A caster renders into every tile its light-space XY box overlaps (see @ref
 * shadow_csm_caster_in_cascade); in light space a caster and the shadow it
 * throws share the same XY footprint, so no shadow-throw extension is needed.
 * At shade time a fragment samples EVERY cascade whose light box contains it and
 * takes the union of their occlusion, so tile borders (where the small filter
 * guard overlaps two tiles) stay seamless regardless of where the camera is.
 *
 * Each cascade is shadowed by TWO co-sampled maps:
 *
 *   - a STATIC array (@ref shadow_csm_bake_static), rendering every static
 *     caster once and caching the depth -- for a stationary light + stationary
 *     world this never changes, so it is baked once and reused; and
 *   - a lower-res DYNAMIC array (@ref shadow_csm_render_dynamic), re-rendering
 *     only the moving casters every frame.
 *
 * At shade time the material shader selects the cascade by view depth and takes
 * the nearer occluder of the two maps (co-sampling), so a static wall and a
 * moving prop both cast without the per-frame cost of redrawing the world.
 *
 * Both maps are R32F GL_TEXTURE_2D_ARRAY (one layer per cascade) storing linear
 * distance from each cascade's virtual eye, normalised by its far plane.
 *
 * Ownership: owns its two array textures, depth renderbuffers, FBO and shader;
 * frees them in @ref shadow_csm_destroy. Scenes/cameras are borrowed per call.
 */
#ifndef FERRUM_RENDERER_SHADOW_CSM_H
#define FERRUM_RENDERER_SHADOW_CSM_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/math/mat4.h"
#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/render_scene.h"
#include "ferrum/renderer/resource/gpu_registry.h"
#include "ferrum/renderer/resource/shadow_atlas.h"
#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/shader_uniforms.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Hard cap on cascade count (shader array size + storage). A stationary light
 *  bakes its static map once, so it can afford many high-res cascades. */
#define SHADOW_CSM_MAX_CASCADES 8u

/** Setup parameters for @ref shadow_csm_init (all by value). */
typedef struct shadow_csm_config {
    const gl_loader_t *loader;      /**< GL entry-point loader (non-NULL). */
    uint32_t           cascades;    /**< 1..SHADOW_CSM_MAX_CASCADES. */
    uint32_t           static_res;  /**< per-cascade static map resolution. */
    uint32_t           dynamic_res; /**< per-cascade dynamic map resolution. */
    float              lambda;      /**< split blend: 0=uniform, 1=logarithmic. */
    float              max_distance;/**< cap the far split here (0 = camera far);
                                     *   keeps texels fine over the shadowed range. */
    float              softness;    /**< sun-penumbra mip LOD bias for the EVSM
                                     *   sample (0 = crisp; ~2-3 = soft). */
    bool               pcss;        /**< true = variable-width PCSS penumbra;
                                     *   false (default) = cheaper fixed-width PCF. */
} shadow_csm_config_t;

/** Shadow-map state: the static EVSM2 cascade array + a single low-res
 *  orthographic depth map for dynamic casters, plus their light matrices. */
typedef struct shadow_csm {
    shader_program_t       shader;   /**< EVSM/distance depth program. */
    shader_uniform_cache_t cache;
    uint32_t cascades;
    uint32_t static_res;
    uint32_t dynamic_res;
    float    lambda;
    float    max_distance; /**< far-split cap (0 = camera far). */
    float    softness;     /**< sun-penumbra EVSM mip LOD bias. */
    bool     pcss;         /**< variable-width PCSS (true) vs fixed-width PCF (false). */

    gpu_registry_t registry;      /**< tracks shadow depth targets as GPU resources. */
    shadow_atlas_t static_atlas;  /**< high-res EVSM2 cascade array (slotmap-managed). */
    int32_t        static_base;   /**< base layer of this light's cascade run. */
    uint32_t dyn_map;        /**< single R32F 2D distance map for dynamic casters. */
    uint32_t dyn_depth_rb;
    uint32_t fbo;
    bool     static_valid;   /**< true once static casters have been baked. */

    mat4_t view_proj[SHADOW_CSM_MAX_CASCADES]; /**< per-cascade light matrix. */
    float  eye[SHADOW_CSM_MAX_CASCADES][3];    /**< per-cascade virtual eye. */
    float  far_plane[SHADOW_CSM_MAX_CASCADES]; /**< per-cascade distance norm. */
    float  texel_world[SHADOW_CSM_MAX_CASCADES]; /**< world size of one LOD-0 texel;
                                          converts a world penumbra to this
                                          cascade's mip LOD so cross-cascade
                                          samples align. */

    /* Light-frustum tiling (rpg-7s4y). The cached directional map cannot
     * follow the view, so it partitions the LIGHT frustum spatially: a shared
     * light-space basis (@ref light_view) projects the scene into a light-space
     * XY AABB, which is gridded into @ref cascades tiles. Each tile is one
     * cascade -- fit to a SMALL slice of the scene so its static_res texels give
     * high ground density everywhere, not just at the scene centre. A caster
     * renders into every tile its light-space XY box overlaps (in light space a
     * caster and the shadow it throws share the same XY footprint, so no
     * shadow-throw extension is needed). Filled each @ref shadow_csm_update. */
    mat4_t light_view;                              /**< shared classification basis. */
    float  tile_min[SHADOW_CSM_MAX_CASCADES][2];    /**< light-space XY lo per tile. */
    float  tile_max[SHADOW_CSM_MAX_CASCADES][2];    /**< light-space XY hi per tile. */

    mat4_t dyn_view_proj;    /**< single-face ortho matrix for the dynamic map. */
    float  dyn_eye[3];
    float  dyn_far;

    /* Translucency mask (rpg-29zj): translucent casters (opacity < 1) leave
     * the main maps and render into these instead -- per-cascade RGBA16F tint
     * + coverage and R32F distance atlases, plus a low-res dynamic pair.
     * Zero-init = disabled; shadow_csm_mask_init enables. */
    bool           mask_enabled;
    bool           mask_static_valid;
    shadow_atlas_t mask_color_atlas;
    shadow_atlas_t mask_depth_atlas;
    int32_t        mask_color_base;
    int32_t        mask_depth_base;
    uint32_t       mask_fbo;
    uint32_t       mask_depth_rb;
    uint32_t       dyn_mask_color;
    uint32_t       dyn_mask_depth;
    uint32_t       dyn_mask_depth_rb;
    shader_program_t       mask_shader;
    shader_uniform_cache_t mask_cache;
    void (*glDrawBuffers)(int32_t, const uint32_t *);

    void (*glFramebufferTexture2D)(uint32_t, uint32_t, uint32_t, uint32_t, int32_t);
    void (*glGenFramebuffers)(int32_t, uint32_t *);
    void (*glDeleteFramebuffers)(int32_t, const uint32_t *);
    void (*glBindFramebuffer)(uint32_t, uint32_t);
    void (*glFramebufferTextureLayer)(uint32_t, uint32_t, uint32_t, int32_t, int32_t);
    void (*glGenRenderbuffers)(int32_t, uint32_t *);
    void (*glDeleteRenderbuffers)(int32_t, const uint32_t *);
    void (*glBindRenderbuffer)(uint32_t, uint32_t);
    void (*glRenderbufferStorage)(uint32_t, uint32_t, int32_t, int32_t);
    void (*glFramebufferRenderbuffer)(uint32_t, uint32_t, uint32_t, uint32_t);
    void (*glGenTextures)(int32_t, uint32_t *);
    void (*glDeleteTextures)(int32_t, const uint32_t *);
    void (*glBindTexture)(uint32_t, uint32_t);
    void (*glActiveTexture)(uint32_t);
    void (*glTexImage2D)(uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, const void *);
    void (*glTexImage3D)(uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, const void *);
    void (*glTexParameteri)(uint32_t, uint32_t, int32_t);
    void (*glViewport)(int32_t, int32_t, int32_t, int32_t);
    void (*glClearColor)(float, float, float, float);
    void (*glClearBufferfv)(uint32_t, int32_t, const float *);
    void (*glClear)(uint32_t);
    void (*glEnable)(uint32_t);
    void (*glDisable)(uint32_t);
    void (*glDepthFunc)(uint32_t);
    uint32_t (*glGetError)(void);
    uint32_t (*glCheckFramebufferStatus)(uint32_t);
    void (*glReadPixels)(int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, void *);
    void (*glGenerateMipmap)(uint32_t);
    void (*glGetTexImage)(uint32_t, int32_t, uint32_t, uint32_t, void *);
    void (*glTexSubImage3D)(uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, const void *);
} shadow_csm_t;

/**
 * @brief Create both cascade arrays (R32F, @p cascades layers), depth
 *        renderbuffers, FBO and the distance shader. Returns false on any
 *        allocation/compile failure (state left destroyed).
 */
bool shadow_csm_init(shadow_csm_t *csm, const shadow_csm_config_t *config);

/**
 * @brief Classify the static casters by size and fit each cascade's texel-snapped
 *        light matrix (view-INDEPENDENT), for @p light_dir (the direction light
 *        travels). @p scene_min/@p scene_max bound the whole scene (the coarse
 *        background cascade is fit to it and the near planes are extended to it).
 *        Cached forever: returns immediately once the static map is valid, since
 *        nothing here depends on the camera. The owner sets @c static_valid=false
 *        to force a refit on a scene/light change.
 */
void shadow_csm_update(shadow_csm_t *csm, const render_scene_t *scene,
                       const float light_dir[3],
                       const float scene_min[3], const float scene_max[3]);

/**
 * @brief Factor @p n cascades into a @p cols x @p rows grid over a light frustum
 *        of the given @p aspect (light-space width / height). Chooses the factor
 *        pair of @p n closest to @p aspect (square-ish tiles => even texel
 *        density); a prime @p n falls back to a single strip along the longer
 *        axis. Guarded so @p n == 0 yields 1x1. Pure math (no GL); testable.
 * @param cols  [out] tile columns (>=1). @param rows [out] tile rows (>=1).
 */
void shadow_csm_grid_dims(uint32_t n, float aspect,
                          uint32_t *cols, uint32_t *rows);

/**
 * @brief Whether static caster @p r must be drawn into cascade @p cascade, i.e.
 *        its light-space XY box overlaps that tile's rect (plus a small filter
 *        guard). Honours an explicit @c renderable.shadow_cascade tag (drawn only
 *        into the tagged cascade). Valid only after @ref shadow_csm_update has
 *        filled the tile rects; returns false for an out-of-range @p cascade.
 */
bool shadow_csm_caster_in_cascade(const shadow_csm_t *csm,
                                  const render_renderable_t *r,
                                  uint32_t cascade);

/**
 * @brief Render every STATIC caster ([0, scene->dynamic_from)) into the static
 *        array for all cascades and mark the cache valid. Idempotent: skipped
 *        when the cache is already valid. Caller restores its own FBO/viewport.
 */
void shadow_csm_bake_static(shadow_csm_t *csm, const render_scene_t *scene);

/**
 * @brief Render every DYNAMIC caster ([scene->dynamic_from, count)) into the
 *        dynamic array for all cascades (cleared to "no occluder" first, so an
 *        empty dynamic set leaves only the static term). Call every frame.
 */
void shadow_csm_render_dynamic(shadow_csm_t *csm, const render_scene_t *scene);

/**
 * @brief Bind both arrays (units @p unit_static, @p unit_dynamic) and set every
 *        u_csm_* uniform (matrices, eyes, fars, splits, count) on @p program.
 */
void shadow_csm_bind(const shadow_csm_t *csm, shader_uniform_cache_t *cache,
                     const shader_program_t *program, uint32_t unit_static,
                     uint32_t unit_dynamic);

/**
 * @brief Gaussian pre-blur the baked EVSM cascade moments (once per static bake,
 *        before the mip chain is built) so the near-binary moments gain the
 *        variance that becomes a soft penumbra. Called by @ref
 *        shadow_csm_bake_static; NULL-safe.
 */
void shadow_csm_blur_moments(shadow_csm_t *csm);

/**
 * @brief Enable the translucency mask (rpg-29zj): per-cascade tint+coverage /
 *        distance atlas pair + a dynamic 2D pair + the MRT caster shader.
 *        Call after @ref shadow_csm_init. When enabled, the MAIN maps skip
 *        translucent casters (light passes through them). Returns false on
 *        allocation/compile failure (mask stays disabled; main maps intact).
 */
bool shadow_csm_mask_init(shadow_csm_t *csm, const gl_loader_t *loader);

/** @brief Bake the STATIC translucent casters into the mask atlases (once;
 *         call after @ref shadow_csm_bake_static). No-op when disabled. */
void shadow_csm_mask_bake_static(shadow_csm_t *csm,
                                 const render_scene_t *scene);

/** @brief Render the DYNAMIC translucent casters into the dynamic mask pair
 *         (per frame). No-op when disabled. */
void shadow_csm_mask_render_dynamic(shadow_csm_t *csm,
                                    const render_scene_t *scene);

/** @brief Bind the four mask samplers + u_csm_mask_on. Uses distinct units
 *         even when disabled (sampler types must not alias). NULL-safe. */
void shadow_csm_mask_bind(const shadow_csm_t *csm,
                          shader_uniform_cache_t *cache,
                          const shader_program_t *program,
                          uint32_t unit_color, uint32_t unit_depth,
                          uint32_t unit_dyn_color, uint32_t unit_dyn_depth);

/** @brief Release all GL resources. NULL-safe. */
void shadow_csm_destroy(shadow_csm_t *csm);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_SHADOW_CSM_H */
