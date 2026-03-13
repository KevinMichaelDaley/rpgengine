/**
 * @file clay_backend.h
 * @brief Renderer-backed Clay UI drawing.
 *
 * Consumes the Clay_RenderCommandArray produced by Clay_EndLayout()
 * and issues draw calls via the renderer module (shader_program_t,
 * vao_t, vbo_t). Handles RECTANGLE, TEXT, BORDER, SCISSOR, IMAGE,
 * and CUSTOM command types.
 *
 * Ownership: clay_backend_init() allocates renderer resources;
 *            clay_backend_destroy() frees them.
 * Nullability: all pointer params must be non-NULL.
 * Error semantics: init returns false on failure.
 * Side effects: issues draw calls during render.
 *
 * Public types: clay_backend_t, clay_backend_config_t (2-type rule).
 */
#ifndef FERRUM_EDITOR_UI_CLAY_BACKEND_H
#define FERRUM_EDITOR_UI_CLAY_BACKEND_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/vao.h"
#include "ferrum/renderer/vbo.h"
#include "ferrum/renderer/gl_loader.h"
#include "ferrum/editor/ui/clay_fonts.h"

/* Forward declarations — avoids pulling in full Clay header here */
struct Clay_RenderCommandArray;

/**
 * @brief Configuration for the Clay backend.
 */
typedef struct clay_backend_config {
    int         window_w;   /**< Window width in pixels. */
    int         window_h;   /**< Window height in pixels. */
    gl_loader_t loader;     /**< GL function loader. */
} clay_backend_config_t;

/**
 * @brief Renderer-backed Clay UI drawing context.
 *
 * Uses renderer module abstractions (shader_program_t, vao_t, vbo_t)
 * so all GL calls go through src/renderer/.
 */
typedef struct clay_backend {
    shader_program_t shader;     /**< UI quad/text shader program. */
    vao_t            vao;        /**< Vertex array for UI quads. */
    vbo_t            vbo;        /**< Vertex buffer for UI quads. */
    gl_loader_t      loader;     /**< Cached GL loader for draw calls. */
    clay_font_set_t  fonts;      /**< Font glyph atlas. */
    int              window_w;   /**< Current window width. */
    int              window_h;   /**< Current window height. */
    int32_t          u_projection; /**< Uniform: orthographic projection. */
    int32_t          u_use_texture; /**< Uniform: texture vs solid color. */
    int32_t          u_texture;    /**< Uniform: texture sampler. */
    bool             initialized;  /**< Guard against double init. */

    /** GL functions not covered by renderer module wrappers.
     *  Resolved from gl_loader_t during init. */
    void (*glDrawArrays)(uint32_t mode, int32_t first, int32_t count);
    void (*glEnable)(uint32_t cap);
    void (*glDisable)(uint32_t cap);
    void (*glBlendFunc)(uint32_t sfactor, uint32_t dfactor);
    void (*glScissor)(int32_t x, int32_t y, int32_t w, int32_t h);
    void (*glActiveTexture)(uint32_t texture);
    void (*glBindTexture)(uint32_t target, uint32_t texture);
} clay_backend_t;

/* ---- Lifecycle ---- */

/**
 * @brief Initialize the Clay backend: compile shaders, create buffers.
 *
 * Requires a current OpenGL context and a valid gl_loader_t.
 *
 * @param backend  Backend to initialize (non-NULL).
 * @param config   Configuration with loader and window size.
 * @return true on success, false on failure.
 */
bool clay_backend_init(clay_backend_t *backend,
                       const clay_backend_config_t *config);

/**
 * @brief Destroy all renderer resources.
 * @param backend  Backend to destroy (non-NULL).
 */
void clay_backend_destroy(clay_backend_t *backend);

/**
 * @brief Render Clay UI commands to the current framebuffer.
 *
 * Call after 3D scene rendering so UI draws on top.
 *
 * @param backend  Initialized backend (non-NULL).
 * @param cmds     Render command array from Clay_EndLayout().
 */
void clay_backend_render(clay_backend_t *backend,
                         struct Clay_RenderCommandArray cmds);

/**
 * @brief Update the window dimensions (call on resize).
 * @param backend  Backend (non-NULL).
 * @param w  New width.
 * @param h  New height.
 */
void clay_backend_resize(clay_backend_t *backend, int w, int h);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_UI_CLAY_BACKEND_H */
