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

struct scene_desc; /* ferrum/scene/scene_desc.h */

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
    render_light_store_t lights;
    render_light_t      *light_buf;   /**< lights backing. */
    const gl_loader_t   *loader;
    int                  static_count;/**< number of static (level) renderables. */
} client_scene_t;

/**
 * @brief Load a level's assets from @p base_dir per @p desc and assemble the
 *        render_world. @p base_dir is prepended to relative asset paths.
 * @return false on a fatal error (OOM, render_world init). Missing individual
 *         textures fall back to a debug pattern (non-fatal).
 */
bool client_scene_load(client_scene_t *cs, const gl_loader_t *loader,
                       const struct scene_desc *desc, const char *base_dir,
                       client_image_load_fn image_load, int screen_w, int screen_h);

/** Render the scene for @p cam with the given dynamic GI collider proxies. */
void client_scene_render(client_scene_t *cs, const render_camera_t *cam,
                         const gi_collider_t *boxes, uint32_t n_boxes,
                         int screen_w, int screen_h);

/** Free all owned GL resources. */
void client_scene_destroy(client_scene_t *cs);

/** Load the baked SH lightmap (9 GL_TEXTURE_2D_ARRAY pages) from @p lm_prefix.
 *  Fills @p sh_tex[9] and per-mesh @p sh_layer (0). @return true if a lightmap
 *  was found. Lifted from hall_lit_dynamic.c's load_sh_arrays. */
bool client_scene_load_lightmap(const gl_loader_t *loader, const char *lm_prefix,
                                uint32_t n_meshes, unsigned int sh_tex[9],
                                int *sh_layer);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_CLIENT_SCENE_H */
