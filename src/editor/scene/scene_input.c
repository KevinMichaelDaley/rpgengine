/**
 * @file scene_input.c
 * @brief SDL2 event dispatch for the scene editor.
 *
 * Handles window resize, quit, panel focus, divider drag, mouse state
 * tracking for Clay interaction, and keyboard shortcuts for entity
 * operations and panel toggles.
 */

#include "ferrum/editor/scene/scene_input.h"
#include "ferrum/editor/scene/scene_main.h"
#include "ferrum/editor/scene/scene_cmd.h"
#include "ferrum/editor/scene/scene_connection.h"
#include "ferrum/editor/scene/scene_sync.h"
#include "ferrum/editor/scene/viewport_bsp/viewport_bsp.h"
#include "ferrum/editor/scene/viewport_bsp/viewport_state.h"
#include "ferrum/editor/viewport/viewport_camera.h"
#include "ferrum/editor/viewport/viewport_nav.h"
#include "ferrum/editor/viewport/viewport_gizmo.h"
#include "ferrum/editor/viewport/transform_basis.h"
#include "ferrum/math/quat.h"
#include "ferrum/editor/viewport/selection_raycast.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/editor/ctrl_cmd_defs.h"
#include "ferrum/editor/scene/snap_state.h"
#include "ferrum/editor/ui/clay_theme.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Camera orbit/pan sensitivity. */
#define CAMERA_ORBIT_SPEED 0.005f
#define CAMERA_PAN_SPEED   0.02f
#define CAMERA_ZOOM_SPEED  1.0f
#define CAMERA_FLY_SPEED   0.5f

/** Gizmo drag sensitivity: world units per pixel of mouse motion. */
#define GIZMO_DRAG_SPEED 0.01f

/** Rotation drag sensitivity: degrees per pixel of mouse motion. */
#define GIZMO_ROTATE_SPEED 0.5f

/* ---- Held-key tracking ---- */

/** @brief Check if a key is currently held (has had KEYDOWN without KEYUP). */
static bool key_is_held(const scene_ui_state_t *ui, uint32_t keycode) {
    for (int i = 0; i < ui->held_key_count; ++i) {
        if (ui->held_keys[i] == keycode) return true;
    }
    return false;
}

/** @brief Mark a key as held. Returns false if already held. */
static bool key_mark_held(scene_ui_state_t *ui, uint32_t keycode) {
    if (key_is_held(ui, keycode)) return false;
    if (ui->held_key_count < UI_HELD_KEYS_MAX) {
        ui->held_keys[ui->held_key_count++] = keycode;
    }
    return true;
}

/** @brief Mark a key as released (remove from held set). */
static void key_mark_released(scene_ui_state_t *ui, uint32_t keycode) {
    for (int i = 0; i < ui->held_key_count; ++i) {
        if (ui->held_keys[i] == keycode) {
            ui->held_keys[i] = ui->held_keys[--ui->held_key_count];
            return;
        }
    }
}

/* ---- Internal helpers ---- */

/** Target gizmo screen fraction: the gizmo axis tip will be this
 *  fraction of the viewport height away from center on screen. */
#define GIZMO_SCREEN_FRACTION 0.12f

/**
 * @brief Compute gizmo visual scale so the gizmo occupies a fixed
 *        fraction of screen height regardless of zoom and FOV.
 */
static float viewport_gizmo_screen_scale(const vec3_t *gizmo_pos,
                                           const vec3_t *eye_pos,
                                           float fov_y) {
    float dx = gizmo_pos->x - eye_pos->x;
    float dy = gizmo_pos->y - eye_pos->y;
    float dz = gizmo_pos->z - eye_pos->z;
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    /* visible_height = 2 * dist * tan(fov/2)
     * gizmo should be GIZMO_SCREEN_FRACTION of that. */
    float scale = dist * tanf(fov_y * 0.5f) * GIZMO_SCREEN_FRACTION * 2.0f;
    if (scale < 0.3f) scale = 0.3f;
    return scale;
}

/** Degrees to radians. */
static const float INPUT_DEG_TO_RAD = 3.14159265358979323846f / 180.0f;

/**
 * @brief Build a rotation matrix from a quaternion.
 *
 * Used for cursor-pivot orbit position computation.
 */
static mat4_t build_rotation_from_quat(quat_t dq) {
    mat4_t m;
    quat_to_mat4(dq, &m);
    return m;
}

/**
 * @brief Apply a rotation quaternion to all selected entities (optimistic).
 *
 * Composes the incremental quaternion with each entity's orientation.
 * In cursor basis, also orbits entity positions around the 3D cursor pivot.
 *
 * @param ed  Scene editor (non-NULL).
 * @param dq  Incremental rotation quaternion.
 */
static void apply_gizmo_rotate(scene_editor_t *ed, quat_t dq) {
    viewport_state_t *fvp = scene_focused_vp(ed);
    bool pivot_rotate = (fvp->gizmo.basis == TRANSFORM_BASIS_CURSOR);
    mat4_t rot_mat;
    vec3_t pivot;
    if (pivot_rotate) {
        rot_mat = build_rotation_from_quat(dq);
        pivot = fvp->cursor_3d;
    }

    for (uint32_t i = 0; i < ed->entities.capacity; ++i) {
        if (!edit_selection_contains(&ed->selection, i)) continue;
        edit_entity_t *ent = edit_entity_store_get_mut(&ed->entities, i);
        if (!ent) continue;

        /* Compose quaternion rotation. */
        ent->orientation = quat_normalize_safe(
            quat_mul(dq, ent->orientation), 1e-8f);

        /* Sync euler cache for display.  Canonicalize w >= 0 to avoid
         * branch jumps in the euler decomposition (q and -q are the
         * same rotation but decompose to different euler angles). */
        {
            quat_t cq = ent->orientation;
            if (cq.w < 0.0f) { cq.x = -cq.x; cq.y = -cq.y; cq.z = -cq.z; cq.w = -cq.w; }
            quat_to_euler_yxz(cq, &ent->rot[0], &ent->rot[1], &ent->rot[2]);
        }
        {
            float r2d = 180.0f / 3.14159265358979323846f;
            ent->rot[0] *= r2d;
            ent->rot[1] *= r2d;
            ent->rot[2] *= r2d;
        }

        /* In cursor basis, also orbit position around the cursor. */
        if (pivot_rotate) {
            float ox = ent->pos[0] - pivot.x;
            float oy = ent->pos[1] - pivot.y;
            float oz = ent->pos[2] - pivot.z;
            float nx = rot_mat.m[0] * ox + rot_mat.m[4] * oy
                      + rot_mat.m[8]  * oz;
            float ny = rot_mat.m[1] * ox + rot_mat.m[5] * oy
                      + rot_mat.m[9]  * oz;
            float nz = rot_mat.m[2] * ox + rot_mat.m[6] * oy
                      + rot_mat.m[10] * oz;
            ent->pos[0] = pivot.x + nx;
            ent->pos[1] = pivot.y + ny;
            ent->pos[2] = pivot.z + nz;
        }
    }
}

/**
 * @brief Snap all three euler axes on every selected entity.
 *
 * Called at rotation drag-end and when rotation snap is toggled ON,
 * NOT per-frame during drag (which causes euler-quat round-trip issues).
 */
static void snap_selected_euler_axes(scene_editor_t *ed) {
    if (!ed->snap.enabled[SNAP_ROTATION]) return;
    static const float R2D = 180.0f / 3.14159265358979323846f;
    static const float D2R = 3.14159265358979323846f / 180.0f;
    for (uint32_t i = 0; i < ed->entities.capacity; ++i) {
        if (!edit_selection_contains(&ed->selection, i)) continue;
        edit_entity_t *ent = edit_entity_store_get_mut(&ed->entities, i);
        if (!ent) continue;

        /* Decompose fresh from the authoritative quaternion.
         * Canonicalize to w >= 0 first so atan2 returns consistent
         * euler branches (q and -q are the same rotation). */
        quat_t cq = ent->orientation;
        if (cq.w < 0.0f) { cq.x = -cq.x; cq.y = -cq.y; cq.z = -cq.z; cq.w = -cq.w; }
        float ex, ey, ez;
        quat_to_euler_yxz(cq, &ex, &ey, &ez);
        ent->rot[0] = ex * R2D;
        ent->rot[1] = ey * R2D;
        ent->rot[2] = ez * R2D;

        ent->rot[0] = snap_state_quantize(&ed->snap, SNAP_ROTATION,
                                           ent->rot[0], 0);
        ent->rot[1] = snap_state_quantize(&ed->snap, SNAP_ROTATION,
                                           ent->rot[1], 1);
        ent->rot[2] = snap_state_quantize(&ed->snap, SNAP_ROTATION,
                                           ent->rot[2], 2);
        ent->orientation = quat_from_euler_yxz(
            ent->rot[0] * D2R, ent->rot[1] * D2R, ent->rot[2] * D2R);
    }

}

/**
 * @brief Apply gizmo drag delta to all selected entities (optimistic).
 *
 * For translate: moves entities by delta.
 * For scale: scales entities by (1 + delta component).
 * For rotate: use apply_gizmo_rotate() instead.
 */
static void apply_gizmo_drag(scene_editor_t *ed, vec3_t delta) {
    for (uint32_t i = 0; i < ed->entities.capacity; ++i) {
        if (!edit_selection_contains(&ed->selection, i)) continue;
        edit_entity_t *ent = edit_entity_store_get_mut(&ed->entities, i);
        if (!ent) continue;

        viewport_state_t *fvp = scene_focused_vp(ed);
        switch (fvp->gizmo.mode) {
        case GIZMO_MODE_TRANSLATE:
            ent->pos[0] += delta.x;
            ent->pos[1] += delta.y;
            ent->pos[2] += delta.z;
            break;
        case GIZMO_MODE_SCALE:
            ent->scale[0] *= (1.0f + delta.x);
            ent->scale[1] *= (1.0f + delta.y);
            ent->scale[2] *= (1.0f + delta.z);
            break;
        default:
            break;
        }
    }
}

/**
 * @brief Send per-entity move_id commands after cursor-pivot rotation.
 *
 * Each entity was orbited around the cursor by a different amount
 * (different offsets from the cursor). The rotate command already handled
 * the euler angle change, so we send per-entity move_id commands
 * for the position deltas.
 */
static void send_pivot_move_commands(scene_editor_t *ed,
                                       quat_t total_rot_quat) {
    viewport_state_t *fvp = scene_focused_vp(ed);
    mat4_t rot = build_rotation_from_quat(total_rot_quat);
    vec3_t pivot = fvp->cursor_3d;

    for (uint32_t i = 0; i < ed->entities.capacity; ++i) {
        if (!edit_selection_contains(&ed->selection, i)) continue;
        const edit_entity_t *ent = edit_entity_store_get(&ed->entities, i);
        if (!ent) continue;

        /* The entity's current position is already orbited (from the
         * optimistic local update). Compute what position the server
         * thinks it's at (before orbit) to get the delta. The server
         * will have applied the rotate command (euler only), so its
         * position is: current_pos rotated back by total_rot_delta. */
        float cx = ent->pos[0] - pivot.x;
        float cy = ent->pos[1] - pivot.y;
        float cz = ent->pos[2] - pivot.z;
        /* Inverse rotation: transpose of rot (orthonormal). */
        float ox = rot.m[0] * cx + rot.m[1] * cy + rot.m[2]  * cz;
        float oy = rot.m[4] * cx + rot.m[5] * cy + rot.m[6]  * cz;
        float oz = rot.m[8] * cx + rot.m[9] * cy + rot.m[10] * cz;
        /* Delta = current - original. */
        float dx = cx - ox;
        float dy = cy - oy;
        float dz = cz - oz;

        if (fabsf(dx) < 1e-6f && fabsf(dy) < 1e-6f && fabsf(dz) < 1e-6f) {
            continue; /* Entity is at the pivot — no position change. */
        }

        char buf[256];
        uint32_t cid = scene_connection_next_id(&ed->connection);
        int n = snprintf(buf, sizeof(buf),
            "{\"id\":%u,\"cmd\":\"move_id\",\"args\":"
            "{\"entity_id\":%u,\"delta\":[%.6g,%.6g,%.6g]}}\n",
            (unsigned)cid, (unsigned)i,
            (double)dx, (double)dy, (double)dz);
        if (n > 0 && (size_t)n < sizeof(buf)) {
            scene_connection_send_cmd(&ed->connection, buf);
        }
    }
}

