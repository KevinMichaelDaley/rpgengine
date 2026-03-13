/**
 * @file scene_viewport_render.h
 * @brief 3D viewport rendering: FBO, shaders, mesh registry, camera.
 *
 * Manages an off-screen framebuffer that the 3D scene is rendered into.
 * The resulting color texture is displayed in the Clay UI viewport panel
 * as an image element. Uses the existing renderer infrastructure:
 *   - render_pipeline_t for pass management
 *   - mesh_registry_t for mesh storage (all entity types)
 *   - shader_program_t + shader_uniform_cache_t for uniforms
 *   - vao_t / vbo_t for grid geometry
 *   - editor_camera_t for the orbit camera
 *
 * Ownership: viewport_render_init() allocates GPU resources;
 *            viewport_render_destroy() frees them.
 * Nullability: all pointer params must be non-NULL.
 * Error semantics: init returns false on failure.
 * Side effects: creates FBO, shaders, meshes, render pipeline.
 *
 * Public types: viewport_render_state_t, viewport_render_config_t (2-type rule).
 */
#ifndef FERRUM_EDITOR_SCENE_VIEWPORT_RENDER_H
#define FERRUM_EDITOR_SCENE_VIEWPORT_RENDER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/editor/viewport/viewport_camera.h"
#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/mesh/mesh_registry.h"
#include "ferrum/renderer/render_pipeline.h"
#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/shader_uniforms.h"
#include "ferrum/renderer/vao.h"
#include "ferrum/renderer/vbo.h"

/* Forward declarations. */
struct scene_editor;
struct edit_entity_store;
struct edit_selection;

/* ---- Configuration ---- */

/**
 * @brief Configuration for viewport renderer initialization.
 */
typedef struct viewport_render_config {
    int         initial_width;  /**< Initial FBO width (pixels). */
    int         initial_height; /**< Initial FBO height (pixels). */
    gl_loader_t loader;         /**< GL function loader. */
} viewport_render_config_t;

/* ---- State ---- */

/**
 * @brief Viewport 3D rendering state.
 *
 * Contains the off-screen FBO, render pipeline, mesh registry for
 * entity visualization, grid mesh, and the orbit camera.
 */
typedef struct viewport_render_state {
    /* Framebuffer objects. */
    uint32_t fbo;         /**< Off-screen framebuffer. */
    uint32_t color_tex;   /**< Color attachment (GL_TEXTURE_2D). */
    uint32_t depth_rbo;   /**< Depth renderbuffer. */
    int      fbo_width;   /**< Current FBO width. */
    int      fbo_height;  /**< Current FBO height. */

    /* Render pipeline (forward + debug passes). */
    render_pipeline_t pipeline; /**< 9-pass render pipeline. */

    /* 3D shader program (Blinn-Phong). */
    shader_program_t       shader;   /**< Entity shading. */
    shader_uniform_cache_t uniforms; /**< Cached uniform locations. */

    /* Grid shader and geometry (unlit lines). */
    shader_program_t       grid_shader;  /**< Grid line shader. */
    shader_uniform_cache_t grid_uniforms;/**< Grid uniform cache. */
    vao_t grid_vao;       /**< Grid line VAO. */
    vbo_t grid_vbo;       /**< Grid line VBO. */
    int   grid_vertex_count; /**< Number of grid line vertices. */

    /* Mesh registry for all entity shapes. */
    mesh_registry_t meshes;  /**< Central mesh store. */

    /* Primitive mesh handles (registered in meshes). */
    mesh_handle_t mesh_box;     /**< Unit box handle. */
    mesh_handle_t mesh_sphere;  /**< Unit sphere handle. */
    mesh_handle_t mesh_capsule; /**< Unit capsule handle. */
    mesh_handle_t mesh_plane;   /**< Halfspace plane handle. */

    /* Entity mesh cache: maps entity_id → mesh_handle in the registry.
     * Used by MESH type entities to look up their loaded FVMA geometry. */
    mesh_handle_t *entity_mesh_cache;      /**< entity_id → mesh_handle. */
    uint32_t       entity_mesh_cache_cap;  /**< Cache capacity. */

    /* Editor camera. */
    editor_camera_t camera;  /**< Orbit camera state. */

    /* GL loader and function pointers for FBO operations. */
    gl_loader_t loader;   /**< Cached GL loader. */
    bool initialized;     /**< Guard against double init. */

    /* FBO GL functions resolved from loader. */
    void     (*glGenFramebuffers)(int32_t n, uint32_t *ids);
    void     (*glDeleteFramebuffers)(int32_t n, const uint32_t *ids);
    void     (*glBindFramebuffer)(uint32_t target, uint32_t fb);
    void     (*glFramebufferTexture2D)(uint32_t target, uint32_t attachment,
                                       uint32_t textarget, uint32_t texture,
                                       int32_t level);
    void     (*glFramebufferRenderbuffer)(uint32_t target, uint32_t attachment,
                                          uint32_t rbtarget, uint32_t rb);
    uint32_t (*glCheckFramebufferStatus)(uint32_t target);
    void     (*glGenRenderbuffers)(int32_t n, uint32_t *ids);
    void     (*glDeleteRenderbuffers)(int32_t n, const uint32_t *ids);
    void     (*glBindRenderbuffer)(uint32_t target, uint32_t rb);
    void     (*glRenderbufferStorage)(uint32_t target, uint32_t fmt,
                                      int32_t w, int32_t h);
    void     (*glGenTextures)(int32_t n, uint32_t *ids);
    void     (*glDeleteTextures)(int32_t n, const uint32_t *ids);
    void     (*glBindTexture)(uint32_t target, uint32_t tex);
    void     (*glTexImage2D)(uint32_t target, int32_t level, int32_t ifmt,
                              int32_t w, int32_t h, int32_t border,
                              uint32_t fmt, uint32_t type, const void *data);
    void     (*glTexParameteri)(uint32_t target, uint32_t pname, int32_t param);
    void     (*glViewport)(int32_t x, int32_t y, int32_t w, int32_t h);
    void     (*glClearColor)(float r, float g, float b, float a);
    void     (*glClear)(uint32_t mask);
    void     (*glEnable)(uint32_t cap);
    void     (*glDisable)(uint32_t cap);
    void     (*glDrawArrays)(uint32_t mode, int32_t first, int32_t count);
} viewport_render_state_t;

