/**
 * @file shadow_csm.h
 * @brief Cascaded shadow maps for a stationary directional light (rpg-fsvq).
 *
 * The cascades are VIEW-INDEPENDENT: because the static map is baked once and
 * cached forever (the sun is stationary and the player may roam the whole
 * scene), fitting the cascades to the camera frustum would clip out casters
 * behind the view (a back wall would stop shadowing). Instead each STATIC caster
 * is classified into a cascade by SIZE / "background"-ness (see @ref
 * shadow_csm_cascade_of): big background structures (walls, vaults, floor) land
 * in a coarse cascade fit to the WHOLE scene, while smaller localised objects
 * land in finer cascades fit tightly to their own bounds (higher effective
 * resolution). At shade time a fragment samples EVERY cascade whose light box
 * contains it and takes the union of their occlusion, so a big-structure shadow
 * and a small-prop shadow both apply regardless of where the camera is.
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

    /* Caster size -> cascade classification (natural-log of the world-AABB
     * diagonal), computed over the static casters in shadow_csm_update and reused
     * to place each caster in both the fit and the draw. Smallest size -> cascade
     * 0 (finest); largest -> the last cascade (coarse, whole-scene background). */
    float  size_log_min;
    float  size_log_max;

    mat4_t dyn_view_proj;    /**< single-face ortho matrix for the dynamic map. */
    float  dyn_eye[3];
    float  dyn_far;

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
 * @brief The cascade a caster is assigned to. Honours an explicit tag
 *        (@c renderable.shadow_cascade in [0,cascades)); otherwise classifies by
 *        the caster's world-AABB size against the range found in the last
 *        @ref shadow_csm_update (small -> fine cascade 0, large -> coarse last).
 *        Returns 0 before any classification. Used by both the fit and the draw
 *        so a caster always lands in the cascade its box was fit to include.
 */
uint32_t shadow_csm_cascade_of(const shadow_csm_t *csm,
                               const render_renderable_t *r);

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

/** @brief Release all GL resources. NULL-safe. */
void shadow_csm_destroy(shadow_csm_t *csm);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_SHADOW_CSM_H */