/**
 * @brief Send per-entity absolute transform commands via _id variants.
 *
 * Used when snap is enabled: each selected entity gets its own command
 * with the exact snapped value, avoiding the issue where a single
 * selection-wide command would set all entities to the same value.
 */
static void send_per_entity_abs_(scene_editor_t *ed, gizmo_mode_t mode) {
    char buf[256];
    for (uint32_t i = 0; i < ed->entities.capacity; ++i) {
        if (!edit_selection_contains(&ed->selection, i)) continue;
        const edit_entity_t *ent = edit_entity_store_get(&ed->entities, i);
        if (!ent) continue;

        uint32_t cid = scene_connection_next_id(&ed->connection);
        int n = 0;
        switch (mode) {
        case GIZMO_MODE_TRANSLATE:
            n = snprintf(buf, sizeof(buf),
                "{\"id\":%u,\"cmd\":\"move_id\",\"args\":"
                "{\"entity_id\":%u,\"abs\":[%.8g,%.8g,%.8g]}}\n",
                (unsigned)cid, (unsigned)i,
                (double)ent->pos[0], (double)ent->pos[1],
                (double)ent->pos[2]);
            break;
        case GIZMO_MODE_ROTATE:
            n = snprintf(buf, sizeof(buf),
                "{\"id\":%u,\"cmd\":\"rotate_id\",\"args\":"
                "{\"entity_id\":%u,\"abs\":[%.8g,%.8g,%.8g,%.8g]}}\n",
                (unsigned)cid, (unsigned)i,
                (double)ent->orientation.x, (double)ent->orientation.y,
                (double)ent->orientation.z, (double)ent->orientation.w);
            break;
        case GIZMO_MODE_SCALE:
            n = snprintf(buf, sizeof(buf),
                "{\"id\":%u,\"cmd\":\"scale_id\",\"args\":"
                "{\"entity_id\":%u,\"abs\":[%.8g,%.8g,%.8g]}}\n",
                (unsigned)cid, (unsigned)i,
                (double)ent->scale[0], (double)ent->scale[1],
                (double)ent->scale[2]);
            break;
        default:
            break;
        }
        if (n > 0 && (size_t)n < sizeof(buf)) {
            scene_connection_send_cmd(&ed->connection, buf);
        }
    }
}

/**
 * @brief Send a single server command for the accumulated gizmo drag.
 *
 * The server applies the transform to all currently selected entities.
 * For cursor-basis rotation, also sends per-entity move_id commands
 * to update positions orbited around the cursor pivot.
 */
static void send_gizmo_commands(scene_editor_t *ed, vec3_t total_delta) {
    viewport_state_t *fvp = scene_focused_vp(ed);
    char cmd_buf[256];
    int cmd_len = 0;
    uint32_t cmd_id = scene_connection_next_id(&ed->connection);

    /* Compute the final snapped values — used for both sending and echo. */
    char echo[64];
    echo[0] = '\0';

    switch (fvp->gizmo.mode) {
    case GIZMO_MODE_TRANSLATE: {
        vec3_t snapped = snap_apply_position(&ed->snap,
            fvp->snap_origin_pos, fvp->gizmo_drag_accum);
        if (ed->snap.enabled[SNAP_POSITION]) {
            /* Send per-entity absolute position via move_id. */
            send_per_entity_abs_(ed, GIZMO_MODE_TRANSLATE);
        } else {
            float delta_arr[3] = {snapped.x, snapped.y, snapped.z};
            cmd_len = scene_cmd_format_move(cmd_buf, sizeof(cmd_buf),
                                             cmd_id, delta_arr);
        }
        snprintf(echo, sizeof(echo), "move [%.4g,%.4g,%.4g]",
                 (double)snapped.x, (double)snapped.y, (double)snapped.z);
        break;
    }
    case GIZMO_MODE_ROTATE: {
        if (ed->snap.enabled[SNAP_ROTATION]) {
            /* Send per-entity absolute orientation via rotate_id. */
            send_per_entity_abs_(ed, GIZMO_MODE_ROTATE);
        } else {
            quat_t rq = fvp->gizmo_rot_accum;
            float q[4] = {rq.x, rq.y, rq.z, rq.w};
            cmd_len = scene_cmd_format_rotate(cmd_buf, sizeof(cmd_buf),
                                               cmd_id, q);
        }
        float rx, ry, rz;
        { quat_t cq = fvp->gizmo_rot_accum;
          if (cq.w < 0.0f) { cq.x = -cq.x; cq.y = -cq.y; cq.z = -cq.z; cq.w = -cq.w; }
          quat_to_euler_yxz(cq, &rx, &ry, &rz); }
        float r2d = 180.0f / 3.14159265358979323846f;
        snprintf(echo, sizeof(echo), "rotate [%.4g,%.4g,%.4g]",
                 (double)(rx * r2d), (double)(ry * r2d),
                 (double)(rz * r2d));
        break;
    }
    case GIZMO_MODE_SCALE: {
        vec3_t snapped = snap_apply_scale(&ed->snap,
            fvp->snap_origin_scale, fvp->gizmo_scale_accum);
        if (ed->snap.enabled[SNAP_SCALE]) {
            /* Send per-entity absolute scale via scale_id. */
            send_per_entity_abs_(ed, GIZMO_MODE_SCALE);
        } else {
            float factor[3] = {snapped.x, snapped.y, snapped.z};
            cmd_len = scene_cmd_format_scale(cmd_buf, sizeof(cmd_buf),
                                              cmd_id, factor);
        }
        snprintf(echo, sizeof(echo), "scale [%.4g,%.4g,%.4g]",
                 (double)snapped.x, (double)snapped.y, (double)snapped.z);
        break;
    }
    default:
        break;
    }

    if (cmd_len > 0) {
        scene_connection_send_cmd(&ed->connection, cmd_buf);
        scene_sync_mark_sent(&ed->sync);
        if (echo[0] != '\0') {
            scene_ui_tui_log_pending(&ed->ui, echo, cmd_id);
        }
    }

    /* For cursor-basis rotation, send per-entity position updates. */
    if (fvp->gizmo.mode == GIZMO_MODE_ROTATE &&
        fvp->gizmo.basis == TRANSFORM_BASIS_CURSOR) {
        send_pivot_move_commands(ed, fvp->gizmo_rot_accum);
    }
}

/**
 * @brief Handle mouse button down: start divider drag, change focus,
 *        and update Clay mouse state.
 */
/**
 * @brief Check if a click at logical coords (lx, ly) hits a scrollbar track.
 *
 * Scrollbar tracks are 8px wide on the right edge of each panel's content
 * area. Returns the scrollbar ID (1=outliner, 2=inspector, 3=tui) or 0.
 */
static int scrollbar_hit_test(const scene_editor_t *ed, int lx, int ly) {
    /* Only panels with scrollable content. */
    static const panel_id_t panels[] = {PANEL_OUTLINER, PANEL_INSPECTOR, PANEL_TUI};
    static const int scroll_ids[] = {1, 2, 3};

    /* Check which panel needs a scrollbar and if click is in the track. */
    for (int p = 0; p < 3; p++) {
        if (!panel_layout_is_visible(&ed->layout, panels[p])) continue;

        /* Check if this panel actually has a scrollbar. */
        bool has_scrollbar = false;
        if (panels[p] == PANEL_OUTLINER) {
            has_scrollbar = ed->ui.outliner_total > ed->ui.outliner_visible_lines;
        } else if (panels[p] == PANEL_INSPECTOR) {
            has_scrollbar = ed->ui.inspector_total > ed->ui.inspector_visible_lines;
        } else if (panels[p] == PANEL_TUI) {
            has_scrollbar = ed->ui.tui_log_count > ed->ui.tui_log_visible;
        }
        if (!has_scrollbar) continue;

        panel_rect_t r = panel_layout_get_rect(&ed->layout, panels[p]);
        /* Scrollbar track: rightmost 8px of panel. */
        int track_left = r.x + r.w - 8 - THEME_PADDING_SMALL;
        int track_right = r.x + r.w - THEME_PADDING_SMALL;
        int track_top = r.y + THEME_ROW_HEIGHT; /* below title bar */
        int track_bottom = r.y + r.h;

        if (lx >= track_left && lx <= track_right &&
            ly >= track_top && ly <= track_bottom) {
            return scroll_ids[p];
        }
    }
    return 0;
}

