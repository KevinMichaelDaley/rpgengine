/**
 * @file client_scene.h
 * @brief Descriptor-driven client scene: load a level's assets from disk and
 *        assemble a render_world (rpg-8302).
 *
 * The reusable, descriptor-driven counterpart of hall_lit_dynamic.c's inline
 * loading: given a parsed scene_desc_t + a base directory, it loads each object's
 * mesh (fvma/dmesh) and material textures, the baked lightmap SH arrays, places
 * the probe grid, and builds a render_world over a render_scene of the level's
 * objects. The client then appends networked bodies as dynamic renderables and
 * calls client_scene_render each frame. GL module (renderer lib).
 *
 * Ownership: owns all loaded GL meshes/textures/lightmap arrays + the render_world
 * and its scene backing; borrows nothing from the descriptor after load.
 */
#ifndef FERRUM_RENDERER_CLIENT_SCENE_H
#define FERRUM_RENDERER_CLIENT_SCENE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/renderer/render_world.h"
#include "ferrum/renderer/render_scene.h"
#include "ferrum/renderer/material.h"
#include "ferrum/renderer/texture.h"
#include "ferrum/renderer/mesh/static_mesh.h"
#include "ferrum/renderer/light_store.h"
#include "ferrum/renderer/gi/gi_static_volume.h"
#include "ferrum/renderer/gi/gi_vis_prepass.h"
#include "ferrum/renderer/gi/gi_voxelize.h"
#include "ferrum/lightmap/lm_atlas.h"

struct scene_desc;    /* ferrum/scene/scene_desc.h */
struct render_config; /* ferrum/scene/render_config.h */

/**
 * @brief Decode an image file to a tightly-packed 3-channel (RGB) buffer.
 *
 * The caller (which owns the image codec, e.g. stb_image) provides this so the
 * renderer lib stays codec-free. On success returns true and sets @p out_px to a
 * malloc'd RGB buffer (freed by client_scene) + @p out_w/@p out_h.
 */
typedef bool (*client_image_load_fn)(const char *path, int *out_w, int *out_h,
                                     unsigned char **out_px);

/** A loaded, renderable level scene. Fields are owned; treat as read-only. */
typedef struct client_scene {
    render_world_t       world;
    render_scene_t       scene;
    render_renderable_t *rb;          /**< scene backing [object_count + slack]. */
    uint32_t             rb_cap;
    static_mesh_t       *meshes;      /**< [object_count]. */
    uint32_t             mesh_count;
    texture_t           *textures;    /**< [material_count * texcount]. */
    uint32_t             texture_count;
    render_material_t   *materials;   /**< [material_count]. */
    uint32_t             material_count;
    unsigned int         sh_tex[9];   /**< baked-lightmap SH coeff arrays (0 = none). */
    int                  sh_borrowed;  /**< 1 = sh_tex owned by the light streamer (don't delete). */
    gi_static_volume_t   static_vol;   /**< baked-irradiance volume for the probe GI. */
    float               *probe_pos_full;/**< full generated probe set [probe_count_full*3]. */
    uint32_t             probe_count_full;
    float               *probe_scratch; /**< [probe_count_full*3] scratch for the resident subset. */
    uint32_t             probe_resident; /**< last resident probe count pushed (churn guard). */
    gi_vis_prepass_t     gi_pp;          /**< shared dual visibility prepass (SDF + lm chunks). */
    int                  gi_pp_ready;    /**< 1 = gi_pp initialised. */
    gi_voxelize_t        vox;            /**< dynamic-geometry voxeliser (GI colour bleed). */
    int                  vox_ready;      /**< 1 = vox initialised. */
    uint32_t            *dyn_idx;        /**< [dyn_count] renderable indices of DYNAMIC objects. */
    float               *dyn_albedo;     /**< [dyn_count*3] their material albedo. */
    gi_collider_t       *dyn_col;        /**< [dyn_count] world-AABB proxies so dynamic
                                          *   objects also OCCLUDE in the probe SDF. */
    uint32_t             dyn_count;      /**< dynamic objects (0 = none; term disabled). */
    render_light_store_t lights;
    render_light_t      *light_buf;   /**< lights backing. */
    const gl_loader_t   *loader;
    int                  static_count;/**< number of static (level) renderables. */
} client_scene_t;

/**
 * @brief Load a level's assets from @p base_dir per @p desc and assemble the
 *        render_world. @p base_dir is prepended to relative asset paths.
 *
 * Baked lightmap: if @p ext_sh_tex is non-NULL the baked SH pages are supplied by
 * an external light-data streamer (client_light_stream) -- @p ext_sh_tex[9] are
 * borrowed GL_TEXTURE_2D_ARRAY ids, @p ext_mrect[desc->object_count] the per-mesh
 * atlas rects, and @p ext_atlas the atlas dims -- and this function neither reads
 * the .flm nor owns the textures (the streamer pages layers; the caller sets each
 * item's sh_layer per frame). If @p ext_sh_tex is NULL the lightmap is loaded
 * synchronously from @p desc->lightdata (legacy path).
 *
 * @return false on a fatal error (OOM, render_world init). Missing individual
 *         textures fall back to a debug pattern (non-fatal).
 */
