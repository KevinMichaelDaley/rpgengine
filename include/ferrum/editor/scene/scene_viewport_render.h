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
#include "ferrum/editor/viewport/viewport_shading.h"
#include "ferrum/editor/viewport/snap/snap_mesh_cache.h"

/* Forward declarations. */
struct scene_editor;
struct edit_entity_store;
struct edit_selection;
struct gizmo_state;

/* ---- Configuration ---- */

/**
 * @brief Configuration for viewport renderer initialization.
 */
typedef struct viewport_render_config {
    int         initial_width;   /**< Initial FBO width (pixels). */
    int         initial_height;  /**< Initial FBO height (pixels). */
    gl_loader_t loader;          /**< GL function loader. */
    uint32_t    entity_cache_cap;/**< Entity mesh cache capacity (0 = default 1M). */
} viewport_render_config_t;

/* ---- State ---- */

/**
 * @brief Viewport 3D rendering state.
 *
 * Contains the off-screen FBO, render pipeline, mesh registry for
 * entity visualization, grid mesh, and the orbit camera.
 */
typedef struct viewport_render_state {
    /* Framebuffer objects (resolve target — single-sample, has color_tex). */
    uint32_t fbo;         /**< Resolve framebuffer (single-sample). */
    uint32_t color_tex;   /**< Color attachment (GL_TEXTURE_2D). */
    uint32_t depth_rbo;   /**< Depth renderbuffer (resolve). */
    int      fbo_width;   /**< Current FBO width. */
    int      fbo_height;  /**< Current FBO height. */

    /* MSAA framebuffer (render target — multisample renderbuffers). */
    uint32_t msaa_fbo;        /**< Multisample framebuffer. */
    uint32_t msaa_color_rbo;  /**< Multisample color renderbuffer. */
    uint32_t msaa_depth_rbo;  /**< Multisample depth+stencil renderbuffer. */
    int      msaa_samples;    /**< Actual sample count (queried from GL). */

    /* Render pipeline (forward + debug passes). */
    render_pipeline_t pipeline; /**< 9-pass render pipeline. */

    /* 3D shader program (Blinn-Phong — "Shaded" mode). */
    shader_program_t       shader;   /**< Entity shading. */
    shader_uniform_cache_t uniforms; /**< Cached uniform locations. */

    /* Matcap shader (half-lambert clay — "Matcap" mode). */
    shader_program_t       matcap_shader;   /**< Matcap shading. */
    shader_uniform_cache_t matcap_uniforms; /**< Matcap uniform cache. */

    /* Flat unlit shader (selection outlines, wireframe mode). */
    shader_program_t       flat_shader;   /**< Flat unlit shading. */
    shader_uniform_cache_t flat_uniforms; /**< Flat uniform cache. */

    /* Grid shader and geometry (unlit lines). */
    shader_program_t       grid_shader;  /**< Grid line shader. */
    shader_uniform_cache_t grid_uniforms;/**< Grid uniform cache. */
    vao_t grid_vao;       /**< Grid line VAO. */
    vbo_t grid_vbo;       /**< Grid line VBO. */
    int   grid_vertex_count; /**< Number of grid line vertices. */

    /* Overlay VBO/VAO for gizmo, cursor, and box select (dynamic data). */
    vao_t overlay_vao;    /**< Overlay line VAO (separate from grid). */
    vbo_t overlay_vbo;    /**< Overlay line VBO (separate from grid). */

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

    /* Collision mesh cache: maps entity_id → mesh_handle for collision geo.
     * Parallel to entity_mesh_cache. When loaded, the collision mesh
     * overrides the render mesh for snapping and physics. */
    mesh_handle_t *collision_mesh_cache;     /**< entity_id → collision mesh. */
    uint32_t       collision_mesh_cache_cap; /**< Collision cache capacity. */

    /* CPU-side geometry cache for surface snap raycasting. */
    snap_mesh_cache_t snap_meshes;  /**< entity_id → CPU vertex/index data. */

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
    void     (*glCullFace)(uint32_t mode);
    void     (*glDepthMask)(uint8_t flag);
    void     (*glDrawArrays)(uint32_t mode, int32_t first, int32_t count);
    void     (*glLineWidth)(float width);
    void     (*glPolygonMode)(uint32_t face, uint32_t mode);

    /* Stencil functions for selection outline rendering. */
    void     (*glStencilFunc)(uint32_t func, int32_t ref, uint32_t mask);
    void     (*glStencilOp)(uint32_t sfail, uint32_t dpfail, uint32_t dppass);
    void     (*glStencilMask)(uint32_t mask);
    void     (*glColorMask)(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

    /* MSAA functions. */
    void     (*glRenderbufferStorageMultisample)(uint32_t target, int32_t samples,
                                                 uint32_t fmt, int32_t w,
                                                 int32_t h);
    void     (*glBlitFramebuffer)(int32_t sx0, int32_t sy0, int32_t sx1,
                                   int32_t sy1, int32_t dx0, int32_t dy0,
                                   int32_t dx1, int32_t dy1, uint32_t mask,
                                   uint32_t filter);
    void     (*glGetIntegerv)(uint32_t pname, int32_t *params);
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
                                    uint32_t active_object_id,
                                    const struct mat4 *view,
                                    const struct mat4 *proj,
                                    const struct vec3 *eye_pos,
                                    shading_mode_t shading_mode);

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
    uint32_t entity_type, const viewport_render_state_t *state);

