/**
 * @file render_forward.h
 * @brief Clustered forward+ pipeline driver: wires a submitted @ref render_scene
 *        (renderables + camera + lights) through depth pre-pass -> clustered
 *        light cull -> forward+ PBR shading, as nodes of a @ref
 *        render_pipeline_graph.
 *
 * This is the mainline realtime lit path. Callers construct a scene and call
 * @ref render_forward_render; the demo/tests own the scene, never the pipeline.
 * The three stages (@ref depth_prepass, @ref cluster_grid, @ref forward_plus)
 * are driven from the graph nodes' callbacks via the driver context. Cluster and
 * light-scratch storage are allocated once at init (not per frame).
 *
 * Ownership: the driver owns its GL resources (shader, forward+ buffers, cluster
 * storage) and frees them in @ref render_forward_destroy. The scene passed to
 * @ref render_forward_render is borrowed for the duration of the call.
 */
#ifndef FERRUM_RENDERER_RENDER_FORWARD_H
#define FERRUM_RENDERER_RENDER_FORWARD_H

#include <stdint.h>

#include "ferrum/renderer/cluster_grid.h"
#include "ferrum/renderer/depth_prepass.h"
#include "ferrum/renderer/forward_plus.h"
#include "ferrum/renderer/render_pipeline_graph.h"
#include "ferrum/renderer/render_scene.h"
#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/shader_uniforms.h"
#include "ferrum/renderer/shadow_csm.h"
#include "ferrum/renderer/shadow_cube.h"
#include "ferrum/renderer/shadow_spot.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Setup parameters for @ref render_forward_init (all borrowed / by value). */
typedef struct render_forward_config {
    const gl_loader_t *loader;       /**< GL entry-point loader. */
    cluster_config_t   cluster;      /**< froxel grid (tiles/slices/near/far). */
    uint32_t           max_lights;   /**< light-scratch capacity. */
    uint32_t           index_capacity;/**< cluster light-index buffer capacity. */
    float              screen_w;      /**< viewport width (px). */
    float              screen_h;      /**< viewport height (px). */
    float              sun_dir[3];    /**< static directional fill direction. */
    float              sun_color[3];  /**< directional fill colour. */
    float              ambient[3];    /**< flat ambient term. */
    int                sh_enabled;    /**< bind a baked SH lightmap (units 7-15). */
    uint32_t           sh_tex[9];     /**< 9 RGB32F SH-coeff atlas texture ids. */
    float              sh_scale;      /**< lightmap intensity multiplier (0 -> 1). */
    int                shadow_light;  /**< flat light index to cast a cube shadow (-1 = none). */
    uint32_t           shadow_res;    /**< cube face resolution (0 = no shadow). */
    float              shadow_near;   /**< cube shadow near plane. */
    float              shadow_far;    /**< cube shadow far plane. */
    float              shadow_bias;   /**< distance-compare bias (world units). */
    int                spot_light;    /**< flat light index to cast a spot shadow (-1 = none). */
    uint32_t           spot_res;      /**< spot map resolution (0 = no spot shadow). */
    float              spot_near;     /**< spot shadow near plane. */
    float              spot_far;      /**< spot shadow far plane. */
    float              spot_bias;     /**< spot distance-compare bias. */
    uint32_t           dir_cascades;    /**< sun CSM cascade count (0 = no sun shadow). */
    uint32_t           dir_static_res;  /**< per-cascade static map resolution. */
    uint32_t           dir_dynamic_res; /**< per-cascade dynamic map resolution. */
    float              dir_lambda;      /**< cascade split blend (0=uniform,1=log). */
    float              dir_bias;        /**< directional distance-compare bias. */
    float              dir_max_distance;/**< cap the shadowed range (0 = camera far). */
} render_forward_config_t;

/** Clustered forward+ driver context: the stages, their GL resources, the
 *  pipeline graph, and the scene being rendered. Opaque to callers except that
 *  they allocate one and pass it to the render/destroy calls. */
typedef struct render_forward {
    depth_prepass_t         depth;
    cluster_grid_t          clusters;
    forward_plus_t          fp;
    shadow_cube_t           shadow;
    shadow_spot_t           spot;
    shadow_csm_t            csm;
    shader_program_t        pbr;
    shader_uniform_cache_t  cache;
    render_forward_config_t cfg;
    const render_scene_t   *scene;    /**< set per @ref render_forward_render. */

    uint32_t *offsets;   /**< owned cluster storage. */
    uint32_t *counts;
    uint32_t *indices;
    float    *light_data;/**< owned pack scratch (max_lights*16). */

    render_pass_t                passes[3];
    render_pipeline_graph_node_t nodes[3];
    render_pipeline_graph_t      graph;
    const char                  *dep_fwd[2]; /**< {"depth_pre","light_cull"} */
} render_forward_t;

/**
 * @brief Initialise the driver: compile the PBR program, create the depth
 *        pre-pass + forward+ buffers, and allocate cluster/light scratch sized
 *        by @p cfg. Wires the three graph nodes (depth_pre -> light_cull ->
 *        forward). Returns true on success; on failure the driver is left
 *        destroyed (safe to ignore).
 */
bool render_forward_init(render_forward_t *fwd, const render_forward_config_t *cfg);

/**
 * @brief Render @p scene through the forward+ graph: depth pre-pass, cluster the
 *        scene's lights for the camera, upload them, then forward+ PBR shading of
 *        every renderable. Depth pre-pass runs when the scene has >1 renderable.
 *        Assumes the target framebuffer + viewport are already bound/cleared.
 */
void render_forward_render(render_forward_t *fwd, const render_scene_t *scene);

/** @brief Release all GL resources and owned storage. NULL-safe. */
void render_forward_destroy(render_forward_t *fwd);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_RENDER_FORWARD_H */