static bool handle_mouse_down(scene_editor_t *ed, const SDL_MouseButtonEvent *ev) {
    if (ev->button == SDL_BUTTON_LEFT) {
        ed->ui.mouse_down = true;
        ed->ui.mouse_clicked = true;

        /* Convert physical mouse coords to logical (layout) coords. */
        float sc = ed->clay_be.ui_scale;
        if (sc < 1.0f) sc = 1.0f;
        int lx = (int)((float)ev->x / sc);
        int ly = (int)((float)ev->y / sc);

        /* Check for divider drag start */
        divider_id_t div = panel_layout_divider_hit_test(&ed->layout,
                                                          lx, ly);
        if (div != DIVIDER_NONE) {
            ed->dragging_divider = div;
            return true;
        }

        /* Check for BSP divider drag start (within the viewport panel). */
        {
            panel_rect_t vp_panel = panel_layout_get_rect(&ed->layout,
                                                           PANEL_VIEWPORT);
            uint8_t bsp_node;
            if (viewport_bsp_hit_test_divider(&ed->vp_bsp, &vp_panel,
                                               lx, ly, &bsp_node)) {
                ed->dragging_bsp_node = (int8_t)bsp_node;
                return true;
            }
        }

        /* Check for scrollbar drag start. */
        int sb = scrollbar_hit_test(ed, lx, ly);
        if (sb > 0) {
            ed->ui.scrollbar_dragging = sb;
            ed->ui.scrollbar_drag_y = (float)ly;
            if (sb == 1) ed->ui.scrollbar_drag_scroll = ed->ui.outliner_scroll;
            else if (sb == 2) ed->ui.scrollbar_drag_scroll = ed->ui.inspector_scroll;
            else if (sb == 3) ed->ui.scrollbar_drag_scroll = ed->ui.tui_log_scroll;
            return true;
        }

        /* Click-to-focus */
        panel_id_t hit = panel_layout_hit_test(&ed->layout, lx, ly);
        panel_layout_set_focus(&ed->layout, hit);

        /* Viewport left-click: gizmo interaction or entity selection.
         * Skip if click is in the toolbar overlay area (title + mode bar)
         * at the top of the viewport — those clicks belong to Clay UI. */
        if (hit == PANEL_VIEWPORT) {
            /* Compute fresh BSP rects and determine which viewport
             * was clicked (rects may not be up-to-date yet this frame). */
            panel_rect_t bsp_rects[VIEWPORT_MAX_COUNT];
            {
                panel_rect_t vp_panel = panel_layout_get_rect(&ed->layout,
                                                               PANEL_VIEWPORT);
                memset(bsp_rects, 0, sizeof(bsp_rects));
                viewport_bsp_compute_rects(&ed->vp_bsp, &vp_panel, bsp_rects);
                /* Store computed rects into viewport states. */
                for (int vi = 0; vi < VIEWPORT_MAX_COUNT; vi++) {
                    ed->viewports[vi].rect = bsp_rects[vi];
                }
                uint8_t hit_vp;
                if (viewport_bsp_hit_test_viewport(&ed->vp_bsp, bsp_rects,
                                                    lx, ly, &hit_vp)) {
                    if (hit_vp != ed->vp_bsp.focused_viewport) {
                        ed->prev_focused_vp = ed->vp_bsp.focused_viewport;
                    }
                    ed->vp_bsp.focused_viewport = hit_vp;
                }
            }

            viewport_state_t *fvp = scene_focused_vp(ed);
            panel_rect_t vp_rect = fvp->rect;
            int toolbar_h = THEME_ROW_HEIGHT * 2 + THEME_PADDING;
            if (ly < vp_rect.y + toolbar_h) {
                /* Click is in the toolbar area.  Handle mode buttons
                 * directly so they behave identically to keybindings
                 * (no selection clear, proper toggle to NONE). */
                int row2_top = vp_rect.y + THEME_ROW_HEIGHT;
                if (ly >= row2_top) {
                    /* Second row: mode button + separator + basis button.
                     * Approximate hit zones for the two cycling buttons. */
                    int rel_x = lx - vp_rect.x;
                    int btn_start = THEME_PADDING_SMALL;
                    int mode_w = 70;
                    int gap = THEME_PADDING_SMALL;
                    int sep_w = 1 + 2 * gap; /* separator + gaps */
                    int basis_start = btn_start + mode_w + sep_w;
                    int basis_w = 80;
                    if (rel_x >= btn_start &&
                        rel_x < btn_start + mode_w) {
                        /* Mode cycle: same as '.' hotkey. */
                        uint8_t next = (ed->ui.transform_mode + 1)
                                       % UI_MODE_COUNT;
                        ed->ui.transform_mode = next;
                        switch (next) {
                        case UI_MODE_TRANSLATE:
                            gizmo_state_set_mode(&fvp->gizmo,
                                GIZMO_MODE_TRANSLATE); break;
                        case UI_MODE_ROTATE:
                            gizmo_state_set_mode(&fvp->gizmo,
                                GIZMO_MODE_ROTATE); break;
                        case UI_MODE_SCALE:
                            gizmo_state_set_mode(&fvp->gizmo,
                                GIZMO_MODE_SCALE); break;
                        default:
                            gizmo_state_set_mode(&fvp->gizmo,
                                GIZMO_MODE_NONE); break;
                        }
                    } else if (rel_x >= basis_start &&
                               rel_x < basis_start + basis_w) {
                        fvp->gizmo.basis = transform_basis_next(
                            fvp->gizmo.basis);
                        char msg[64];
                        snprintf(msg, sizeof(msg), "Basis: %s",
                                 transform_basis_name(fvp->gizmo.basis));
                        scene_ui_tui_log(&ed->ui, msg);
                    }
                }
                return false;
            }
            float nx = (float)(lx - vp_rect.x) / (float)vp_rect.w;
            float ny = (float)(ly - vp_rect.y) / (float)vp_rect.h;
            /* Screen coords for 2D gizmo test (0,0 = top-left). */
            float screen_nx = nx;
            float screen_ny = ny;
            /* FBO is displayed Y-flipped by Clay's ortho projection,
             * so screen top = FBO bottom.  Flip ny so the ray matches
             * the visual scene rather than the raw FBO layout. */
            ny = 1.0f - ny;

            vec2_t screen_pos = {nx, ny};
            vec2_t vp_size = {(float)vp_rect.w, (float)vp_rect.h};
            editor_ray_t ray;
            if (editor_camera_screen_to_ray(&fvp->camera,
                                              screen_pos, vp_size, &ray) == 0) {
                /* Test gizmo hit first (if we have a selection). */
                bool gizmo_hit = false;
                if (edit_selection_count(&ed->selection) > 0) {
                    vec3_t eye = editor_camera_eye_position(
                        &fvp->camera);
                    float gscale = viewport_gizmo_screen_scale(
                        &fvp->gizmo.position, &eye,
                        fvp->camera.fov);
                    /* Build VP matrix for screen-space ring test. */
                    float aspect = (vp_rect.w > 0 && vp_rect.h > 0)
                        ? (float)vp_rect.w / (float)vp_rect.h : 1.0f;
                    mat4_t view_m, proj_m;
                    editor_camera_view_matrix(&fvp->camera, &view_m);
                    editor_camera_projection_matrix(&fvp->camera,
                                                      aspect, &proj_m);
                    mat4_t vp_m = mat4_mul(proj_m, view_m);
                    gizmo_axis_t axis = gizmo_hit_test(
                        &fvp->gizmo, &ray, gscale,
                        &vp_m, screen_nx, screen_ny);
                    if (axis != GIZMO_AXIS_NONE) {
                        fvp->gizmo.active_axis = axis;
                        fvp->gizmo.dragging = true;
                        fvp->gizmo_drag_origin = fvp->gizmo.position;
                        fvp->gizmo_drag_accum = (vec3_t){0, 0, 0};
                        fvp->gizmo_scale_accum = (vec3_t){1, 1, 1};
                        fvp->gizmo_rot_accum = (quat_t){0, 0, 0, 1};
                        fvp->snap_applied_delta = (vec3_t){0, 0, 0};
                        fvp->snap_applied_scale = (vec3_t){1, 1, 1};
                        fvp->snap_rot_accum_deg = 0.0f;
                        fvp->snap_rot_applied_deg = 0.0f;
                        fvp->snap_rot_origin[0] = 0.0f;
                        fvp->snap_rot_origin[1] = 0.0f;
                        fvp->snap_rot_origin[2] = 0.0f;
                        if (ed->active_object_id != EDIT_ENTITY_INVALID_ID) {
                            const edit_entity_t *re = edit_entity_store_get(
                                &ed->entities, ed->active_object_id);
                            if (re) {
                                fvp->snap_rot_origin[0] = re->rot[0];
                                fvp->snap_rot_origin[1] = re->rot[1];
                                fvp->snap_rot_origin[2] = re->rot[2];
                            }
                        }
                        /* Capture active entity's pos/scale for snap reference. */
                        fvp->snap_origin_pos = fvp->gizmo.position;
                        fvp->snap_origin_scale = (vec3_t){1, 1, 1};
                        if (ed->active_object_id != EDIT_ENTITY_INVALID_ID) {
                            const edit_entity_t *ae = edit_entity_store_get(
                                &ed->entities, ed->active_object_id);
                            if (ae) {
                                fvp->snap_origin_pos = (vec3_t){
                                    ae->pos[0], ae->pos[1], ae->pos[2]};
                                fvp->snap_origin_scale = (vec3_t){
                                    ae->scale[0], ae->scale[1], ae->scale[2]};
                            }
                        }
                        gizmo_hit = true;
                    }
                }

                /* If no gizmo hit, do entity picking. */
                if (!gizmo_hit) {
                    uint32_t cap = ed->entities.capacity;
                    uint32_t count = 0;
                    pick_candidate_t candidates[256];
                    for (uint32_t i = 0; i < cap && count < 256; ++i) {
                        const edit_entity_t *ent =
                            edit_entity_store_get(&ed->entities, i);
                        if (!ent || ent->pending_delete) continue;
                        float hw = ent->scale[0] * 0.5f;
                        float hh = ent->scale[1] * 0.5f;
                        float hd = ent->scale[2] * 0.5f;
                        candidates[count].entity_id = i;
                        candidates[count].aabb_min = (vec3_t){
                            ent->pos[0] - hw, ent->pos[1] - hh,
                            ent->pos[2] - hd};
                        candidates[count].aabb_max = (vec3_t){
                            ent->pos[0] + hw, ent->pos[1] + hh,
                            ent->pos[2] + hd};
                        count++;
                    }

                    uint32_t picked_id;
                    SDL_Keymod mod = SDL_GetModState();
                    if (pick_nearest_entity(&ray, candidates, count,
                                             &picked_id)) {
                        if (mod & KMOD_SHIFT) {
                            /* Shift-click: toggle selection. */
                            if (edit_selection_contains(&ed->selection,
                                                        picked_id)) {
                                ed->ui.action = UI_ACTION_DESELECT_ENTITY;
                            } else {
                                ed->ui.action = UI_ACTION_SELECT_ENTITY;
                            }
                            ed->ui.action_target = picked_id;
                            ed->active_object_id = picked_id;
                        } else if (fvp->gizmo.mode == GIZMO_MODE_TRANSLATE &&
                                   edit_selection_contains(&ed->selection,
                                                            picked_id)) {
                            /* Already-selected entity in translate mode:
                             * start free-move on camera-facing plane. */
                            fvp->gizmo.dragging = true;
                            fvp->free_dragging = true;
                            fvp->gizmo_drag_origin = fvp->gizmo.position;
                            fvp->gizmo_drag_accum = (vec3_t){0, 0, 0};
                            fvp->gizmo_scale_accum = (vec3_t){1, 1, 1};
                            fvp->gizmo_rot_accum = (quat_t){0, 0, 0, 1};
                            fvp->snap_applied_delta = (vec3_t){0, 0, 0};
                            fvp->snap_applied_scale = (vec3_t){1, 1, 1};
                            fvp->snap_origin_pos = fvp->gizmo.position;
                            fvp->snap_origin_scale = (vec3_t){1, 1, 1};
                            if (ed->active_object_id != EDIT_ENTITY_INVALID_ID) {
                                const edit_entity_t *ae = edit_entity_store_get(
                                    &ed->entities, ed->active_object_id);
                                if (ae) {
                                    fvp->snap_origin_pos = (vec3_t){
                                        ae->pos[0], ae->pos[1], ae->pos[2]};
                                }
                            }
                        } else {
                            edit_selection_clear(&ed->selection);
                            ed->ui.action = UI_ACTION_SELECT_ENTITY;
                            ed->ui.action_target = picked_id;
                            ed->active_object_id = picked_id;
                        }
                    } else {
                        /* No entity hit — start box select drag.
                         * Store logical coords (divide by UI scale) so
                         * they match panel_layout_get_rect() coordinates. */
                        fvp->box_selecting = true;
                        float bsc = ed->clay_be.ui_scale;
                        if (bsc < 1.0f) bsc = 1.0f;
                        fvp->box_select_start_x = ed->ui.mouse_x / bsc;
                        fvp->box_select_start_y = ed->ui.mouse_y / bsc;
                        if (!(mod & KMOD_SHIFT)) {
                            edit_selection_clear(&ed->selection);
                        }
                    }
                }
            }
        }
    } else if (ev->button == SDL_BUTTON_MIDDLE) {
        ed->ui.middle_mouse_down = true;
    } else if (ev->button == SDL_BUTTON_RIGHT) {
        ed->ui.right_mouse_down = true;
    }
    return false; /* let Clay also handle the click */
}

/**
 * @brief Complete a box select: project entity centers to screen, select
 *        those inside the drag rectangle.
 *
 * Uses the VP matrix to project world positions to normalized viewport
 * coords [0,1]. Entities whose projected center falls inside the box
 * are added to the selection.
 */
static void finish_box_select_(scene_editor_t *ed) {
    viewport_state_t *fvp = scene_focused_vp(ed);
    panel_rect_t vp_rect = fvp->rect;
    if (vp_rect.w <= 0 || vp_rect.h <= 0) return;

    /* Convert current mouse to logical coords (start was already stored
     * in logical coords). Panel rects are in logical pixels. */
    float bsc = ed->clay_be.ui_scale;
    if (bsc < 1.0f) bsc = 1.0f;
    float cur_lx = ed->ui.mouse_x / bsc;
    float cur_ly = ed->ui.mouse_y / bsc;

    /* Normalized box corners in viewport space [0,1].
     * FBO is displayed Y-flipped, so flip sy so projected entity
     * coords (which use standard NDC→screen mapping) match. */
    float sx0 = (fvp->box_select_start_x - (float)vp_rect.x) / (float)vp_rect.w;
    float sy0 = 1.0f - (fvp->box_select_start_y - (float)vp_rect.y) / (float)vp_rect.h;
    float sx1 = (cur_lx - (float)vp_rect.x) / (float)vp_rect.w;
    float sy1 = 1.0f - (cur_ly - (float)vp_rect.y) / (float)vp_rect.h;

    /* Ensure min/max ordering. */
    float bx0 = sx0 < sx1 ? sx0 : sx1;
    float by0 = sy0 < sy1 ? sy0 : sy1;
    float bx1 = sx0 < sx1 ? sx1 : sx0;
    float by1 = sy0 < sy1 ? sy1 : sy0;

    /* Skip tiny drags (likely just a click). */
    if ((bx1 - bx0) < 0.01f && (by1 - by0) < 0.01f) return;

    /* Build view-projection matrix. */
    float aspect = (float)vp_rect.w / (float)vp_rect.h;
    mat4_t view, proj;
    editor_camera_view_matrix(&fvp->camera, &view);
    editor_camera_projection_matrix(&fvp->camera, aspect, &proj);
    mat4_t vp = mat4_mul(proj, view);

    uint32_t cap = ed->entities.capacity;
    for (uint32_t i = 0; i < cap; i++) {
        const edit_entity_t *ent = edit_entity_store_get(&ed->entities, i);
        if (!ent || ent->pending_delete) continue;

        /* Project entity center to clip space. */
        float px = vp.m[0] * ent->pos[0] + vp.m[4] * ent->pos[1]
                  + vp.m[8]  * ent->pos[2] + vp.m[12];
        float py = vp.m[1] * ent->pos[0] + vp.m[5] * ent->pos[1]
                  + vp.m[9]  * ent->pos[2] + vp.m[13];
        float pw = vp.m[3] * ent->pos[0] + vp.m[7] * ent->pos[1]
                  + vp.m[11] * ent->pos[2] + vp.m[15];

        /* Skip entities behind the camera. */
        if (pw <= 0.0f) continue;

        /* NDC to normalized viewport coords [0,1]. */
        float nx = (px / pw) * 0.5f + 0.5f;
        float ny = 0.5f - (py / pw) * 0.5f; /* Flip Y: NDC up → screen down. */

        if (nx >= bx0 && nx <= bx1 && ny >= by0 && ny <= by1) {
            edit_selection_add(&ed->selection, i);
            /* Make the last selected entity the active object. */
            ed->active_object_id = i;
        }
    }
}

