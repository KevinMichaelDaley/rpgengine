/**
 * @file render_world.h
 * @brief Reusable render-world: the clustered-forward + dynamic-GI assembly
 *        lifted out of hall_lit_dynamic.c (rpg-i3wx).
 *
 * Owns a render_forward_t and a gi_runtime_t and wires the three engine
 * contracts that were previously open-coded in the demo:
 *   - gi cfg.froxel is forced to equal fwd cfg.cluster (identical froxels),
 *   - forward's material_extra_bind is set to the GI bind at texture unit 24,
 *   - probe grid + all GI setters are applied from config.
 * It renders a caller-owned render_scene_t (borrowed) each frame via
 * gi_runtime_frame + render_forward_render. No global state.
 *
 * This is a GL module (renderer lib). Ownership: forward + GI are owned; the
 * scene and all meshes/materials/textures/lights it references are borrowed.
 */
#ifndef FERRUM_RENDERER_RENDER_WORLD_H
#define FERRUM_RENDERER_RENDER_WORLD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/renderer/render_forward.h"
#include "ferrum/renderer/render_scene.h"
#include "ferrum/renderer/gi/gi_runtime.h"
#include "ferrum/renderer/gi/gi_sdf.h" /* gi_collider_t */

/**
 * @brief Everything needed to assemble a render world.
 *
 * @c forward is filled by the caller EXCEPT material_extra_bind/user, which the
 * module overwrites to route GI. @c scene is the caller-owned scene to render.
 * GI is enabled when @c gi_enabled != 0; then the probe grid + optional
 * static-volume / weights / sky-AO / spec-gain setters are applied. Values
 * mirror the demo's fields 1:1.
 */
struct probe_brick_data;
struct probe_brick_index;

typedef struct render_world_config {
    render_forward_config_t forward;   /**< forward+ config (cluster, lights, sh, shadows). */
    render_scene_t         *scene;      /**< borrowed scene to render (required). */

    int          gi_enabled;
    const char  *gi_sdf_prefix;
    gi_sdf_stream_t *gi_ext_sdf;  /**< optional externally-owned SDF stream (streamed
                                   *   residency); if set, gi_runtime borrows it
                                   *   instead of self-loading @c gi_sdf_prefix. */
    float        gi_aabb_min[3], gi_aabb_max[3];
    const struct probe_brick_data *gi_bricks;       /**< offline .bricks (nullable). */
    const struct probe_brick_index *gi_brick_index; /**< rebuilt voxel index (with gi_bricks). */
    const float *gi_probe_pos;          /**< [gi_probe_count*3], copied by gi_runtime_init. */
    uint32_t     gi_probe_count;
    const float *gi_baked_sh;           /**< [gi_probe_count*24] baked probe SH (NULL = converge). */
    const float *gi_baked_sg;           /**< [gi_probe_count*24] baked probe SG. */
    uint32_t     gi_baked_count;        /**< baked SH/SG probe count. */
    uint32_t     gi_max_probes;         /**< probe backing cap for runtime set-probe
                                         *   updates (streamed probes); 0 => fixed. */
    float        gi_grid_cell;
    int          gi_prepass_w, gi_prepass_h;
    uint32_t     gi_max_lights, gi_max_boxes;
    float        gi_soft_k;
    int          gi_update_interval, gi_n_probe_groups, gi_freeze_ticks;
    float        gi_smooth;             /**< probe temporal-EMA blend (0 -> 0.15). */
    gi_probe_tuning_t gi_tuning;        /**< full probe-GI tuning from render_config. */
    uint32_t     gi_probe_min;
    float        gi_probe_sphere_margin;
    int          gi_bin_interval;

    /* Trilinear probe grid (dim[0]==0 disables). */
    float gi_grid_origin[3], gi_grid_cell3[3];
    int   gi_grid_dim[3];

    /* Optional GI setters (each gated by its has_* flag). */
    int          has_static_volume;
    unsigned int static_vol_tex;
    float        static_vol_origin[3], static_vol_dim[3], static_vol_voxel, static_k;
    int          has_static_weights; float static_baked_w, static_dyn_w;
    int          has_sky_ao; float sky_ao_color[3], sky_ao_ref, sky_ao_mult;
    int          has_spec_gain; float spec_gain;
} render_world_config_t;

/** The assembled render world. Treat fields as read-only from outside. */
typedef struct render_world {
    render_forward_t forward;
    gi_runtime_t     gi;
    render_scene_t  *scene;    /**< borrowed. */
    int              gi_enabled;
} render_world_t;

/** Assemble the world (inits forward + GI, applies setters). @return false on error. */
bool render_world_init(render_world_t *rw, const render_world_config_t *cfg);
/** Free owned resources (forward, GI). Does not touch the borrowed scene. */
void render_world_destroy(render_world_t *rw);

/** Per frame: run the GI dispatch then the forward+ render, using the scene's
 *  current camera. @p boxes are the dynamic GI colliders. Assumes the target
 *  FBO/viewport is bound. */
void render_world_update(render_world_t *rw, const gi_collider_t *boxes,
                         uint32_t n_boxes, int screen_w, int screen_h);

/** Replace the GI probe set at runtime (streamed / per-zone probes, rpg-zygg):
 *  forwards to gi_runtime_set_probes. @p count clamped to the init max_probes. */
void render_world_set_probes(render_world_t *rw, const float *pos, uint32_t count);

/** Provide the GI runtime an external visible-SDF-chunk mask (shared dual prepass,
 *  rpg-sazm); forwards to gi_runtime_set_visible. NULL restores the internal pass. */
void render_world_set_visible(render_world_t *rw, const uint8_t *visible, int n_chunks);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_RENDER_WORLD_H */
