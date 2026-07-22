/**
 * @file render_config.h
 * @brief Headless render-world tuning config, loadable from JSON (rpg-da8c).
 *
 * The client's forward+ / shadow / GI tuning was hardcoded in client_scene_load.c
 * (and had drifted from the hall_lit_dynamic reference). This is the single source
 * of truth for those SCALAR tunables: a plain-data struct with defaults that match
 * hall_lit_dynamic, plus a JSON overlay parser (present keys override, missing keys
 * keep the default). Data-driven, GL-free -- the GL client (client_scene_load)
 * reads these fields into its render_world_config_t and supplies the runtime bits
 * (textures, scene pointer, scene AABB, sun from the descriptor, streamed probes).
 *
 * A level ships its own render config, or a world/zone supplies one per zone
 * (world_desc), or the engine default (render_config_defaults) is used.
 *
 * Ownership: render_config_t is a flat value type (no owned pointers); copy freely.
 */
#ifndef FERRUM_SCENE_RENDER_CONFIG_H
#define FERRUM_SCENE_RENDER_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct arena;

/** Scalar render-world tuning parameters (see render_config_defaults for values). */
typedef struct render_config {
    /* Forward+ froxel cluster. */
    uint32_t cluster_tiles_x, cluster_tiles_y, cluster_slices;
    float    cluster_near, cluster_far;
    uint32_t max_lights;

    /* Baked-lightmap sampling (sh_scale is the over-bright tuning knob). Note the
     * renderer remaps sh_scale==0 to 1.0, so use sh_enabled to turn the lightmap
     * OFF: -1 = auto (on iff a lightmap texture is present), 0 = force off, 1 = on. */
    int      sh_enabled;
    float    sh_scale;
    float    sh_normal_bias;
    float    ambient[3];

    /* Directional sun + cascaded shadow maps. */
    float    sun_energy_scale;      /**< descriptor sun intensity -> radiance scale. */
    uint32_t dir_cascades;
    uint32_t dir_static_res, dir_dynamic_res;
    float    dir_lambda, dir_bias, dir_softness, dir_max_distance;
    int      dir_pcss;
    int      dir_translucency; /**< translucent casters -> tinted sun shadows
                                *   via the CSM mask (rpg-29zj). Default 1. */
    int      dir_caustics;     /**< SDF-traced caustic refinement of the mask
                                *   (rpg-kbqd, GL 4.3; graceful fallback).
                                *   Default 1. */

    /* Point (cube) shadows. */
    uint32_t shadow_max, shadow_res;
    float    shadow_near, shadow_far_scale, shadow_bias; /**< far = scene diag * far_scale. */

    /* GI runtime. */
    int      gi_enabled;
    float    gi_grid_cell, gi_soft_k;
    uint32_t gi_max_lights, gi_max_boxes, gi_probe_min;
    float    gi_probe_sphere_margin;
    int      gi_bin_interval, gi_update_interval, gi_n_probe_groups;
    float    gi_smooth;             /**< steady-state probe temporal-EMA blend (0..1;
                                     *   smaller = smoother/slower convergence). */
    /* Probe-GI tuning that used to be env-only (GI_*), rpg-2vfm. The GI_* vars are
     * still honoured as a live-tuning override, but these are the source of truth. */
    int      gi_field;              /**< DDGI recurrent gather (0 = pure SDF march). */
    int      gi_mis;                /**< MIS-sampled march directions. */
    int      gi_hybrid;             /**< field bounce + hero SDF marches. */
    int      gi_hero;               /**< hero SDF marches per probe (0..4). */
    int      gi_samples;            /**< source samples per gather (>= sources => exact). */
    int      gi_spec_lobes;         /**< SG specular lobes per probe (0..3). */
    float    gi_bounce;             /**< transport gain; steady state = 1/(1-gain). */
    float    gi_near;               /**< direct-sample vs stochastic-gather threshold (m). */
    float    gi_dmax;               /**< nearest-surface distance for a SOURCE probe. */
    float    gi_emin;               /**< emission luminance for a SOURCE probe. */
    float    gi_norm_gate;          /**< |sdf| under which a probe is a SURFACE probe. */
    float    gi_stat_scale;         /**< scale on the probes' STATIC bounce gather. */
    float    gi_vis_bias;           /**< Chebyshev self-visible band (probe dot artifacts). */
    float    gi_vis_varmin;         /**< Chebyshev variance floor (softer falloff). */
    float    gi_vis_sharp;          /**< Chebyshev falloff exponent (1 soft, 2 sharp). */
    /* Offline post-bake probe placement (rpg-pjkb; consumed by build/probe_bake,
     * which ships its result as the level's manual .probes + .bricks files). */
    float    gi_brick_coarse;       /**< coarsest brick edge (m). */
    int      gi_brick_levels;       /**< ternary hierarchy depth (1..4). */
    int      gi_brick_fill;         /**< keep failing coarse bricks (open-air GI). */
    float    gi_brick_buried;       /**< cull bricks buried deeper than this x probe spacing. */
    float    gi_fixup_clearance;    /**< virtual-offset target SDF clearance (m). */
    float    gi_fixup_max_push;     /**< virtual-offset displacement cap (m). */
    float    gi_ray_clamp;          /**< per-ray radiance cap in the probe march (fireflies). */
    /* Texture/AA quality (were FR_ANISO / FR_MSAA). */
    float    aniso;                 /**< max anisotropy for material textures (1 = off). */
    int      msaa;                  /**< MSAA samples for the client context (0/1 = off). */
    float    probe_spacing_scale;   /**< multiplies the descriptor probe spacing
                                     *   (<1 = denser/more probes; 1 = as authored). */
    float    gi_aabb_pad_lo[3], gi_aabb_pad_hi[3]; /**< inset applied to the probe AABB. */

    /* GI static/irradiance/spec/sky-AO weights. */
    float    static_baked_w, static_dyn_w, static_k, spec_gain;
    float    sky_ao_color[3], sky_ao_ref, sky_ao_mult;

    /* ---------------------------------------------------------------------
     * Low-end performance knobs (rpg-vwyk / rpg-iplq). Every field here
     * DEFAULTS to a value that reproduces today's behavior, so a configless
     * level is unchanged; a preset or per-level override opts into cheaper
     * rendering. Fields are consumed by their respective subsystems as those
     * code tickets land -- until then they parse and thread through harmlessly.
     * See ref/renderer_performance_issues_and_suggested_fixes.md section 7.
     * --------------------------------------------------------------------- */

    /* Quality / resolution. */
    float    render_scale;          /**< internal render-target scale, then blit to
                                     *   the swapchain; 1 = native (rpg-iplq). */
    int      pbr_quality;           /**< forward PBR shader variant: 0 low, 1 med,
                                     *   2 full (default 2 = today's ubershader). */
    int      texture_quality;       /**< top mip levels dropped at load (0 = full res). */
    int      depth_prepass;         /**< -1 auto (on when >1 renderable, as today),
                                     *   0 force off, 1 force on. Was env PBR_NOPREPASS. */

    /* Shadow budget / quality. */
    int      shadow_fp16;           /**< 1 = R16F shadow maps + 16-bit depth (0 = R32F). */
    int      shadow_update_interval;/**< cube-shadow slots refreshed per frame in a
                                     *   round-robin; 0 = all slots every frame (today). */
    float    shadow_distance;       /**< drop cube-shadow slots beyond this many metres
                                     *   from the camera; 0 = unlimited (today). */
    int      shadow_static_cache;   /**< 1 = re-render a cube-shadow slot only when its
                                     *   light or an intersecting dynamic caster moved. */
    int      dir_pcf_taps;          /**< CSM PCF taps per cascade: 8/4/1 (1 = hard). */
    int      shadow_pcf_taps;       /**< point-shadow cube PCF taps: 8/4/1 (1 = hard). */
    int      dir_dynamic_interval;  /**< dynamic CSM map refresh every N frames (1 = every). */

    /* Baked lightmap resource cost. */
    int      lightmap_bands;        /**< SH bands sampled: 9 (full) or 4 (L1). */
    int      lm_fp16;               /**< 1 = RGBA16F SH atlases (0 = RGB32F, today). */
    int      lm_resident_layers;    /**< max resident streamed lightmap chunk layers. */

    /* GI march / SDF / dynamic voxelization cost. */
    int      gi_dyn_voxel;          /**< dynamic-object voxelization: 0 off (static GI
                                     *   only), 1 on GI-tick cadence, 2 every frame (today). */
    float    gi_march_quality;      /**< SDF march step/count scale (1 = full, <1 = coarser). */
    int      gi_frag_quality;       /**< fragment probe gather: 0 low (4-corner/no vis),
                                     *   1 full 8-corner (today). */
    int      gi_prepass_scale;      /**< visibility prepass downscale divisor (8 = w/8, today). */
    int      gi_probe_cap;          /**< max probes solved per tick; 0 = unlimited (today). */
    float    gi_adaptive_ms;        /**< skip a GI tick if the previous frame exceeded this
                                     *   many ms; 0 = never skip (today). */
    int      sdf_fp16;              /**< 1 = RGBA16F SDF chunks (0 = RGBA32F, today). */
    int      sdf_resident_slots;    /**< resident SDF chunk 3D-texture slots. */
    int      sdf_uploads_per_frame; /**< max SDF chunk page-ins per frame; 0 = uncapped (today). */

    /* Streaming budgets (were hardcoded / uncapped). */
    int      stream_upload_mb_per_frame; /**< per-frame texture upload byte budget in MiB;
                                     *   0 = uncapped (today). */
    int      stream_ram_budget_mb;  /**< streamer system-RAM budget (default 2048). */
    int      stream_vram_budget_mb; /**< streamer VRAM budget (default 2048). */

    /* Misc. */
    float    draw_distance;         /**< far cull distance for view/shadow draws;
                                     *   0 = unlimited (today). */
} render_config_t;

/**
 * @brief Fill @p rc with the engine default tuning (matches hall_lit_dynamic).
 *        Never fails. Call before parse to establish the baseline the JSON overlays.
 */
void render_config_defaults(render_config_t *rc);

/**
 * @brief Parse a render-config JSON object over the defaults: @p out is first set
 *        to render_config_defaults, then every key PRESENT in @p json overrides its
 *        field (missing keys keep the default). @p arena backs the JSON scratch
 *        tree (no malloc). @return false on NULL args, a non-object root, or a
 *        malformed document (then @p out is untouched-invalid; don't use it).
 */
bool render_config_parse(const char *json, size_t len, struct arena *arena,
                         render_config_t *out);

/**
 * @brief Load + parse a render-config JSON file into @p out. Missing file or parse
 *        failure returns false. @p arena backs the file buffer + JSON scratch.
 */
bool render_config_load(const char *path, struct arena *arena, render_config_t *out);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_SCENE_RENDER_CONFIG_H */
