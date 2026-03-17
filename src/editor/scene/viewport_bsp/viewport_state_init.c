/**
 * @file viewport_state_init.c
 * @brief Per-viewport state initialization and copy.
 *
 * Non-static functions (3 / 4 limit):
 *   viewport_state_init
 *   viewport_state_copy_camera
 *   viewport_state_reset
 */

#include "ferrum/editor/scene/viewport_bsp/viewport_state.h"
#include "ferrum/editor/edit_entity.h"
#include <string.h>

void viewport_state_init(viewport_state_t *state) {
    memset(state, 0, sizeof(*state));

    editor_camera_init(&state->camera);
    gizmo_state_init(&state->gizmo);

    state->cursor_3d = (vec3_t){0.0f, 0.0f, 0.0f};
    state->cursor_orientation = (quat_t){0, 0, 0, 1};
    state->gizmo_rot_accum = (quat_t){0, 0, 0, 1};
    state->box_selecting = false;
    state->per_object_gizmo = false;
    state->per_object_drag_entity = EDIT_ENTITY_INVALID_ID;
    state->active = true;
    state->fbo_valid = false;
}

void viewport_state_copy_camera(viewport_state_t *dst,
                                const viewport_state_t *src) {
    dst->camera = src->camera;
    dst->nav_mode = src->nav_mode;
    dst->shading_mode = src->shading_mode;
    dst->gizmo.mode = src->gizmo.mode;
    dst->gizmo.basis = src->gizmo.basis;
    dst->cursor_3d = src->cursor_3d;
    dst->cursor_orientation = src->cursor_orientation;
}

void viewport_state_reset(viewport_state_t *state) {
    bool fbo_valid = state->fbo_valid;
    uint32_t fbo = state->fbo;
    uint32_t color_tex = state->color_tex;
    uint32_t depth_rbo = state->depth_rbo;
    int fbo_w = state->fbo_width;
    int fbo_h = state->fbo_height;

    viewport_state_init(state);

    /* Preserve FBO resources (they're managed externally). */
    state->fbo_valid = fbo_valid;
    state->fbo = fbo;
    state->color_tex = color_tex;
    state->depth_rbo = depth_rbo;
    state->fbo_width = fbo_w;
    state->fbo_height = fbo_h;
}
