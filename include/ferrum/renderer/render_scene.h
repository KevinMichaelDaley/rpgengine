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
} render_renderable_t;

/** A submitted frame: renderables + camera + lights. */
typedef struct render_scene {
    render_renderable_t       *items;    /**< caller-owned backing array. */
    uint32_t                   count;    /**< renderables submitted. */
    uint32_t                   capacity; /**< backing capacity. */
    render_camera_t            camera;    /**< view camera. */
    const render_light_store_t *lights;   /**< borrowed light store (may be NULL). */
} render_scene_t;

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
 * @brief Remove all renderables (capacity/camera/lights unchanged).
 */
void render_scene_clear(render_scene_t *scene);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_RENDER_SCENE_H */