/**
 * @brief Handle mouse button up: stop divider drag, update Clay state.
 */
static bool handle_mouse_up(scene_editor_t *ed, const SDL_MouseButtonEvent *ev) {
    if (ev->button == SDL_BUTTON_LEFT) {
        ed->ui.mouse_down = false;
        viewport_state_t *fvp = scene_focused_vp(ed);

        /* End box select: project entities to screen, select those inside. */
        if (fvp->box_selecting) {
            fvp->box_selecting = false;
            finish_box_select_(ed);
            return true;
        }

        /* End gizmo drag (including free-move): send server commands. */
        if (fvp->gizmo.dragging) {
            /* Snap euler axes BEFORE sending so the server gets the
             * snapped absolute orientation (not the unsnapped delta). */
            snap_selected_euler_axes(ed);
            send_gizmo_commands(ed, fvp->gizmo_drag_accum);
            fvp->gizmo.dragging = false;
            fvp->gizmo.active_axis = GIZMO_AXIS_NONE;
            fvp->free_dragging = false;
            return true;
        }

        /* End BSP divider drag. */
        if (ed->dragging_bsp_node >= 0) {
            ed->dragging_bsp_node = -1;
            return true;
        }

        if (ed->dragging_divider != DIVIDER_NONE) {
            ed->dragging_divider = DIVIDER_NONE;
            return true;
        }
        if (ed->ui.scrollbar_dragging != 0) {
            ed->ui.scrollbar_dragging = 0;
            return true;
        }
    } else if (ev->button == SDL_BUTTON_MIDDLE) {
        ed->ui.middle_mouse_down = false;
    } else if (ev->button == SDL_BUTTON_RIGHT) {
        ed->ui.right_mouse_down = false;
    }
    return false;
}

/**
 * @brief Handle mouse motion: drag divider if active, update position.
 */