bool client_scene_load(client_scene_t *cs, const gl_loader_t *loader,
                       const struct scene_desc *desc, const char *base_dir,
                       client_image_load_fn image_load, int screen_w, int screen_h,
                       const unsigned int *ext_sh_tex,
                       const lm_atlas_rect_t *ext_mrect,
                       const lm_atlas_t *ext_atlas,
                       gi_sdf_stream_t *ext_sdf,
                       uint32_t lm_chunk_count,
                       const struct render_config *render_cfg);

/** Render the scene for @p cam with the given dynamic GI collider proxies. */
void client_scene_render(client_scene_t *cs, const render_camera_t *cam,
                         const gi_collider_t *boxes, uint32_t n_boxes,
                         int screen_w, int screen_h);

/**
 * @brief Stream the probe set to the RESIDENT light-data chunks (rpg-zygg): keep
 *        only the generated probes inside one of the @p n_boxes resident chunk
 *        world boxes and push them to the GI runtime (gi_runtime_set_probes). As
 *        chunks page in/out (SDF streaming), the probe set follows. No-op if the
 *        scene wasn't loaded with a full probe set. Call per frame on the GL thread.
 */
void client_scene_stream_probes(client_scene_t *cs, const float *box_min,
                                const float *box_max, uint32_t n_boxes);

/**
 * @brief Run the shared dual visibility prepass (rpg-sazm) over the scene and use
 *        it to drive GI residency: page the GI runtime's SDF from the on-screen
 *        chunk set (render_world_set_visible) and gate the probe set to the
 *        VISIBLE SDF chunks (instead of RAM residency). @p sdf_box_min/@p _max are
 *        the @p n_sdf_boxes SDF chunk world boxes (same order as gi_sdf_stream).
 *        @p lm_mchunk[@p lm_nm] is the per-mesh lightmap-chunk id (the streamer's
 *        mchunk; NULL/0 => single atlas): it drives the prepass's lightmap channel
 *        so @c gi_pp.visible_lm reports the on-screen lightmap chunk set (feed it
 *        to client_light_stream_set_visible). Retires gi_runtime's internal
 *        prepass. Call per frame before render; GL thread. No-op without a dual prepass.
 */
void client_scene_gi_visibility(client_scene_t *cs, const float view[16],
                                const float proj[16], const float *sdf_box_min,
                                const float *sdf_box_max, int n_sdf_boxes,
                                const int *lm_mchunk, int lm_nm,
                                int screen_w, int screen_h);

/**
 * @brief Rasterise the scene's DYNAMIC objects into the probe GI's sparse dynamic
 *        albedo volume so their colour bleeds into the indirect (rpg-3c6g).
 *
 * Dynamic objects are excluded from the offline bake, so without this the probes
 * only see them as occluders and bounce a neutral grey (a red cloth banner bleeds
 * grey). Clears + refills the volume from the objects tagged @c dynamic in the
 * descriptor, using their real material albedo. No-op if the scene has none.
 * Call once per frame before render; GL thread.
 */
void client_scene_voxelize_dynamic(client_scene_t *cs);

/** Free all owned GL resources. */
void client_scene_destroy(client_scene_t *cs);

/**
 * @brief Build the static-irradiance GI volume from the level's fvma meshes + the
 *        baked SH lightmap (rpg-zygg): folds the baked lightmap ambience into a
 *        coarse 3D world grid the dynamic GI probes gather, so shadowed interior
 *        surfaces read the baked bounce. @p mrect/@p atlas are the per-mesh atlas
 *        rects + dims; @p amin/@p amax the scene AABB. @return true on success
 *        (fills @p vol with a GL_TEXTURE_3D). Needs a current GL context.
 */
bool client_static_volume_build(gi_static_volume_t *vol, const struct scene_desc *desc,
                                const char *base_dir, const lm_atlas_rect_t *mrect,
                                const lm_atlas_t *atlas, const float amin[3],
                                const float amax[3]);

/** Load the baked SH lightmap atlas (9 GL_TEXTURE_2D_ARRAY pages, layer 0) from
 *  @p lm_prefix, plus the per-mesh atlas rectangles + atlas dimensions the caller
 *  needs to remap each mesh's uv1 into the shared atlas (lm_atlas_remap_uv). This
 *  is the one-chunk case of the general chunked lightmap system.
 *  @param mrect      out: [n_meshes] per-mesh atlas rects (w=0 => mesh has none).
 *  @param atlas_out  out: atlas width/height.
 *  @return true if a lightmap was found + loaded. */
bool client_scene_load_lightmap(const gl_loader_t *loader, const char *lm_prefix,
                                uint32_t n_meshes, unsigned int sh_tex[9],
                                lm_atlas_rect_t *mrect, lm_atlas_t *atlas_out);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_CLIENT_SCENE_H */
