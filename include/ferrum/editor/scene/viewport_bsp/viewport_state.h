/**
 * @file viewport_state.h
 * @brief Per-viewport state for multi-viewport tiling.
 *
 * Each BSP leaf viewport has independent camera, gizmo, cursor,
 * box-select, and FBO state. The FBO and its GL resources are
 * managed separately (only valid when GL context exists).
 *
 * Ownership: caller owns viewport_state_t; FBO resources must be
 *            created/destroyed via viewport_state_create_fbo /
 *            viewport_state_destroy_fbo.
 * Nullability: all pointer params must be non-NULL.
 * Error semantics: create_fbo returns false on failure.
 * Side effects: create/destroy allocate/free GPU resources.
 *
 * Public types: viewport_state_t (1 / 2-type rule).
 */
#ifndef FERRUM_EDITOR_SCENE_VIEWPORT_STATE_H
#define FERRUM_EDITOR_SCENE_VIEWPORT_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/editor/viewport/viewport_camera.h"
#include "ferrum/editor/viewport/viewport_gizmo.h"
#include "ferrum/editor/viewport/viewport_nav.h"
#include "ferrum/editor/viewport/viewport_shading.h"
#include "ferrum/editor/scene/scene_panel.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

/**
 * @brief Per-viewport state for a BSP leaf.
 *
 * Each viewport has its own camera, gizmo, 3D cursor,
 * box-select state, and FBO for independent rendering.
 */
typedef struct viewport_state {
    /* Camera + navigation. */
    editor_camera_t camera;         /**< Independent camera state. */
    nav_mode_t      nav_mode;       /**< Navigation mode (orbit/fly/pan). */
    shading_mode_t  shading_mode;   /**< Viewport shading mode. */

    /* Transform gizmo. */
    gizmo_state_t   gizmo;         /**< Gizmo mode, axis, drag state. */
    vec3_t          gizmo_drag_origin; /**< Where gizmo drag started. */
    vec3_t          gizmo_drag_accum;  /**< Accumulated drag delta (translate/scale). */
    quat_t          gizmo_rot_accum;   /**< Accumulated rotation quaternion. */

    /* 3D cursor. */
    vec3_t          cursor_3d;     /**< 3D cursor world position. */

    /* Box select. */
    bool            box_selecting;       /**< True during box select drag. */
    float           box_select_start_x;  /**< Logical X at start. */
    float           box_select_start_y;  /**< Logical Y at start. */

    /* Free-move drag (translate on camera-facing plane). */
    bool            free_dragging;       /**< True during free-move drag. */

    /* FBO (GPU resources — only valid with GL context). */
    uint32_t        fbo;           /**< Off-screen framebuffer. */
    uint32_t        color_tex;     /**< Color attachment texture. */
    uint32_t        depth_rbo;     /**< Depth renderbuffer. */
    int             fbo_width;     /**< Current FBO width. */
    int             fbo_height;    /**< Current FBO height. */

    /* Computed layout rect (set by BSP layout pass). */
    panel_rect_t    rect;          /**< Screen rect for this viewport. */

    /* Lifecycle. */
    bool            active;        /**< True if this slot is in use. */
    bool            fbo_valid;     /**< True if FBO resources are created. */
} viewport_state_t;

/* ---- Lifecycle (viewport_state_init.c) ---- */

/**
 * @brief Initialize viewport state to defaults.
 *
 * Sets up default camera, gizmo, cursor. Does NOT create FBO
 * resources (call viewport_state_create_fbo separately).
 *
 * @param state  Viewport state to initialize (non-NULL).
 */
void viewport_state_init(viewport_state_t *state);

/**
 * @brief Copy camera state from another viewport.
 *
 * Copies camera, gizmo mode, basis, and cursor position.
 * Does NOT copy FBO or box-select state.
 *
 * @param dst  Destination viewport state (non-NULL).
 * @param src  Source viewport state (non-NULL).
 */
void viewport_state_copy_camera(viewport_state_t *dst,
                                const viewport_state_t *src);

/**
 * @brief Reset viewport state to defaults.
 * @param state  Viewport state to reset (non-NULL).
 */
void viewport_state_reset(viewport_state_t *state);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_VIEWPORT_STATE_H */