static bool handle_mouse_motion(scene_editor_t *ed,
                                 const SDL_MouseMotionEvent *ev) {
    /* Always update mouse position for Clay. */
    ed->ui.mouse_x = (float)ev->x;
    ed->ui.mouse_y = (float)ev->y;

    viewport_state_t *fvp = scene_focused_vp(ed);

    /* BSP divider drag: adjust split ratio of the dragged node. */
    if (ed->dragging_bsp_node >= 0) {
        viewport_bsp_node_t *node =
            &ed->vp_bsp.nodes[ed->dragging_bsp_node];
        /* Determine total size along the split axis. */
        panel_rect_t vp_panel = panel_layout_get_rect(&ed->layout,
                                                       PANEL_VIEWPORT);
        int total_size = (node->split == SPLIT_VERTICAL)
                         ? vp_panel.w : vp_panel.h;
        /* Scale physical pixel delta to logical pixels. */
        float sc = ed->clay_be.ui_scale;
        if (sc < 1.0f) sc = 1.0f;
        int raw_delta = (node->split == SPLIT_HORIZONTAL)
                        ? ev->yrel : ev->xrel;
        int delta = (int)((float)raw_delta / sc);
        viewport_bsp_drag_divider(&ed->vp_bsp,
                                   (uint8_t)ed->dragging_bsp_node,
                                   delta, total_size);
        return true;
    }

    /* Gizmo drag: convert pixel delta to constrained world delta. */
    if (fvp->gizmo.dragging) {
        vec3_t delta = {0, 0, 0};

        /* Free-move: translate on the camera-facing plane. */
        if (fvp->free_dragging) {
            mat4_t view;
            editor_camera_view_matrix(&fvp->camera, &view);

            /* Camera right = row 0 of view, camera up = row 1 of view.
             * View matrix rows are: right, up, -forward (column-major). */
            vec3_t cam_right = {view.m[0], view.m[4], view.m[8]};
            vec3_t cam_up    = {view.m[1], view.m[5], view.m[9]};

            vec3_t eye = editor_camera_eye_position(&fvp->camera);
            float cam_dist = viewport_gizmo_screen_scale(
                &fvp->gizmo.position, &eye,
                fvp->camera.fov);
            float speed = cam_dist * GIZMO_DRAG_SPEED;

            /* Mouse X → camera right, mouse Y → camera up.
             * FBO is displayed Y-flipped by Clay, so SDL's downward-positive
             * Y already matches the visual direction — no negation. */
            float mx = (float)ev->xrel * speed;
            float my = (float)ev->yrel * speed;
            delta.x = cam_right.x * mx + cam_up.x * my;
            delta.y = cam_right.y * mx + cam_up.y * my;
            delta.z = cam_right.z * mx + cam_up.z * my;

            /* Accumulate raw delta, then compute snapped increment. */
            fvp->gizmo_drag_accum.x += delta.x;
            fvp->gizmo_drag_accum.y += delta.y;
            fvp->gizmo_drag_accum.z += delta.z;
            vec3_t snapped = snap_apply_position(&ed->snap,
                fvp->snap_origin_pos, fvp->gizmo_drag_accum);
            vec3_t inc = {
                snapped.x - fvp->snap_applied_delta.x,
                snapped.y - fvp->snap_applied_delta.y,
                snapped.z - fvp->snap_applied_delta.z,
            };
            fvp->snap_applied_delta = snapped;
            apply_gizmo_drag(ed, inc);
            return true;
        }

        /* Get the oriented axis direction from the gizmo orientation.
         * Column 0=X, 1=Y, 2=Z of the orientation matrix. */
        int axis_col = -1;
        switch (fvp->gizmo.active_axis) {
        case GIZMO_AXIS_X: axis_col = 0; break;
        case GIZMO_AXIS_Y: axis_col = 1; break;
        case GIZMO_AXIS_Z: axis_col = 2; break;
        default: break;
        }

        if (fvp->gizmo.mode == GIZMO_MODE_ROTATE) {
            /* Project the rotation axis onto screen space to determine
             * which mouse direction maps to positive rotation.
             * This keeps rotation intuitive regardless of camera angle. */
            mat4_t view;
            editor_camera_view_matrix(&fvp->camera, &view);

            /* Get oriented axis direction from gizmo basis. */
            vec3_t axis_dir = {0, 0, 0};
            if (axis_col >= 0) {
                const mat4_t *o = &fvp->gizmo.orientation;
                axis_dir.x = o->m[axis_col * 4 + 0];
                axis_dir.y = o->m[axis_col * 4 + 1];
                axis_dir.z = o->m[axis_col * 4 + 2];
            }

            /* Transform axis to view space (rotation only). */
            float sx = view.m[0] * axis_dir.x + view.m[4] * axis_dir.y
                      + view.m[8] * axis_dir.z;
            float sy = view.m[1] * axis_dir.x + view.m[5] * axis_dir.y
                      + view.m[9] * axis_dir.z;

            /* Screen-space perpendicular: mouse along (sy, -sx)
             * produces positive rotation around the axis. */
            float perp_x =  sy;
            float perp_y = -sx;
            float perp_len = sqrtf(perp_x * perp_x + perp_y * perp_y);

            float rot_amount;
            if (perp_len > 0.05f) {
                perp_x /= perp_len;
                perp_y /= perp_len;
                float mouse_proj = (float)ev->xrel * perp_x
                                  + (float)ev->yrel * perp_y;
                rot_amount = mouse_proj * GIZMO_ROTATE_SPEED;
            } else {
                rot_amount = (float)ev->xrel * GIZMO_ROTATE_SPEED;
            }

            /* Accumulate raw rotation, snap absolute euler, apply increment. */
            fvp->snap_rot_accum_deg += rot_amount;
            int snap_axis = (axis_col >= 0) ? axis_col : 0;

            /* Compute absolute target euler = start + accum, snap it,
             * then derive the corrected delta from start. */
            float abs_target = fvp->snap_rot_origin[snap_axis]
                             + fvp->snap_rot_accum_deg;
            float snapped_abs = snap_apply_rotation(&ed->snap,
                abs_target, snap_axis);
            /* Corrected delta from the original orientation. */
            float snapped_delta = snapped_abs
                                - fvp->snap_rot_origin[snap_axis];
            float inc_deg = snapped_delta - fvp->snap_rot_applied_deg;
            fvp->snap_rot_applied_deg = snapped_delta;

            if (fabsf(inc_deg) > 1e-6f) {
                quat_t dq = quat_from_axis_angle(
                    axis_dir, inc_deg * INPUT_DEG_TO_RAD, 1e-8f);
                apply_gizmo_rotate(ed, dq);
                fvp->gizmo_rot_accum = quat_normalize_safe(
                    quat_mul(dq, fvp->gizmo_rot_accum), 1e-8f);
            }
            return true;
        } else if (gizmo_axis_is_planar(fvp->gizmo.active_axis)) {
            /* Planar constraint: use the plane's own orthonormal
             * tangent/bitangent from the gizmo orientation.  Project
             * both to screen space and invert the resulting 2×2 matrix
             * so screen-space mouse delta maps correctly to plane
             * coordinates regardless of viewing angle. */
            mat4_t view;
            editor_camera_view_matrix(&fvp->camera, &view);
            const mat4_t *o = &fvp->gizmo.orientation;

            /* Determine which two orientation columns are the plane axes. */
            int col_a, col_b;
            switch (fvp->gizmo.active_axis) {
            case GIZMO_AXIS_XY: col_a = 0; col_b = 1; break;
            case GIZMO_AXIS_XZ: col_a = 0; col_b = 2; break;
            case GIZMO_AXIS_YZ: col_a = 1; col_b = 2; break;
            default:            col_a = 0; col_b = 1; break;
            }

            /* Plane tangent and bitangent (already orthonormal). */
            vec3_t tang = {o->m[col_a*4+0], o->m[col_a*4+1], o->m[col_a*4+2]};
            vec3_t btan = {o->m[col_b*4+0], o->m[col_b*4+1], o->m[col_b*4+2]};

            /* Project both to screen space (view rotation, XY only). */
            float sa_x = view.m[0]*tang.x + view.m[4]*tang.y + view.m[8]*tang.z;
            float sa_y = view.m[1]*tang.x + view.m[5]*tang.y + view.m[9]*tang.z;
            float sb_x = view.m[0]*btan.x + view.m[4]*btan.y + view.m[8]*btan.z;
            float sb_y = view.m[1]*btan.x + view.m[5]*btan.y + view.m[9]*btan.z;

            /* Invert the 2×2 screen-projection matrix [sa | sb]:
             *   [amount_a]   1   [ sb_y  -sb_x ] [mouse_dx]
             *   [amount_b] = - * [-sa_y   sa_x ] [mouse_dy]
             *                det                             */
            float det = sa_x * sb_y - sa_y * sb_x;

            vec3_t eye = editor_camera_eye_position(&fvp->camera);
            float cam_dist = viewport_gizmo_screen_scale(
                &fvp->gizmo.position, &eye,
                fvp->camera.fov);
            float speed = cam_dist * GIZMO_DRAG_SPEED;
            float mx = (float)ev->xrel;
            float my = (float)ev->yrel;

            /* Compute per-axis amounts (how much to move along each
             * plane axis).  Used by both translate and scale. */
            float amount_a = 0.0f, amount_b = 0.0f;

            if (fabsf(det) > 0.01f) {
                float inv_det = 1.0f / det;
                amount_a = ( sb_y * mx - sb_x * my) * inv_det;
                amount_b = (-sa_y * mx + sa_x * my) * inv_det;
            } else {
                /* Plane is edge-on: one axis is screen-visible, the
                 * other points into/out of the screen.  Map mouse
                 * motion along the visible axis's screen direction to
                 * that axis, and perpendicular motion to the hidden. */
                float la = sqrtf(sa_x*sa_x + sa_y*sa_y);
                float lb = sqrtf(sb_x*sb_x + sb_y*sb_y);
                bool a_vis = (la >= lb);
                float vsx = a_vis ? sa_x : sb_x;
                float vsy = a_vis ? sa_y : sb_y;
                float vlen = a_vis ? la : lb;
                vec3_t hid_dir = a_vis ? btan : tang;

                float vis_amt = 0.0f, hid_amt = 0.0f;
                if (vlen > 0.01f) {
                    float nvx = vsx / vlen, nvy = vsy / vlen;
                    vis_amt = mx * nvx + my * nvy;

                    float px = -nvy, py = nvx;
                    float perp_proj = mx * px + my * py;
                    float hz = view.m[2]*hid_dir.x + view.m[6]*hid_dir.y
                              + view.m[10]*hid_dir.z;
                    float sign = (hz < 0.0f) ? 1.0f : -1.0f;
                    hid_amt = perp_proj * sign;
                }
                amount_a = a_vis ? vis_amt : hid_amt;
                amount_b = a_vis ? hid_amt : vis_amt;
            }

            if (fvp->gizmo.mode == GIZMO_MODE_SCALE) {
                /* Scale: apply per-axis scale factors along each plane
                 * axis independently. The normal axis gets zero delta
                 * so scaling is truly planar-constrained. */
                float sa = amount_a * speed;
                float sb = amount_b * speed;
                /* Map plane axis indices back to XYZ scale components. */
                float scale_d[3] = {0.0f, 0.0f, 0.0f};
                scale_d[col_a] = sa;
                scale_d[col_b] = sb;
                delta.x = scale_d[0];
                delta.y = scale_d[1];
                delta.z = scale_d[2];
            } else {
                /* Translate: reconstruct world-space displacement. */
                delta.x = (tang.x * amount_a + btan.x * amount_b) * speed;
                delta.y = (tang.y * amount_a + btan.y * amount_b) * speed;
                delta.z = (tang.z * amount_a + btan.z * amount_b) * speed;
            }
        } else {
            /* Single-axis translate/scale: project mouse motion along
             * the oriented axis direction in screen space. */
            mat4_t view;
            editor_camera_view_matrix(&fvp->camera, &view);

            vec3_t axis_dir = {0, 0, 0};
            if (axis_col >= 0) {
                const mat4_t *o = &fvp->gizmo.orientation;
                axis_dir.x = o->m[axis_col * 4 + 0];
                axis_dir.y = o->m[axis_col * 4 + 1];
                axis_dir.z = o->m[axis_col * 4 + 2];
            }

            /* Project world axis to screen space (view rotation only). */
            float sx = view.m[0] * axis_dir.x + view.m[4] * axis_dir.y
                      + view.m[8] * axis_dir.z;
            float sy = view.m[1] * axis_dir.x + view.m[5] * axis_dir.y
                      + view.m[9] * axis_dir.z;
            float slen = sqrtf(sx * sx + sy * sy);

            float mouse_proj;
            if (slen > 0.05f) {
                /* Normal case: axis has visible screen projection.
                 * Dot mouse delta with screen-space axis direction. */
                sx /= slen;
                sy /= slen;
                mouse_proj = (float)ev->xrel * sx
                            + (float)ev->yrel * sy;
            } else {
                /* Edge case: axis points toward/away from camera
                 * (nearly parallel to view direction). Use mouse Y
                 * to move along the axis, with sign from view-space Z
                 * so dragging up moves the object toward the camera. */
                float sz = view.m[2] * axis_dir.x + view.m[6] * axis_dir.y
                          + view.m[10] * axis_dir.z;
                float sign = (sz < 0.0f) ? 1.0f : -1.0f;
                mouse_proj = (float)ev->yrel * sign;
            }

            vec3_t eye = editor_camera_eye_position(&fvp->camera);
            float cam_dist = viewport_gizmo_screen_scale(
                &fvp->gizmo.position, &eye,
                fvp->camera.fov);
            float speed = cam_dist * GIZMO_DRAG_SPEED;

            /* Apply along the world-space axis direction. */
            delta.x = axis_dir.x * mouse_proj * speed;
            delta.y = axis_dir.y * mouse_proj * speed;
            delta.z = axis_dir.z * mouse_proj * speed;
        }

        /* Accumulate raw delta. */
        fvp->gizmo_drag_accum.x += delta.x;
        fvp->gizmo_drag_accum.y += delta.y;
        fvp->gizmo_drag_accum.z += delta.z;

        if (fvp->gizmo.mode == GIZMO_MODE_SCALE) {
            /* Track multiplicative scale factor. */
            fvp->gizmo_scale_accum.x *= (1.0f + delta.x);
            fvp->gizmo_scale_accum.y *= (1.0f + delta.y);
            fvp->gizmo_scale_accum.z *= (1.0f + delta.z);

            /* Snap the absolute scale and apply incremental difference. */
            vec3_t snapped = snap_apply_scale(&ed->snap,
                fvp->snap_origin_scale, fvp->gizmo_scale_accum);
            /* Incremental scale delta from last applied. */
            vec3_t inc;
            inc.x = (1.0f + snapped.x - fvp->snap_applied_scale.x)
                    / 1.0f - 1.0f;
            /* Actually: entities are at scale (orig * applied_factor).
             * We need to apply (snapped / applied) multiplicatively.
             * Equivalently, delta = snapped_factor / applied_factor - 1. */
            float safe_div_x = (fabsf(fvp->snap_applied_scale.x) > 1e-9f)
                ? fvp->snap_applied_scale.x : 1.0f;
            float safe_div_y = (fabsf(fvp->snap_applied_scale.y) > 1e-9f)
                ? fvp->snap_applied_scale.y : 1.0f;
            float safe_div_z = (fabsf(fvp->snap_applied_scale.z) > 1e-9f)
                ? fvp->snap_applied_scale.z : 1.0f;
            inc.x = snapped.x / safe_div_x - 1.0f;
            inc.y = snapped.y / safe_div_y - 1.0f;
            inc.z = snapped.z / safe_div_z - 1.0f;
            fvp->snap_applied_scale = snapped;
            apply_gizmo_drag(ed, inc);
        } else {
            /* Translate: snap absolute position, apply incremental. */
            vec3_t snapped = snap_apply_position(&ed->snap,
                fvp->snap_origin_pos, fvp->gizmo_drag_accum);
            vec3_t inc = {
                snapped.x - fvp->snap_applied_delta.x,
                snapped.y - fvp->snap_applied_delta.y,
                snapped.z - fvp->snap_applied_delta.z,
            };
            fvp->snap_applied_delta = snapped;
            apply_gizmo_drag(ed, inc);
        }
        return true;
    }

    /* Right mouse or middle mouse drag: camera control per nav mode. */
    if (ed->ui.right_mouse_down || ed->ui.middle_mouse_down) {
        SDL_Keymod mod = SDL_GetModState();
        nav_mode_t nm = fvp->nav_mode;

        if (nm == NAV_MODE_FLY) {
            /* Fly mode: mouselook (rotate yaw/pitch). */
            editor_camera_orbit(&fvp->camera,
                                 -(float)ev->xrel * CAMERA_ORBIT_SPEED,
                                 -(float)ev->yrel * CAMERA_ORBIT_SPEED);
        } else if (nm == NAV_MODE_PAN_ZOOM) {
            /* Pan-zoom: always pan, never orbit. */
            editor_camera_pan(&fvp->camera,
                               -(float)ev->xrel * CAMERA_PAN_SPEED,
                               (float)ev->yrel * CAMERA_PAN_SPEED);
        } else {
            /* Orbit modes: shift=pan, otherwise orbit. */
            if (mod & KMOD_SHIFT) {
                editor_camera_pan(&fvp->camera,
                                   -(float)ev->xrel * CAMERA_PAN_SPEED,
                                   (float)ev->yrel * CAMERA_PAN_SPEED);
            } else {
                /* For orbit-cursor, snap focus to cursor before orbiting. */
                if (nm == NAV_MODE_ORBIT_CURSOR) {
                    fvp->camera.focus = fvp->cursor_3d;
                }
                editor_camera_orbit(&fvp->camera,
                                     -(float)ev->xrel * CAMERA_ORBIT_SPEED,
                                     -(float)ev->yrel * CAMERA_ORBIT_SPEED);
            }
        }
        return true;
    }

    /* Scrollbar drag: map mouse Y delta to scroll position. */
    if (ed->ui.scrollbar_dragging != 0) {
        float sc = ed->clay_be.ui_scale;
        if (sc < 1.0f) sc = 1.0f;
        float ly = (float)ev->y / sc;
        float dy = ly - ed->ui.scrollbar_drag_y;

        int sb = ed->ui.scrollbar_dragging;
        int total = 0, visible = 0;
        if (sb == 1) {
            total = ed->ui.outliner_total;
            visible = ed->ui.outliner_visible_lines;
        } else if (sb == 2) {
            total = ed->ui.inspector_total;
            visible = ed->ui.inspector_visible_lines;
        } else if (sb == 3) {
            total = ed->ui.tui_log_count;
            visible = ed->ui.tui_log_visible;
        }

        int max_scroll = total - visible;
        if (max_scroll < 0) max_scroll = 0;

        /* Get track height from panel rect. */
        panel_id_t panels[] = {PANEL_OUTLINER, PANEL_INSPECTOR, PANEL_TUI};
        panel_rect_t r = panel_layout_get_rect(&ed->layout, panels[sb - 1]);
        float track_h = (float)(r.h - THEME_ROW_HEIGHT);
        if (track_h < 1.0f) track_h = 1.0f;

        /* Convert pixel drag to scroll units. */
        float scroll_per_px = (float)max_scroll / track_h;
        /* TUI scroll is inverted: scroll=0 is bottom (newest),
         * so dragging down should decrease scroll. */
        int delta_scroll = (int)(dy * scroll_per_px);
        if (sb == 3) delta_scroll = -delta_scroll;
        int new_scroll = ed->ui.scrollbar_drag_scroll + delta_scroll;
        if (new_scroll < 0) new_scroll = 0;
        if (new_scroll > max_scroll) new_scroll = max_scroll;

        if (sb == 1) ed->ui.outliner_scroll = new_scroll;
        else if (sb == 2) ed->ui.inspector_scroll = new_scroll;
        else if (sb == 3) ed->ui.tui_log_scroll = new_scroll;

        return true;
    }

    if (ed->dragging_divider == DIVIDER_NONE) return false;

    /* Scale physical pixel delta to logical pixels for the layout. */
    float sc = ed->clay_be.ui_scale;
    if (sc < 1.0f) sc = 1.0f;
    int raw_delta = (ed->dragging_divider == DIVIDER_BOTTOM) ? ev->yrel : ev->xrel;
    int delta = (int)((float)raw_delta / sc);
    panel_layout_drag_divider(&ed->layout, ed->dragging_divider, delta);
    return true;
}

/**
 * @brief Push a command into the history ring buffer.
 *
 * Skips empty strings and duplicates of the most recent entry.
 */
static void tui_history_push(scene_ui_state_t *ui, const char *cmd) {
    if (!cmd || cmd[0] == '\0') return;

    /* Skip if identical to previous entry. */
    if (ui->tui_history_count > 0) {
        int prev = (ui->tui_history_head - 1 + UI_TUI_HISTORY_MAX)
                   % UI_TUI_HISTORY_MAX;
        if (strcmp(ui->tui_history[prev], cmd) == 0) return;
    }

    strncpy(ui->tui_history[ui->tui_history_head], cmd,
            UI_TUI_INPUT_MAX - 1);
    ui->tui_history[ui->tui_history_head][UI_TUI_INPUT_MAX - 1] = '\0';
    ui->tui_history_head = (ui->tui_history_head + 1) % UI_TUI_HISTORY_MAX;
    if (ui->tui_history_count < UI_TUI_HISTORY_MAX) {
        ui->tui_history_count++;
    }
}