/* ---- Lifecycle ---- */

/**
 * @brief Initialize the viewport renderer: create FBO, shaders, meshes.
 *
 * @param state   Render state to initialize (non-NULL).
 * @param config  Configuration (non-NULL).
 * @return true on success, false on failure.
 */
bool viewport_render_init(viewport_render_state_t *state,
                           const viewport_render_config_t *config);

/**
 * @brief Destroy the viewport renderer and free all GPU resources.
 * @param state  Render state to destroy (non-NULL).
 */
void viewport_render_destroy(viewport_render_state_t *state);

/* ---- Rendering ---- */

/**
 * @brief Resize the FBO to match the viewport panel dimensions.
 *
 * Recreates the color texture and depth renderbuffer.
 *
 * @param state  Render state (non-NULL, must be initialized).
 * @param width  New width in pixels (>0).
 * @param height New height in pixels (>0).
 */
void viewport_render_resize(viewport_render_state_t *state,
                             int width, int height);

/**
 * @brief Get the color texture handle for display in Clay UI.
 *
 * The returned handle can be passed to Clay's image element as
 * imageData (cast to void*).
 *
 * @param state  Render state (non-NULL).
 * @return GL texture handle, or 0 if not initialized.
 */
uint32_t viewport_render_get_texture(const viewport_render_state_t *state);

/* ---- Scene drawing (scene_viewport_draw.c) ---- */

/**
 * @brief Render the full 3D scene into the viewport FBO.
 *
 * Resizes FBO if needed, computes camera matrices, draws grid
 * and all entities, then unbinds the FBO.
 *
 * @param ed  Scene editor context (non-NULL).
 */
void viewport_render_draw_scene(struct scene_editor *ed);

/**
 * @brief Draw the grid into the currently bound FBO.
 *
 * @param state  Render state (non-NULL).
 * @param view   View matrix.
 * @param proj   Projection matrix.
 */
void viewport_render_draw_grid(viewport_render_state_t *state,
                                const struct mat4 *view,
                                const struct mat4 *proj);

