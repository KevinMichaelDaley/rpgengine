/**
 * @file scene_main.h
 * @brief Scene editor top-level context and lifecycle.
 *
 * The scene editor is a standalone SDL2/OpenGL application that connects
 * to a game server and provides a graphical editing interface.
 *
 * Ownership: scene_editor_init() creates all resources; scene_editor_shutdown()
 *            destroys them. The caller owns the scene_editor_t.
 * Nullability: all pointer params must be non-NULL.
 * Error semantics: init returns false on failure (partial cleanup performed).
 * Side effects: creates an SDL2 window and OpenGL context.
 *
 * Public types: scene_editor_t, scene_editor_config_t (2-type rule).
 */
#ifndef FERRUM_EDITOR_SCENE_MAIN_H
#define FERRUM_EDITOR_SCENE_MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/editor/scene/scene_connection.h"
#include "ferrum/editor/scene/scene_panel.h"
#include "ferrum/editor/scene/scene_sync.h"
#include "ferrum/editor/scene/scene_ui.h"
#include "ferrum/editor/scene/snap_state.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/editor/scene/scene_viewport_render.h"
#include "ferrum/editor/viewport/viewport_gizmo.h"
#include "ferrum/editor/ui/clay_backend.h"
#include "ferrum/math/vec3.h"
#include "ferrum/memory/arena.h"

/* Forward declarations */
struct SDL_Window;
typedef void *SDL_GLContext;

/* ---- Configuration ---- */

/**
 * @brief Scene editor initialization parameters.
 *
 * Zero-initialized fields use sensible defaults.
 */
typedef struct scene_editor_config {
    int window_w;           /**< Initial window width (0 = 1280). */
    int window_h;           /**< Initial window height (0 = 720). */
    const char *window_title; /**< Window title (NULL = "Scene Editor"). */
    uint32_t arena_size;    /**< Main arena size in bytes (0 = 4MB). */
    const char *server_host; /**< Server host (NULL = "127.0.0.1"). */
    uint16_t server_port;   /**< Server TCP port (0 = 9100). */
    float ui_scale;         /**< UI scale factor (0 = 2.0). */
} scene_editor_config_t;

/* ---- Context ---- */

/**
 * @brief Top-level scene editor context.
 *
 * Aggregates the SDL2 window, GL context, panel layout, snap state,
 * and all editor subsystems.
 */
typedef struct scene_editor {
    struct SDL_Window *window;  /**< SDL2 window handle. */
    SDL_GLContext      gl_ctx;  /**< OpenGL context. */

    panel_layout_t     layout;  /**< Panel layout (four regions + dividers). */
    snap_state_t       snap;    /**< Grid/snap state. */
    clay_backend_t     clay_be; /**< Clay UI renderer backend. */

    arena_t            arena;   /**< Main editor arena allocator. */
    uint8_t           *arena_buf; /**< Backing buffer for the arena. */

    void              *clay_mem;   /**< Clay internal memory. */
    struct Clay_Context *clay_ctx; /**< Clay context handle. */

    /* Server connection and sync. */
    scene_connection_t connection;  /**< TCP/UDP connection to server. */
    scene_sync_t       sync;       /**< Offline queue and sync state. */

    /* Local entity data (mirrored from server). */
    edit_entity_store_t entities;  /**< Local entity store. */
    edit_selection_t    selection; /**< Selected entity set. */

    /* 3D viewport renderer. */
    viewport_render_state_t viewport; /**< FBO, shaders, meshes, camera. */

    /* 3D cursor (world-space crosshair). */
    vec3_t cursor_3d;               /**< 3D cursor world position. */

    /* Transform gizmo state. */
    gizmo_state_t gizmo;            /**< Gizmo mode, axis, drag state. */

    /* Viewport interaction state. */
    bool box_selecting;             /**< True during box select drag. */
    float box_select_start_x;      /**< Screen X at box select start. */
    float box_select_start_y;      /**< Screen Y at box select start. */

    /* Interactive UI state. */
    scene_ui_state_t   ui;         /**< UI actions, scroll, mouse. */

    scene_editor_config_t config; /**< Resolved config. */
    bool               running;   /**< Main loop flag. */
    bool               initialized; /**< Guard against double init. */
    bool               connected;   /**< True if server connection is live. */
    uint32_t           reconnect_ticks; /**< Frame counter for reconnect throttle. */

    /* Divider drag state */
    divider_id_t       dragging_divider; /**< DIVIDER_NONE if not dragging. */
} scene_editor_t;

/* ---- Lifecycle ---- */

/**
 * @brief Initialize the scene editor: create window, GL context, layout.
 *
 * @param ed     Editor context to initialize (non-NULL).
 * @param config Configuration (zero-init uses defaults).
 * @return true on success, false on failure.
 */
bool scene_editor_init(scene_editor_t *ed, const scene_editor_config_t *config);

/**
 * @brief Shut down the scene editor and free all resources.
 * @param ed  Editor context to shut down (non-NULL).
 */
void scene_editor_shutdown(scene_editor_t *ed);

/**
 * @brief Run the main editor loop (blocks until quit).
 *
 * Polls SDL events, dispatches to panels, renders, swaps.
 *
 * @param ed  Editor context (must be initialized).
 */
void scene_editor_run(scene_editor_t *ed);

/**
 * @brief Process one frame: poll events, update, render.
 *
 * Exposed for testing and custom main loops.
 *
 * @param ed  Editor context (must be initialized).
 */
void scene_editor_frame(scene_editor_t *ed);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_MAIN_H */