/**
 * @brief Register primitive meshes (box, sphere, capsule, plane) in the
 *        viewport's mesh registry.
 *
 * Must be called after the mesh registry is initialized. Primitives are
 * owned by the registry and destroyed via mesh_registry_destroy().
 *
 * @param state  Render state (non-NULL, must have valid loader and meshes).
 * @return true on success, false if primitive creation fails.
 */
bool viewport_render_init_primitives(viewport_render_state_t *state);

/* ---- Collision mesh (scene_viewport_collision_mesh.c) ---- */

/**
 * @brief Load FVMA collision mesh data for an entity.
 *
 * Stores the resulting mesh handle in the collision mesh cache. Also
 * updates the snap mesh cache to use collision geometry for snapping.
 *
 * @param state      Viewport render state (non-NULL, must be initialized).
 * @param entity_id  Entity ID to associate the collision mesh with.
 * @param fvma_data  FVMA binary data (non-NULL).
 * @param fvma_size  FVMA data size in bytes (> 0).
 * @return true on success, false on invalid args or load failure.
 */
bool viewport_render_load_collision_mesh(viewport_render_state_t *state,
                                          uint32_t entity_id,
                                          const uint8_t *fvma_data,
                                          size_t fvma_size);

/**
 * @brief Unload an entity's collision mesh from the viewport mesh registry.
 *
 * Removes the collision mesh from the registry and clears the cache entry.
 * Also removes the snap cache entry (caller should re-load render mesh snap
 * data if needed).
 *
 * @param state      Viewport render state (NULL-safe).
 * @param entity_id  Entity ID to unload.
 */
void viewport_render_unload_collision_mesh(viewport_render_state_t *state,
                                            uint32_t entity_id);

/**
 * @brief Look up a loaded collision mesh for an entity.
 *
 * @param state      Viewport render state (NULL-safe).
 * @param entity_id  Entity ID to look up.
 * @return Pointer to the static mesh, or NULL if not loaded.
 */
const static_mesh_t *viewport_render_get_collision_mesh(
    const viewport_render_state_t *state, uint32_t entity_id);

/* ---- Collision overlay (scene_viewport_collision_overlay.c) ---- */

/**
 * @brief Draw green wireframe overlay showing collision geometry.
 *
 * For each active, visible entity:
 *   - MESH: draws collision mesh if loaded, else render mesh
 *   - BOX/SPHERE/CAPSULE: draws primitive mesh wireframe
 *   - HALFSPACE/MARKER: skipped
 *
 * Uses flat_shader with glPolygonMode(GL_LINE), depth test disabled,
 * depth write disabled. Green color for visual distinction.
 *
 * @param state      Render state (non-NULL).
 * @param entities   Entity store (non-NULL).
 * @param view       View matrix.
 * @param proj       Projection matrix.
 */
void viewport_render_draw_collision_overlay(viewport_render_state_t *state,
                                             const struct edit_entity_store *entities,
                                             const struct mat4 *view,
                                             const struct mat4 *proj);

/* ---- Gizmo drawing (scene_viewport_gizmo.c) ---- */

/**
 * @brief Draw the transform gizmo at the selection center.
 *
 * Renders translate arrows, rotate rings, or scale handles based
 * on current gizmo mode. Always visible (no depth test). Axis
 * colors: X=red, Y=green, Z=blue. Active axis is brighter.
 *
 * @param state      Render state (non-NULL).
 * @param gizmo      Gizmo state (non-NULL).
 * @param selection  Selection set (non-NULL).
 * @param view       View matrix.
 * @param proj       Projection matrix.
 */
void viewport_render_draw_gizmo(viewport_render_state_t *state,
                                  const struct gizmo_state *gizmo,
                                  const struct edit_selection *selection,
                                  const struct mat4 *view,
                                  const struct mat4 *proj);

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
                                              uint32_t active_object_id,
                                              const struct mat4 *view,
                                              const struct mat4 *proj,
                                              const struct vec3 *eye_pos,
                                              float fov_y,
                                              int fbo_height,
                                              shading_mode_t shading_mode);

/**
 * @brief Draw a box select rectangle overlay in the viewport.
 *
 * Coordinates are normalized [0,1] within the viewport panel:
 * (0,0)=top-left, (1,1)=bottom-right.
 *
 * @param state  Render state (non-NULL).
 * @param x0     Start X (normalized).
 * @param y0     Start Y (normalized).
 * @param x1     End X (normalized).
 * @param y1     End Y (normalized).
 */
void viewport_render_draw_box_select(viewport_render_state_t *state,
                                       float x0, float y0,
                                       float x1, float y1);

/* ---- Shader init (scene_viewport_shaders.c) ---- */

/**
 * @brief Compile additional viewport shaders (matcap, flat).
 *
 * Must be called after the main entity shader is compiled. Creates
 * the matcap (half-lambert clay) and flat (unlit) shader programs.
 *
 * @param state   Render state (non-NULL, must have valid loader).
 * @return true on success, false if shader compilation fails.
 */
bool viewport_render_init_extra_shaders(viewport_render_state_t *state);

/**
 * @brief Destroy additional viewport shaders (matcap, flat).
 * @param state  Render state (non-NULL).
 */
void viewport_render_destroy_extra_shaders(viewport_render_state_t *state);

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
