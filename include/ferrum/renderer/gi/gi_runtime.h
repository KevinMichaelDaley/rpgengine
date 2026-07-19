/**
 * @file gi_runtime.h
 * @brief Dynamic-light SDF-probe GI runtime (rpg-fo9r): ties the whole pipeline
 *        together so a caller only does the final per-frame invocation.
 *
 * Owns the visibility prepass, the streamed baked-SDF residency, the adaptive
 * probe set + its lookup accel grid, and the GPU probe-update compute. Each
 * frame it: runs the WORLD-mode prepass to page the on-screen SDF chunks, marches
 * every probe to every dynamic light through the resident combined SDF (baked
 * chunks + dynamic collider boxes) on the GPU, and exposes probe + accel texture
 * buffers so the forward+ material samples the nearest probes' SH for a dynamic
 * indirect term. Everything is in renderer modules; the demo only calls
 * @ref gi_runtime_frame + @ref gi_runtime_bind. Needs a GL 4.3 context.
 */
#ifndef FERRUM_RENDERER_GI_GI_RUNTIME_H
#define FERRUM_RENDERER_GI_GI_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/cluster_grid.h"
#include "ferrum/renderer/render_scene.h"
#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/shader_uniforms.h"
#include "ferrum/renderer/gi/gi_vis_prepass.h"
#include "ferrum/renderer/gi/gi_sdf_stream.h"
#include "ferrum/renderer/gi/gi_probe_gpu.h"
#include "ferrum/renderer/gi/gi_probe_set.h"
#include "ferrum/renderer/gi/gi_probe_grid.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Setup for @ref gi_runtime_init. */
typedef struct gi_runtime_config {
    const gl_loader_t *loader;
    const char *sdf_prefix;      /**< baked SDF sidecar prefix (<prefix>_cNNN.sdf). */
    float aabb_min[3], aabb_max[3]; /**< probe play volume (accel-grid bounds). */
    const float *probe_pos_in;   /**< explicit probe positions (3/probe) for manual
                                  *   adaptive placement; NULL = seed a lattice. */
    uint32_t n_probe_in;         /**< count for @c probe_pos_in (0 = auto-seed). */
    float probe_spacing;         /**< auto-seed spacing (m) when no explicit set. */
    float grid_cell;             /**< accel-grid cell size (m). */
    int   prepass_w, prepass_h;  /**< SDF vis-prepass resolution. */
    uint32_t max_lights, max_boxes;
    float soft_k;                /**< penumbra sharpness. */
    int   update_interval;       /**< recompute probes every N frames (0 -> 8). */
    int   n_probe_groups;        /**< stagger: spread the probe trace over N frames,
                                  *   a spatially-dithered 1/N slice each frame
                                  *   (0 -> update_interval; 1 = no stagger). */
    cluster_config_t froxel;     /**< MUST match the forward+ cluster config so
                                  *   probes bin into the exact same froxels. */
    uint32_t probe_min;          /**< guaranteed K-nearest probes per froxel (0 -> 4). */
    float probe_sphere_margin;   /**< world halo added to each froxel sphere (m, 0->1.5). */
    int   bin_interval;          /**< re-bin probes into froxels every N frames (0 -> 1). */
} gi_runtime_config_t;

/** The dynamic-GI runtime (owns everything). */
typedef struct gi_runtime {
    gi_vis_prepass_t pp;
    gi_sdf_stream_t  sdf;
    gi_probe_gpu_t   gpu;
    gi_probe_set_t   probes;
    gi_probe_grid_t  grid;
    float           *probe_pos, *probe_sh;   /**< probe backing. */
    uint32_t        *cell_start, *probe_idx; /**< accel backing. */
    unsigned int     tbo_cs, tbo_cs_tex;     /**< accel cell_start buffer texture. */
    unsigned int     tbo_pi, tbo_pi_tex;     /**< accel probe_idx buffer texture. */
    float            box_min[GI_VIS_MAX_BOXES * 3];
    float            box_max[GI_VIS_MAX_BOXES * 3];
    int              n_sdf_boxes;
    struct gi_light *light_scratch; /**< converted DYNAMIC_INDIRECT scene lights. */
    uint32_t         max_lights;
    float            soft_k;
    int              update_interval; /**< recompute cadence (frames). */
    int              n_groups;        /**< staggered dithered probe groups (>=1). */
    int              frame_counter;
    /* Per-object weights for the probe STATIC indirect (rpg-pau4). baked_w scales
     * it for lightmapped surfaces (small: extra bounce only); dyn_w for dynamic
     * objects (their only static ambience). Bound as forward+ uniforms. */
    float            static_baked_w;
    float            static_dyn_w;
    /* Regular probe-grid layout for trilinear indirect (rpg-pau4). grid_on=0 ->
     * the forward+ falls back to froxel nearest-probe blending. */
    int              probe_grid_on;
    float            probe_grid_origin[3];
    float            probe_grid_cell[3];
    int              probe_grid_dim[3];
    /* Sky-openness AO from the probe depth maps (0 colour = off). */
    float            sky_ao_color[3];
    float            sky_ao_ref;
    float            spec_gain;   /**< probe specular master scale (0 = off). */
    int              spec_lobes;  /**< SG lobes summed per probe (1..3; GI_SG_LOBES). */
    float            ao_mult;     /**< openness->AO multiply on indirect (0..1). */
    bool             ready;

    /* --- Probe froxel binning: probes assigned to the SAME froxels the forward+
     * lights use, so a fragment reads its probe candidates from its own cluster
     * (via the forward+ cluster uniforms) instead of a separate world grid. --- */
    cluster_grid_t   froxel;          /**< probe cluster grid (forward+ geometry). */
    uint32_t        *fx_off, *fx_cnt, *fx_idx; /**< froxel grid backing. */
    unsigned int     tbo_fo, tbo_fo_tex;  /**< froxel offset buffer texture. */
    unsigned int     tbo_fc, tbo_fc_tex;  /**< froxel count buffer texture. */
    unsigned int     tbo_fi, tbo_fi_tex;  /**< froxel index buffer texture. */
    uint32_t         probe_min;
    float            probe_sphere_margin;
    int              bin_interval;
    unsigned int     fx_last_unit;    /**< last texture unit used by the froxel bind. */
} gi_runtime_t;