/**
 * @brief Handle tab completion for TUI command input.
 *
 * Completes command names using ctrl_cmd_complete(). If a single match
 * is found, replaces the input. If multiple matches, logs them and
 * fills the common prefix.
 */
static void handle_tui_tab(scene_editor_t *ed) {
    scene_ui_state_t *ui = &ed->ui;
    if (ui->tui_input_len == 0) return;

    /* Only complete the first word (command name). */
    char *space = strchr(ui->tui_input, ' ');
    if (space) return;  /* Already past command name — no arg completion yet. */

    const char *matches[32];
    uint32_t match_count = ctrl_cmd_complete(ui->tui_input, matches, 32);

    if (match_count == 1) {
        /* Single match — replace input with match + space. */
        size_t len = strlen(matches[0]);
        if (len < UI_TUI_INPUT_MAX - 2) {
            memcpy(ui->tui_input, matches[0], len);
            ui->tui_input[len] = ' ';
            ui->tui_input_len = (int)(len + 1);
            ui->tui_input[ui->tui_input_len] = '\0';
            ui->tui_cursor = ui->tui_input_len;
        }
    } else if (match_count > 1) {
        /* Multiple matches — log them. */
        char line[512];
        int pos = 0;
        for (uint32_t i = 0; i < match_count; i++) {
            int n = snprintf(line + pos, sizeof(line) - (size_t)pos,
                             "  %s", matches[i]);
            if (n > 0) pos += n;
        }
        scene_ui_tui_log(ui, line);

        /* Fill common prefix. */
        size_t common = strlen(matches[0]);
        for (uint32_t i = 1; i < match_count; i++) {
            size_t j = 0;
            while (j < common && matches[0][j] == matches[i][j]) j++;
            common = j;
        }
        if (common > (size_t)ui->tui_input_len) {
            memcpy(ui->tui_input, matches[0], common);
            ui->tui_input_len = (int)common;
            ui->tui_input[ui->tui_input_len] = '\0';
            ui->tui_cursor = ui->tui_input_len;
        }
    }
}

/**
 * @brief Insert a character at the TUI cursor position.
 */
static void tui_insert_char(scene_ui_state_t *ui, char ch) {
    if (ui->tui_input_len >= UI_TUI_INPUT_MAX - 1) return;
    /* Shift characters right to make room at cursor. */
    memmove(&ui->tui_input[ui->tui_cursor + 1],
            &ui->tui_input[ui->tui_cursor],
            (size_t)(ui->tui_input_len - ui->tui_cursor));
    ui->tui_input[ui->tui_cursor] = ch;
    ui->tui_cursor++;
    ui->tui_input_len++;
    ui->tui_input[ui->tui_input_len] = '\0';
}

/**
 * @brief Handle key down when TUI has keyboard focus.
 *
 * Handles cursor movement, backspace, delete, enter (submit),
 * and escape (deactivate). Returns true if the key was consumed.
 */
static bool handle_tui_key(scene_editor_t *ed, const SDL_KeyboardEvent *ev) {
    scene_ui_state_t *ui = &ed->ui;
    SDL_Keycode key = ev->keysym.sym;

    switch (key) {
    case SDLK_ESCAPE:
        /* Deactivate TUI input, return focus to viewport. */
        ui->tui_active = false;
        panel_layout_focus_viewport(&ed->layout);
        return true;

    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        /* Submit the command if non-empty. */
        if (ui->tui_input_len > 0) {
            /* Push to command history before clearing. */
            tui_history_push(ui, ui->tui_input);
            ui->tui_history_index = -1;

            /* Echo the typed command to the log. */
            scene_ui_tui_log(ui, ui->tui_input);

            /* Copy command to tui_cmd before clearing input. */
            memcpy(ui->tui_cmd, ui->tui_input, (size_t)(ui->tui_input_len + 1));
            ui->action = UI_ACTION_TUI_COMMAND;

            /* Clear input and deactivate TUI. */
            ui->tui_input[0] = '\0';
            ui->tui_input_len = 0;
            ui->tui_cursor = 0;
            ui->tui_active = false;
        }
        return true;

    case SDLK_BACKSPACE:
        if (ui->tui_cursor > 0) {
            memmove(&ui->tui_input[ui->tui_cursor - 1],
                    &ui->tui_input[ui->tui_cursor],
                    (size_t)(ui->tui_input_len - ui->tui_cursor));
            ui->tui_cursor--;
            ui->tui_input_len--;
            ui->tui_input[ui->tui_input_len] = '\0';
        }
        return true;

    case SDLK_DELETE:
        if (ui->tui_cursor < ui->tui_input_len) {
            memmove(&ui->tui_input[ui->tui_cursor],
                    &ui->tui_input[ui->tui_cursor + 1],
                    (size_t)(ui->tui_input_len - ui->tui_cursor - 1));
            ui->tui_input_len--;
            ui->tui_input[ui->tui_input_len] = '\0';
        }
        return true;

    case SDLK_LEFT:
        if (ui->tui_cursor > 0) ui->tui_cursor--;
        return true;

    case SDLK_RIGHT:
        if (ui->tui_cursor < ui->tui_input_len) ui->tui_cursor++;
        return true;

    case SDLK_HOME:
        ui->tui_cursor = 0;
        return true;

    case SDLK_END:
        ui->tui_cursor = ui->tui_input_len;
        return true;

    case SDLK_UP:
        /* Browse command history backward. */
        if (ui->tui_history_count > 0) {
            if (ui->tui_history_index == -1) {
                /* Stash current input before browsing. */
                memcpy(ui->tui_history_stash, ui->tui_input,
                       (size_t)(ui->tui_input_len + 1));
                ui->tui_history_index = 0;
            } else if (ui->tui_history_index < ui->tui_history_count - 1) {
                ui->tui_history_index++;
            }
            /* Copy history entry to input. */
            int slot = (ui->tui_history_head - 1 - ui->tui_history_index
                        + UI_TUI_HISTORY_MAX) % UI_TUI_HISTORY_MAX;
            strncpy(ui->tui_input, ui->tui_history[slot],
                    UI_TUI_INPUT_MAX - 1);
            ui->tui_input[UI_TUI_INPUT_MAX - 1] = '\0';
            ui->tui_input_len = (int)strlen(ui->tui_input);
            ui->tui_cursor = ui->tui_input_len;
        }
        return true;

    case SDLK_DOWN:
        /* Browse command history forward. */
        if (ui->tui_history_index > 0) {
            ui->tui_history_index--;
            int slot = (ui->tui_history_head - 1 - ui->tui_history_index
                        + UI_TUI_HISTORY_MAX) % UI_TUI_HISTORY_MAX;
            strncpy(ui->tui_input, ui->tui_history[slot],
                    UI_TUI_INPUT_MAX - 1);
            ui->tui_input[UI_TUI_INPUT_MAX - 1] = '\0';
            ui->tui_input_len = (int)strlen(ui->tui_input);
            ui->tui_cursor = ui->tui_input_len;
        } else if (ui->tui_history_index == 0) {
            /* Restore stashed input. */
            ui->tui_history_index = -1;
            memcpy(ui->tui_input, ui->tui_history_stash,
                   UI_TUI_INPUT_MAX);
            ui->tui_input_len = (int)strlen(ui->tui_input);
            ui->tui_cursor = ui->tui_input_len;
        }
        return true;

    case SDLK_TAB:
        handle_tui_tab(ed);
        return true;

    case SDLK_u:
        /* Ctrl+U: clear line. */
        if (ev->keysym.mod & KMOD_CTRL) {
            ui->tui_input[0] = '\0';
            ui->tui_input_len = 0;
            ui->tui_cursor = 0;
            return true;
        }
        break;

    default:
        break;
    }

    return false;
}

/**
 * @brief Handle SDL_TEXTINPUT event: insert typed text into TUI input.
 */
static bool handle_text_input(scene_editor_t *ed, const char *text) {
    if (!ed->ui.tui_active) return false;

    /* Skip the ':' character that triggered TUI activation. */
    if (ed->ui.tui_skip_next_text) {
        ed->ui.tui_skip_next_text = false;
        return true;
    }

    for (int i = 0; text[i] != '\0'; i++) {
        char ch = text[i];
        if (ch >= 32 && ch < 127) {
            tui_insert_char(&ed->ui, ch);
        }
    }
    return true;
}

/**
 * @brief Handle key down: panel toggles, entity operations, TUI routing.
 */
/** @brief Returns true for keys that should fire continuously while held
 *  (camera movement, zoom, numpad orbit, scroll). */
static bool key_is_continuous(SDL_Keycode key) {
    switch (key) {
    /* Fly mode camera movement. */
    case SDLK_UP: case SDLK_DOWN: case SDLK_LEFT: case SDLK_RIGHT:
    /* Zoom. */
    case SDLK_PLUS: case SDLK_EQUALS: case SDLK_MINUS:
    /* Numpad orbit. */
    case SDLK_2: case SDLK_4: case SDLK_6: case SDLK_8: case SDLK_9:
    /* Scroll (handled earlier but included for completeness). */
    case SDLK_PAGEUP: case SDLK_PAGEDOWN: case SDLK_HOME: case SDLK_END:
        return true;
    default:
        return false;
    }
}

