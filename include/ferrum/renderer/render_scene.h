#ifndef FERRUM_RENDERER_RENDER_SCENE_H
#define FERRUM_RENDERER_RENDER_SCENE_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/renderer/light_store.h"
#include "ferrum/renderer/material.h"
#include "ferrum/renderer/mesh/static_mesh.h"
#include "ferrum/renderer/render_camera.h"

/** @file
 * @brief Scene submission interface: the draw-list the app/editor hands to the
 *        render pipeline.
 *
 * A scene is a flat list of renderables (mesh + material + world transform), a
 * camera, and a borrowed light store. Backing storage for the renderable list
 * is caller-provided (no internal allocation). The pipeline iterates the list,
 * binds each material, and draws each mesh; the camera and lights drive every
 * pass. Ownership: meshes, materials and the light store are borrowed.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** One drawable: a mesh + material + world transform. */
typedef struct render_renderable {
    const static_mesh_t   *mesh;     /**< borrowed mesh. */
    const render_material_t *material;/**< borrowed material. */
    float model[16];                 /**< model->world (column-major). */
    int   sh_layer;                  /**< baked-SH texture-array layer (per-chunk
                                          lightmap page, rpg-yfa4). 0 by default. */
    int   shadow_cascade;            /**< CSM cascade this caster is assigned to,
                                          by size/background (rpg-fsvq). -1 = not
                                          yet classified; set by shadow_csm_update. */
} render_renderable_t;

/** A submitted frame: renderables + camera + lights.
 *
 * Renderables in [0, dynamic_from) are STATIC (never move -> their shadow term
 * is baked once); [dynamic_from, count) are DYNAMIC (re-shadowed per frame).
 * @ref render_scene_mark_dynamic draws the boundary. Default: all static. */
typedef struct render_scene {
    render_renderable_t       *items;    /**< caller-owned backing array. */
    uint32_t                   count;    /**< renderables submitted. */
    uint32_t                   capacity; /**< backing capacity. */
    uint32_t                   dynamic_from; /**< first dynamic renderable index. */
    render_camera_t            camera;    /**< view camera. */
    const render_light_store_t *lights;   /**< borrowed light store (may be NULL). */
    /* Material table for MULTI-MATERIAL meshes: when a renderable's own
     * material is NULL, each submesh resolves its material by material_slot
     * into this borrowed table (so one mesh's walls/glass/signs each shade
     * with their own material). NULL = every item uses its single material. */
    const render_material_t   *materials;
    uint32_t                   material_count;
} render_scene_t;

/**
 * @brief Resolve the material for submesh @p sub of renderable @p r. When the
 *        renderable carries its own material (single-material items: terrain,
 *        dynamic bodies) that wins; otherwise the submesh's material_slot
 *        indexes the scene material table. Returns NULL when unresolvable
 *        (caller skips the submesh).
 */
static inline const render_material_t *render_submesh_material(
    const render_scene_t *scene, const render_renderable_t *r, uint32_t sub)
{
    if (r->material != NULL)
        return r->material;
    if (scene->materials != NULL && r->mesh != NULL &&
        sub < r->mesh->submesh_count) {
        uint16_t slot = r->mesh->submeshes[sub].material_slot;
        if (slot < scene->material_count)
            return &scene->materials[slot];
    }
    return NULL;
}

/**
 * @brief Initialise a scene over caller-provided renderable backing storage.
 */
void render_scene_init(render_scene_t *scene, render_renderable_t *backing,
                       uint32_t capacity);

/**
 * @brief Append a renderable (mesh + material + model). Returns false if full.
 */
bool render_scene_add(render_scene_t *scene, const static_mesh_t *mesh,
                      const render_material_t *material, const float model[16]);

/**
 * @brief Mark the static/dynamic boundary at the current count: everything
 *        added so far is static; everything added afterwards is dynamic.
 */
void render_scene_mark_dynamic(render_scene_t *scene);

/**
 * @brief Remove all renderables (capacity/camera/lights unchanged). Resets the
 *        dynamic boundary (all subsequently-added renderables are static again).
 */
void render_scene_clear(render_scene_t *scene);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_RENDER_SCENE_H */