/** @brief Build the whole runtime. Returns false on any failure. */
bool gi_runtime_init(gi_runtime_t *gi, const gi_runtime_config_t *cfg);

/**
 * @brief Per frame: page the visible SDF chunks (world prepass over @p scene with
 *        @p view/@p proj), then GPU-march every probe to every scene light tagged
 *        RENDER_LIGHT_FLAG_DYNAMIC_INDIRECT (read from @p scene->lights) through
 *        the resident combined SDF + dynamic @p boxes (@p n_boxes). @p main_w/
 *        @p main_h restore the viewport.
 */
void gi_runtime_frame(gi_runtime_t *gi, const render_scene_t *scene,
                      const float view[16], const float proj[16],
                      const gi_collider_t *boxes, uint32_t n_boxes,
                      int main_w, int main_h);

/**
 * @brief Bind the probe + accel texture buffers and set the u_gi_* uniforms on
 *        @p program (via @p cache), starting at texture unit @p base_unit, so the
 *        forward+ material's probe sampler is live for the next draw.
 */
void gi_runtime_bind(const gi_runtime_t *gi, shader_uniform_cache_t *cache,
                     const shader_program_t *program, uint32_t base_unit);

/**
 * @brief Bind a static irradiance volume (rpg-pau4) so the probe cone trace
 *        gathers the baked-lightmap ambience in addition to the dynamic-light
 *        term. Forwards to @ref gi_probe_gpu_set_static; @p tex == 0 disables it.
 *        The texture is not owned. NULL-safe.
 */
void gi_runtime_set_static_volume(gi_runtime_t *gi, unsigned int tex,
                                  const float origin[3], const float dim[3],
                                  float vox, float k);

/**
 * @brief Set the per-object static-indirect weights (rpg-pau4): @p baked_w for
 *        lightmapped surfaces (small -- they already have the bake), @p dyn_w for
 *        dynamic objects (their only source of static ambience). NULL-safe.
 */
void gi_runtime_set_static_weights(gi_runtime_t *gi, float baked_w, float dyn_w);

/**
 * @brief Declare that the probes form a regular grid so the forward+ samples
 *        indirect by TRILINEAR interpolation of the 8 surrounding probes instead
 *        of froxel nearest-probe blending (rpg-pau4). Probe index must be
 *        (z*dim[1] + y)*dim[0] + x. @p origin is the world position of probe
 *        (0,0,0); @p cell is the per-axis spacing. Pass dim all-zero to disable.
 *        NULL-safe.
 */
void gi_runtime_set_probe_grid(gi_runtime_t *gi, const float origin[3],
                               const float cell[3], const int dim[3]);

/**
 * @brief Set the sky-openness ambient AO (rpg-hw75 prelude): @p color is the
 *        constant sky ambient added where the probe depth maps see open sky
 *        overhead; @p ref is the overhead distance (m) treated as fully open.
 *        @p color NULL or zero disables it. NULL-safe.
 */
void gi_runtime_set_sky_ao(gi_runtime_t *gi, const float color[3], float ref, float ao_mult);

/** @brief Master scale for the probe SG specular reflection (rpg-hw75); 0 = off. */
void gi_runtime_set_spec_gain(gi_runtime_t *gi, float gain);

/** @brief Free everything. NULL-safe. */
void gi_runtime_destroy(gi_runtime_t *gi);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_GI_GI_RUNTIME_H */