static bool handle_key_down(scene_editor_t *ed, const SDL_KeyboardEvent *ev) {
    SDL_Keycode key = ev->keysym.sym;
    viewport_state_t *fvp = scene_focused_vp(ed);

    /* If TUI is active, route keys to TUI input handler first. */
    if (ed->ui.tui_active) {
        return handle_tui_key(ed, ev);
    }

    /* One-shot guard: for non-continuous keys, require a KEYUP between
     * presses. This prevents double-fire from OS-level key repeat or
     * brief release/repress generating two KEYDOWN events. */
    if (!key_is_continuous(key)) {
        if (!key_mark_held(&ed->ui, (uint32_t)key)) {
            return true;  /* Already held — swallow the event. */
        }
    }

    /* Alt+Arrow: split focused viewport in the BSP tree.
     * Alt+Number: focus viewport by display number (1-9). */
    if (ev->keysym.mod & KMOD_ALT) {
        /* Alt+1..9: switch focus to viewport by display number. */
        if (key >= SDLK_1 && key <= SDLK_9) {
            int target_num = key - SDLK_1 + 1;
            int count = 0;
            for (int i = 0; i < VIEWPORT_MAX_COUNT; i++) {
                if (!ed->viewports[i].active) continue;
                if (ed->viewports[i].rect.w <= 0) continue;
                count++;
                if (count == target_num) {
                    if ((uint8_t)i != ed->vp_bsp.focused_viewport) {
                        ed->prev_focused_vp = ed->vp_bsp.focused_viewport;
                    }
                    ed->vp_bsp.focused_viewport = (uint8_t)i;
                    break;
                }
            }
            return true;
        }

        viewport_split_dir_t split_dir = SPLIT_NONE;
        bool original_first = true;
        switch (key) {
        case SDLK_LEFT:
            split_dir = SPLIT_VERTICAL;
            original_first = true;
            break;
        case SDLK_RIGHT:
            split_dir = SPLIT_VERTICAL;
            original_first = false;
            break;
        case SDLK_UP:
            split_dir = SPLIT_HORIZONTAL;
            original_first = true;
            break;
        case SDLK_DOWN:
            split_dir = SPLIT_HORIZONTAL;
            original_first = false;
            break;
        default:
            break;
        }
        if (split_dir != SPLIT_NONE) {
            uint8_t new_vp;
            if (viewport_bsp_split(&ed->vp_bsp,
                                    ed->vp_bsp.focused_viewport,
                                    split_dir, original_first, &new_vp)) {
                viewport_state_init(&ed->viewports[new_vp]);
                viewport_state_copy_camera(&ed->viewports[new_vp], fvp);
            }
            return true;
        }
    }

    /* Gizmo nudge: arrow keys apply exact rotation steps when an axis
     * is active (either during drag or after selecting an axis). */
    if (fvp->gizmo.active_axis != GIZMO_AXIS_NONE &&
        fvp->gizmo.mode == GIZMO_MODE_ROTATE) {
        float step_deg = 0.0f;
        if (key == SDLK_UP)   step_deg =  15.0f;
        if (key == SDLK_DOWN) step_deg = -15.0f;
        if (step_deg != 0.0f) {
            /* Get oriented axis from gizmo orientation matrix. */
            int axis_col = -1;
            switch (fvp->gizmo.active_axis) {
            case GIZMO_AXIS_X: axis_col = 0; break;
            case GIZMO_AXIS_Y: axis_col = 1; break;
            case GIZMO_AXIS_Z: axis_col = 2; break;
            default: break;
            }
            vec3_t axis_dir = {0, 1, 0};
            if (axis_col >= 0) {
                const mat4_t *o = &fvp->gizmo.orientation;
                axis_dir.x = o->m[axis_col * 4 + 0];
                axis_dir.y = o->m[axis_col * 4 + 1];
                axis_dir.z = o->m[axis_col * 4 + 2];
            }
            quat_t dq = quat_from_axis_angle(
                axis_dir, step_deg * INPUT_DEG_TO_RAD, 1e-8f);
            apply_gizmo_rotate(ed, dq);
            fvp->gizmo_rot_accum = quat_normalize_safe(
                quat_mul(dq, fvp->gizmo_rot_accum), 1e-8f);
            /* If not mid-drag, snap then send so server gets abs orientation. */
            if (!fvp->gizmo.dragging) {
                snap_selected_euler_axes(ed);
                vec3_t unused = {0, 0, 0};
                send_gizmo_commands(ed, unused);
            }
            return true;
        }
    }

    /* Unified scroll keys: scroll whichever panel is focused. */
    {
        int *scroll_ptr = NULL;
        int max_scroll = 0;
        int step = 1;       /* lines for outliner/tui, pixels for inspector */
        int page_step = 1;

        switch (ed->layout.focus) {
        case PANEL_OUTLINER:
            scroll_ptr = &ed->ui.outliner_scroll;
            max_scroll = ed->ui.outliner_total - ed->ui.outliner_visible_lines;
            step = 1;
            page_step = ed->ui.outliner_visible_lines;
            break;
        case PANEL_INSPECTOR:
            scroll_ptr = &ed->ui.inspector_scroll;
            max_scroll = ed->ui.inspector_total - ed->ui.inspector_visible_lines;
            step = THEME_ROW_HEIGHT;
            page_step = ed->ui.inspector_visible_lines;
            break;
        case PANEL_TUI:
            scroll_ptr = &ed->ui.tui_log_scroll;
            max_scroll = ed->ui.tui_log_count - ed->ui.tui_log_visible;
            step = -1;          /* Inverted: scroll value counts back from newest. */
            page_step = -(ed->ui.tui_log_visible > 0 ? ed->ui.tui_log_visible : 1);
            break;
        default:
            break;
        }

        if (max_scroll < 0) max_scroll = 0;

        if (scroll_ptr) {
            bool handled = true;
            switch (key) {
            case SDLK_UP:
                *scroll_ptr -= step;
                break;
            case SDLK_DOWN:
                *scroll_ptr += step;
                break;
            case SDLK_PAGEUP:
                *scroll_ptr -= page_step;
                break;
            case SDLK_PAGEDOWN:
                *scroll_ptr += page_step;
                break;
            case SDLK_HOME:
                *scroll_ptr = 0;
                break;
            case SDLK_END:
                *scroll_ptr = max_scroll;
                break;
            default:
                handled = false;
                break;
            }
            if (handled) {
                if (*scroll_ptr < 0) *scroll_ptr = 0;
                if (*scroll_ptr > max_scroll) *scroll_ptr = max_scroll;
                return true;
            }
        }
    }

    /* Panel toggles: F5=Outliner, F6=Viewport, F7=Inspector, F8=TUI */
    switch (key) {
    case SDLK_F5:
        panel_layout_toggle(&ed->layout, PANEL_OUTLINER);
        return true;
    case SDLK_F6:
        panel_layout_toggle(&ed->layout, PANEL_VIEWPORT);
        return true;
    case SDLK_F7:
        panel_layout_toggle(&ed->layout, PANEL_INSPECTOR);
        return true;
    case SDLK_F8:
        panel_layout_toggle(&ed->layout, PANEL_TUI);
        return true;
    case SDLK_F11:
        /* Toggle fullscreen */
        return true;
    case SDLK_TAB:
        if (ev->keysym.mod & KMOD_SHIFT) {
            panel_layout_focus_prev(&ed->layout);
        } else {
            panel_layout_focus_next(&ed->layout);
        }
        /* Activate TUI input when TUI receives focus. */
        ed->ui.tui_active = (ed->layout.focus == PANEL_TUI);
        return true;
    case SDLK_ESCAPE: {
        /* Close the focused viewport if multiple exist.
         * The last remaining viewport cannot be closed. */
        uint8_t fvp_idx = ed->vp_bsp.focused_viewport;
        if (ed->vp_bsp.viewport_count > 1) {
            if (viewport_bsp_close(&ed->vp_bsp, fvp_idx)) {
                viewport_state_t *cvs = &ed->viewports[fvp_idx];
                /* Viewport 0 shares the renderer's FBO — only delete
                 * FBO resources for viewports that own their own. */
                if (fvp_idx != 0 && cvs->fbo_valid) {
                    viewport_render_state_t *rend = &ed->viewport;
                    rend->glDeleteFramebuffers(1, &cvs->fbo);
                    rend->glDeleteTextures(1, &cvs->color_tex);
                    rend->glDeleteRenderbuffers(1, &cvs->depth_rbo);
                }
                viewport_state_init(cvs);
                cvs->active = false;
            }
            return true;
        }
        ed->ui.tui_active = false;
        panel_layout_focus_viewport(&ed->layout);
        return true;
    }

    /* '^': swap focused viewport with previously focused. */
    case SDLK_CARET: {
        uint8_t prev = ed->prev_focused_vp;
        if (prev != ed->vp_bsp.focused_viewport &&
            ed->viewports[prev].active) {
            ed->prev_focused_vp = ed->vp_bsp.focused_viewport;
            ed->vp_bsp.focused_viewport = prev;
        }
        return true;
    }

    /* ':' activates TUI command input (vim-style). */
    case SDLK_SEMICOLON:
        if (ev->keysym.mod & KMOD_SHIFT) {
            /* Shift+; = ':' on US keyboard layout.
             * Start text input so SDL sends TEXTINPUT events, then
             * suppress the ':' character that SDL_TEXTINPUT will
             * generate this frame by setting a skip flag. */
            ed->ui.tui_active = true;
            ed->ui.tui_skip_next_text = true;
            panel_layout_set_focus(&ed->layout, PANEL_TUI);
            SDL_StartTextInput();
            return true;
        }
        break;

    /* Entity operations. */
    case SDLK_DELETE:
    case SDLK_x:
        if (ev->keysym.mod & KMOD_SHIFT) {
            ed->ui.action = UI_ACTION_DELETE_SELECTED;
            return true;
        }
        if (key == SDLK_DELETE) {
            ed->ui.action = UI_ACTION_DELETE_SELECTED;
            return true;
        }
        break;

    /* Transform mode shortcuts (toggle: pressing same key disables gizmo).
     * W/E/R = Translate/Rotate/Scale (standard layout). */
    case SDLK_w:
        if (fvp->gizmo.mode == GIZMO_MODE_TRANSLATE) {
            ed->ui.action = UI_ACTION_MODE_NONE;
        } else {
            ed->ui.action = UI_ACTION_MODE_TRANSLATE;
        }
        return true;
    case SDLK_e:
        if (fvp->gizmo.mode == GIZMO_MODE_ROTATE) {
            ed->ui.action = UI_ACTION_MODE_NONE;
        } else {
            ed->ui.action = UI_ACTION_MODE_ROTATE;
        }
        return true;
    case SDLK_r:
        if (fvp->gizmo.mode == GIZMO_MODE_SCALE) {
            ed->ui.action = UI_ACTION_MODE_NONE;
        } else {
            ed->ui.action = UI_ACTION_MODE_SCALE;
        }
        return true;

    /* Cycle transform mode: Nav → Sel → Move → Rot → Scale. */
    case SDLK_PERIOD: {
        uint8_t next = (ed->ui.transform_mode + 1) % UI_MODE_COUNT;
        ed->ui.transform_mode = next;
        switch (next) {
        case UI_MODE_TRANSLATE:
            gizmo_state_set_mode(&fvp->gizmo, GIZMO_MODE_TRANSLATE); break;
        case UI_MODE_ROTATE:
            gizmo_state_set_mode(&fvp->gizmo, GIZMO_MODE_ROTATE); break;
        case UI_MODE_SCALE:
            gizmo_state_set_mode(&fvp->gizmo, GIZMO_MODE_SCALE); break;
        default:
            gizmo_state_set_mode(&fvp->gizmo, GIZMO_MODE_NONE); break;
        }
        return true;
    }

    /* Cycle transform basis: World → Local → View → Cursor. */
    case SDLK_COMMA: {
        fvp->gizmo.basis = transform_basis_next(fvp->gizmo.basis);
        char msg[64];
        snprintf(msg, sizeof(msg), "Basis: %s",
                 transform_basis_name(fvp->gizmo.basis));
        scene_ui_tui_log(&ed->ui, msg);
        return true;
    }

    /* Cycle viewport shading mode: Shaded → Matcap → Wire. */
    case SDLK_SLASH: {
        fvp->shading_mode = shading_mode_next(fvp->shading_mode);
        char msg[64];
        snprintf(msg, sizeof(msg), "Shading: %s",
                 shading_mode_name(fvp->shading_mode));
        scene_ui_tui_log(&ed->ui, msg);
        return true;
    }

    /* Cycle viewport navigation mode: Orbit → OrbCur → Fly → PanZm. */
    case SDLK_n: {
        nav_mode_t old_mode = fvp->nav_mode;
        nav_mode_t new_mode = nav_mode_next(old_mode);
        /* Handle transitions to/from fly mode. */
        if (old_mode == NAV_MODE_FLY && new_mode != NAV_MODE_FLY) {
            editor_camera_exit_fly(&fvp->camera, 10.0f);
        } else if (old_mode != NAV_MODE_FLY && new_mode == NAV_MODE_FLY) {
            editor_camera_enter_fly(&fvp->camera);
        }
        fvp->nav_mode = new_mode;
        char msg[64];
        snprintf(msg, sizeof(msg), "Nav: %s", nav_mode_name(new_mode));
        scene_ui_tui_log(&ed->ui, msg);
        return true;
    }

    /* Keyboard zoom: +/- (also = for unshifted plus). */
    case SDLK_PLUS:
    case SDLK_EQUALS:
        editor_camera_zoom(&fvp->camera, -CAMERA_ZOOM_SPEED);
        return true;
    case SDLK_MINUS:
        editor_camera_zoom(&fvp->camera, CAMERA_ZOOM_SPEED);
        return true;

    /* A: select all / Shift+A: deselect all. */
    case SDLK_a: {
        SDL_Keymod km = SDL_GetModState();
        if (km & KMOD_SHIFT) {
            /* Shift+A: deselect all — sync each to server. */
            uint32_t sc = edit_selection_count(&ed->selection);
            const uint32_t *sids = edit_selection_ids(&ed->selection);
            for (uint32_t si = 0; si < sc; ++si) {
                char buf[256];
                uint32_t cid = scene_connection_next_id(&ed->connection);
                int n = scene_cmd_format_deselect(buf, sizeof(buf),
                                                   cid, sids[si]);
                if (n > 0 && ed->connected) {
                    ctrl_conn_send_raw(&ed->connection.tcp,
                                       buf, (uint32_t)n);
                }
            }
            edit_selection_clear(&ed->selection);
            ed->active_object_id = EDIT_ENTITY_INVALID_ID;
        } else {
            /* A: select all non-deleted entities — sync each to server. */
            for (uint32_t i = 0; i < ed->entities.capacity; ++i) {
                const edit_entity_t *ent =
                    edit_entity_store_get(&ed->entities, i);
                if (ent && !ent->pending_delete) {
                    if (!edit_selection_contains(&ed->selection, i)) {
                        char buf[256];
                        uint32_t cid =
                            scene_connection_next_id(&ed->connection);
                        int n = scene_cmd_format_select(buf, sizeof(buf),
                                                        cid, i);
                        if (n > 0 && ed->connected) {
                            ctrl_conn_send_raw(&ed->connection.tcp,
                                               buf, (uint32_t)n);
                        }
                    }
                    edit_selection_add(&ed->selection, i);
                }
            }
        }
        return true;
    }

    /* D: duplicate selected entities via per-entity clone_id.
     * clone_id returns a delta sync response, so new entities
     * appear in the viewport immediately when the response arrives.
     * Deselect originals so only the clones end up selected. */
    case SDLK_d: {
        uint32_t sel_count = edit_selection_count(&ed->selection);
        if (sel_count > 0) {
            const uint32_t *sel_ids = edit_selection_ids(&ed->selection);
            uint32_t last_cid = 0;
            for (uint32_t si = 0; si < sel_count; ++si) {
                char buf[256];
                uint32_t cid = scene_connection_next_id(&ed->connection);
                int n = snprintf(buf, sizeof(buf),
                    "{\"id\":%u,\"cmd\":\"clone_id\",\"args\":"
                    "{\"entity_id\":%u}}\n",
                    (unsigned)cid, (unsigned)sel_ids[si]);
                if (n > 0 && (size_t)n < sizeof(buf)) {
                    scene_connection_send_cmd(&ed->connection, buf);
                }
                last_cid = cid;
            }
            /* Deselect originals — clones will be selected when the
             * delta sync response arrives and is processed. */
            edit_selection_clear(&ed->selection);
            scene_ui_tui_log_pending(&ed->ui, "clone", last_cid);
        }
        return true;
    }

    /* Arrow keys: fly mode movement (forward/back/left/right). */
    case SDLK_UP:
        if (fvp->nav_mode == NAV_MODE_FLY) {
            editor_camera_fly_move(&fvp->camera, CAMERA_FLY_SPEED, 0, 0);
            return true;
        }
        break;
    case SDLK_DOWN:
        if (fvp->nav_mode == NAV_MODE_FLY) {
            editor_camera_fly_move(&fvp->camera, -CAMERA_FLY_SPEED, 0, 0);
            return true;
        }
        break;
    case SDLK_LEFT:
        if (fvp->nav_mode == NAV_MODE_FLY) {
            editor_camera_fly_move(&fvp->camera, 0, -CAMERA_FLY_SPEED, 0);
            return true;
        }
        break;
    case SDLK_RIGHT:
        if (fvp->nav_mode == NAV_MODE_FLY) {
            editor_camera_fly_move(&fvp->camera, 0, CAMERA_FLY_SPEED, 0);
            return true;
        }
        break;

    /* Snap view shortcuts (number row keys). */
    case SDLK_1:
        if (ev->keysym.mod & KMOD_CTRL) {
            editor_camera_snap_view(&fvp->camera, EDITOR_VIEW_BACK);
        } else {
            editor_camera_snap_view(&fvp->camera, EDITOR_VIEW_FRONT);
        }
        return true;
    case SDLK_3:
        if (ev->keysym.mod & KMOD_CTRL) {
            editor_camera_snap_view(&fvp->camera, EDITOR_VIEW_LEFT);
        } else {
            editor_camera_snap_view(&fvp->camera, EDITOR_VIEW_RIGHT);
        }
        return true;
    case SDLK_7:
        if (ev->keysym.mod & KMOD_CTRL) {
            editor_camera_snap_view(&fvp->camera, EDITOR_VIEW_BOTTOM);
        } else {
            editor_camera_snap_view(&fvp->camera, EDITOR_VIEW_TOP);
        }
        return true;
    case SDLK_5:
        editor_camera_toggle_projection(&fvp->camera);
        return true;

    /* Incremental orbit (15-degree steps). */
    case SDLK_8:
        editor_camera_orbit(&fvp->camera,
                             0.0f, 15.0f * INPUT_DEG_TO_RAD);
        return true;
    case SDLK_2:
        editor_camera_orbit(&fvp->camera,
                             0.0f, -15.0f * INPUT_DEG_TO_RAD);
        return true;
    case SDLK_4:
        editor_camera_orbit(&fvp->camera,
                             -15.0f * INPUT_DEG_TO_RAD, 0.0f);
        return true;
    case SDLK_6:
        editor_camera_orbit(&fvp->camera,
                             15.0f * INPUT_DEG_TO_RAD, 0.0f);
        return true;
    case SDLK_9:
        /* Flip view 180° around yaw. */
        editor_camera_orbit(&fvp->camera,
                             180.0f * INPUT_DEG_TO_RAD, 0.0f);
        return true;
    case SDLK_f:
        /* Frame selection: compute AABB of selected entities and frame it. */
        if (edit_selection_count(&ed->selection) > 0) {
            vec3_t aabb_min = { 1e30f,  1e30f,  1e30f};
            vec3_t aabb_max = {-1e30f, -1e30f, -1e30f};
            uint32_t sel_count = edit_selection_count(&ed->selection);
            const uint32_t *sel_ids = edit_selection_ids(&ed->selection);
            for (uint32_t si = 0; si < sel_count; si++) {
                const edit_entity_t *ent =
                    edit_entity_store_get(&ed->entities, sel_ids[si]);
                if (!ent) continue;
                /* Expand AABB by entity position ± half scale. */
                float hx = fabsf(ent->scale[0]) * 0.5f;
                float hy = fabsf(ent->scale[1]) * 0.5f;
                float hz = fabsf(ent->scale[2]) * 0.5f;
                if (ent->pos[0] - hx < aabb_min.x) aabb_min.x = ent->pos[0] - hx;
                if (ent->pos[1] - hy < aabb_min.y) aabb_min.y = ent->pos[1] - hy;
                if (ent->pos[2] - hz < aabb_min.z) aabb_min.z = ent->pos[2] - hz;
                if (ent->pos[0] + hx > aabb_max.x) aabb_max.x = ent->pos[0] + hx;
                if (ent->pos[1] + hy > aabb_max.y) aabb_max.y = ent->pos[1] + hy;
                if (ent->pos[2] + hz > aabb_max.z) aabb_max.z = ent->pos[2] + hz;
            }
            editor_camera_frame_selection(&fvp->camera,
                                           aabb_min, aabb_max);
        }
        return true;

    /* G: toggle snap for the current gizmo mode.
     * Ctrl+G: toggle all snap types at once. */
    case SDLK_g: {
        snap_state_t *sn = &ed->snap;
        if (ev->keysym.mod & KMOD_SHIFT) {
            /* Shift+G: enable all three snaps, unless all three are
             * already on — in that case, disable all three. */
            bool all_on = sn->enabled[SNAP_POSITION] &&
                          sn->enabled[SNAP_ROTATION] &&
                          sn->enabled[SNAP_SCALE];
            bool new_val = !all_on;
            sn->enabled[SNAP_POSITION] = new_val;
            sn->enabled[SNAP_ROTATION] = new_val;
            sn->enabled[SNAP_SCALE]    = new_val;
            /* When enabling, immediately snap selected entities' eulers
             * and sync per-entity absolute orientation to server. */
            if (new_val) {
                snap_selected_euler_axes(ed);
                send_per_entity_abs_(ed, GIZMO_MODE_ROTATE);
            }
            char msg[64];
            snprintf(msg, sizeof(msg), "Snap: %s", new_val ? "ALL ON" : "OFF");
            scene_ui_tui_log(&ed->ui, msg);
        } else {
            /* G: toggle snap for the current gizmo mode. */
            snap_transform_type_t st = SNAP_POSITION;
            const char *type_name = "Position";
            switch (fvp->gizmo.mode) {
            case GIZMO_MODE_ROTATE:
                st = SNAP_ROTATION;
                type_name = "Rotation";
                break;
            case GIZMO_MODE_SCALE:
                st = SNAP_SCALE;
                type_name = "Scale";
                break;
            default:
                break;
            }
            sn->enabled[st] = !sn->enabled[st];
            /* When enabling rotation snap, immediately snap eulers
             * and sync per-entity absolute orientation to server. */
            if (sn->enabled[st] && st == SNAP_ROTATION) {
                snap_selected_euler_axes(ed);
                send_per_entity_abs_(ed, GIZMO_MODE_ROTATE);
            }
            char msg[64];
            snprintf(msg, sizeof(msg), "%s snap: %s (grid=%.2g)",
                     type_name,
                     sn->enabled[st] ? "ON" : "OFF",
                     (double)sn->grid_size[st]);
            scene_ui_tui_log(&ed->ui, msg);
        }
        return true;
    }

    default:
        break;
    }

    return false;
}