/**
 * @brief Draw all active entities into the currently bound FBO.
 *
 * Handles all entity types: BOX, SPHERE, CAPSULE, HALFSPACE,
 * MESH (via mesh registry), and MARKER. Selection-highlighted
 * entities are drawn in orange.
 *
 * @param state      Render state (non-NULL).
 * @param entities   Entity store (non-NULL).
 * @param selection  Selection state (may be NULL).
 * @param view       View matrix.
 * @param proj       Projection matrix.
 * @param eye_pos    Camera eye position for specular.
 */
void viewport_render_draw_entities(viewport_render_state_t *state,
                                    const struct edit_entity_store *entities,
                                    const struct edit_selection *selection,
                                    const struct mat4 *view,
                                    const struct mat4 *proj,
                                    const struct vec3 *eye_pos);

/**
 * @brief Get a lazily-created primitive mesh for a given entity type.
 *
 * Returns built-in geometry for BOX, SPHERE, CAPSULE, HALFSPACE,
 * and MARKER types. Returns NULL for MESH type.
 *
 * @param entity_type  Entity type constant.
 * @param loader       GL function loader (non-NULL).
 * @return Pointer to static mesh, or NULL.
 */
const static_mesh_t *viewport_render_get_primitive_mesh(
    uint32_t entity_type, const gl_loader_t *loader);

/**
 * @brief Destroy lazily-created primitive meshes.
 *
 * Must be called before GL context is destroyed.
 */
void viewport_render_destroy_primitives(void);

/* ---- Overlay drawing (scene_viewport_overlay.c) ---- */

/**
 * @brief Draw the 3D cursor crosshair at the given world position.
 *
 * Renders axis-colored lines (X=red, Y=green, Z=blue). Always visible
 * (no depth test).
 *
 * @param state  Render state (non-NULL).
 * @param pos    Cursor world position (non-NULL).
 * @param view   View matrix.
 * @param proj   Projection matrix.
 */
void viewport_render_draw_cursor(viewport_render_state_t *state,
                                   const struct vec3 *pos,
                                   const struct mat4 *view,
                                   const struct mat4 *proj);

/**
 * @brief Draw orange wireframe outlines around selected entities.
 *
 * Re-draws selected entities slightly scaled up with a flat orange
 * wireframe to highlight the selection.
 *
 * @param state      Render state (non-NULL).
 * @param entities   Entity store (non-NULL).
 * @param selection  Selection set (non-NULL).
 * @param view       View matrix.
 * @param proj       Projection matrix.
 */
void viewport_render_draw_selection_outline(viewport_render_state_t *state,
                                              const struct edit_entity_store *entities,
                                              const struct edit_selection *selection,
                                              const struct mat4 *view,
                                              const struct mat4 *proj);

/* ---- Entity mesh loading (scene_viewport_mesh.c) ---- */

/**
 * @brief Load FVMA mesh data for an entity into the viewport mesh registry.
 *
 * Deserializes the FVMA binary, creates a static mesh in the viewport's
 * mesh registry, and caches the handle for the given entity ID. If the
 * entity already has a loaded mesh, the old one is replaced.
 *
 * @param state      Viewport render state (non-NULL, must be initialized).
 * @param entity_id  Entity ID to associate the mesh with.
 * @param fvma_data  FVMA binary data (non-NULL).
 * @param fvma_size  FVMA data size in bytes (> 0).
 * @return true on success, false on invalid args or load failure.
 */
bool viewport_render_load_entity_mesh(viewport_render_state_t *state,
                                       uint32_t entity_id,
                                       const uint8_t *fvma_data,
                                       size_t fvma_size);

/**
 * @brief Unload an entity's mesh from the viewport mesh registry.
 *
 * Removes the mesh from the registry and clears the cache entry.
 * Safe to call if no mesh is loaded for this entity.
 *
 * @param state      Viewport render state (NULL-safe).
 * @param entity_id  Entity ID to unload.
 */
void viewport_render_unload_entity_mesh(viewport_render_state_t *state,
                                         uint32_t entity_id);

/**
 * @brief Look up a loaded mesh for an entity.
 *
 * @param state      Viewport render state (NULL-safe).
 * @param entity_id  Entity ID to look up.
 * @return Pointer to the static mesh, or NULL if not loaded.
 */
const static_mesh_t *viewport_render_get_entity_mesh(
    const viewport_render_state_t *state, uint32_t entity_id);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_VIEWPORT_RENDER_H */