/* ---- Public API ---- */

bool scene_input_process(struct scene_editor *ed, const union SDL_Event *event) {
    switch (event->type) {
    case SDL_QUIT:
        ed->running = false;
        return true;

    case SDL_WINDOWEVENT:
        if (event->window.event == SDL_WINDOWEVENT_RESIZED ||
            event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            /* Layout uses logical pixels (physical / ui_scale).
             * render_frame() calls panel_layout_resize() each frame
             * with the correct logical dimensions, so skip it here
             * to avoid a one-frame mismatch with physical sizes. */
            return true;
        }
        break;

    case SDL_MOUSEBUTTONDOWN:
        return handle_mouse_down(ed, &event->button);

    case SDL_MOUSEBUTTONUP:
        return handle_mouse_up(ed, &event->button);

    case SDL_MOUSEMOTION:
        return handle_mouse_motion(ed, &event->motion);

    case SDL_MOUSEWHEEL: {
        /* Route scroll wheel based on which panel the mouse is over. */
        float sc = ed->clay_be.ui_scale;
        if (sc < 1.0f) sc = 1.0f;
        int lx = (int)(ed->ui.mouse_x / sc);
        int ly = (int)(ed->ui.mouse_y / sc);
        panel_id_t hover_panel = panel_layout_hit_test(&ed->layout, lx, ly);
        if (hover_panel == PANEL_VIEWPORT) {
            viewport_state_t *fvp = scene_focused_vp(ed);
            editor_camera_zoom(&fvp->camera,
                                -(float)event->wheel.y * CAMERA_ZOOM_SPEED);
        } else if (hover_panel == PANEL_TUI) {
            /* Scroll TUI log: wheel up = scroll back, wheel down = toward newest. */
            int max_scroll = ed->ui.tui_log_count - ed->ui.tui_log_visible;
            if (max_scroll < 0) max_scroll = 0;
            ed->ui.tui_log_scroll += event->wheel.y;
            if (ed->ui.tui_log_scroll < 0) ed->ui.tui_log_scroll = 0;
            if (ed->ui.tui_log_scroll > max_scroll)
                ed->ui.tui_log_scroll = max_scroll;
        } else if (hover_panel == PANEL_OUTLINER) {
            /* Scroll outliner entity list. */
            int max_scroll = ed->ui.outliner_total - ed->ui.outliner_visible_lines;
            if (max_scroll < 0) max_scroll = 0;
            ed->ui.outliner_scroll -= event->wheel.y;
            if (ed->ui.outliner_scroll < 0) ed->ui.outliner_scroll = 0;
            if (ed->ui.outliner_scroll > max_scroll)
                ed->ui.outliner_scroll = max_scroll;
        } else if (hover_panel == PANEL_INSPECTOR) {
            /* Scroll inspector (pixel-based). */
            int max_scroll = ed->ui.inspector_total
                             - ed->ui.inspector_visible_lines;
            if (max_scroll < 0) max_scroll = 0;
            ed->ui.inspector_scroll -= event->wheel.y * THEME_ROW_HEIGHT;
            if (ed->ui.inspector_scroll < 0) ed->ui.inspector_scroll = 0;
            if (ed->ui.inspector_scroll > max_scroll)
                ed->ui.inspector_scroll = max_scroll;
        } else {
            /* Other panels: pass to Clay scroll containers. */
            ed->ui.scroll_delta_y += (float)event->wheel.y * 40.0f;
        }
        return true;
    }

    case SDL_KEYDOWN:
        return handle_key_down(ed, &event->key);

    case SDL_KEYUP:
        key_mark_released(&ed->ui, (uint32_t)event->key.keysym.sym);
        return false;

    case SDL_TEXTINPUT:
        return handle_text_input(ed, event->text.text);

    default:
        break;
    }

    return false;
}
