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
#include "ferrum/editor/viewport/viewport_camera.h"
#include "ferrum/editor/viewport/viewport_gizmo.h"
#include "ferrum/editor/viewport/selection_raycast.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/editor/ctrl_cmd_defs.h"
#include "ferrum/editor/ui/clay_theme.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <string.h>

/* Camera orbit/pan sensitivity. */
#define CAMERA_ORBIT_SPEED 0.005f
#define CAMERA_PAN_SPEED   0.02f
#define CAMERA_ZOOM_SPEED  1.0f

/** Gizmo drag sensitivity: world units per pixel of mouse motion. */
#define GIZMO_DRAG_SPEED 0.01f

/** Rotation drag sensitivity: degrees per pixel of mouse motion. */
#define GIZMO_ROTATE_SPEED 0.5f

/* ---- Internal helpers ---- */

/**
 * @brief Compute gizmo visual scale from camera distance.
 */
static float viewport_gizmo_screen_scale(const vec3_t *gizmo_pos,
                                           const vec3_t *eye_pos) {
    float dx = gizmo_pos->x - eye_pos->x;
    float dy = gizmo_pos->y - eye_pos->y;
    float dz = gizmo_pos->z - eye_pos->z;
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    return dist * 0.15f;
}

/**
 * @brief Apply gizmo drag delta to all selected entities (optimistic).
 *
 * For translate: moves entities by delta.
 * For scale: scales entities by (1 + delta component).
 * For rotate: rotates entities by delta (degrees).
 */
static void apply_gizmo_drag(scene_editor_t *ed, vec3_t delta) {
    for (uint32_t i = 0; i < ed->entities.capacity; ++i) {
        if (!edit_selection_contains(&ed->selection, i)) continue;
        edit_entity_t *ent = edit_entity_store_get_mut(
            &ed->entities, i);
        if (!ent) continue;

        switch (ed->gizmo.mode) {
        case GIZMO_MODE_TRANSLATE:
            ent->pos[0] += delta.x;
            ent->pos[1] += delta.y;
            ent->pos[2] += delta.z;
            break;
        case GIZMO_MODE_ROTATE:
            ent->rot[0] += delta.x;
            ent->rot[1] += delta.y;
            ent->rot[2] += delta.z;
            break;
        case GIZMO_MODE_SCALE:
            ent->scale[0] *= (1.0f + delta.x);
            ent->scale[1] *= (1.0f + delta.y);
            ent->scale[2] *= (1.0f + delta.z);
            break;
        }
    }
}

/**
 * @brief Send a single server command for the accumulated gizmo drag.
 *
 * The server applies the transform to all currently selected entities.
 */
static void send_gizmo_commands(scene_editor_t *ed, vec3_t total_delta) {
    char cmd_buf[256];
    int cmd_len = 0;
    uint32_t cmd_id = scene_connection_next_id(&ed->connection);

    switch (ed->gizmo.mode) {
    case GIZMO_MODE_TRANSLATE: {
        float delta_arr[3] = {total_delta.x, total_delta.y, total_delta.z};
        cmd_len = scene_cmd_format_move(cmd_buf, sizeof(cmd_buf),
                                         cmd_id, delta_arr);
        break;
    }
    case GIZMO_MODE_ROTATE: {
        float delta_arr[3] = {total_delta.x, total_delta.y, total_delta.z};
        cmd_len = scene_cmd_format_rotate(cmd_buf, sizeof(cmd_buf),
                                           cmd_id, delta_arr);
        break;
    }
    case GIZMO_MODE_SCALE: {
        float factor[3] = {1.0f + total_delta.x,
                            1.0f + total_delta.y,
                            1.0f + total_delta.z};
        cmd_len = scene_cmd_format_scale(cmd_buf, sizeof(cmd_buf),
                                          cmd_id, factor);
        break;
    }
    }

    if (cmd_len > 0) {
        scene_connection_send_cmd(&ed->connection, cmd_buf);
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

        /* Viewport left-click: gizmo interaction or entity selection. */
        if (hit == PANEL_VIEWPORT) {
            panel_rect_t vp_rect = panel_layout_get_rect(&ed->layout,
                                                           PANEL_VIEWPORT);
            float nx = (float)(lx - vp_rect.x) / (float)vp_rect.w;
            float ny = (float)(ly - vp_rect.y) / (float)vp_rect.h;

            vec2_t screen_pos = {nx, ny};
            vec2_t vp_size = {(float)vp_rect.w, (float)vp_rect.h};
            editor_ray_t ray;
            if (editor_camera_screen_to_ray(&ed->viewport.camera,
                                              screen_pos, vp_size, &ray) == 0) {
                /* Test gizmo hit first (if we have a selection). */
                bool gizmo_hit = false;
                if (edit_selection_count(&ed->selection) > 0) {
                    vec3_t eye = editor_camera_eye_position(
                        &ed->viewport.camera);
                    float gscale = viewport_gizmo_screen_scale(
                        &ed->gizmo.position, &eye);
                    gizmo_axis_t axis = gizmo_hit_test(
                        &ed->gizmo, &ray, gscale);
                    if (axis != GIZMO_AXIS_NONE) {
                        ed->gizmo.active_axis = axis;
                        ed->gizmo.dragging = true;
                        ed->gizmo_drag_origin = ed->gizmo.position;
                        ed->gizmo_drag_accum = (vec3_t){0, 0, 0};
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
                            if (edit_selection_contains(&ed->selection,
                                                        picked_id)) {
                                ed->ui.action = UI_ACTION_DESELECT_ENTITY;
                            } else {
                                ed->ui.action = UI_ACTION_SELECT_ENTITY;
                            }
                        } else {
                            edit_selection_clear(&ed->selection);
                            ed->ui.action = UI_ACTION_SELECT_ENTITY;
                        }
                        ed->ui.action_target = picked_id;
                    }
                    /* Clicking empty space does nothing — selection is
                     * only cleared by selecting a new entity without shift. */
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
 * @brief Handle mouse button up: stop divider drag, update Clay state.
 */
static bool handle_mouse_up(scene_editor_t *ed, const SDL_MouseButtonEvent *ev) {
    if (ev->button == SDL_BUTTON_LEFT) {
        ed->ui.mouse_down = false;

        /* End gizmo drag: send server commands for total delta. */
        if (ed->gizmo.dragging) {
            send_gizmo_commands(ed, ed->gizmo_drag_accum);
            ed->gizmo.dragging = false;
            ed->gizmo.active_axis = GIZMO_AXIS_NONE;
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

    /* Gizmo drag: convert pixel delta to constrained world delta. */
    if (ed->gizmo.dragging) {
        vec3_t delta = {0, 0, 0};

        if (ed->gizmo.mode == GIZMO_MODE_ROTATE) {
            /* Project the rotation axis onto screen space to determine
             * which mouse direction maps to positive rotation.
             * This keeps rotation intuitive regardless of camera angle. */
            mat4_t view;
            editor_camera_view_matrix(&ed->viewport.camera, &view);

            /* Get the world-space axis direction. */
            vec3_t axis_dir = {0, 0, 0};
            switch (ed->gizmo.active_axis) {
            case GIZMO_AXIS_X: axis_dir.x = 1.0f; break;
            case GIZMO_AXIS_Y: axis_dir.y = 1.0f; break;
            case GIZMO_AXIS_Z: axis_dir.z = 1.0f; break;
            default: break;
            }

            /* Transform axis to view space (rotation only, no translation).
             * The upper-left 3x3 of the view matrix rotates world→view. */
            float sx = view.m[0] * axis_dir.x + view.m[4] * axis_dir.y
                      + view.m[8] * axis_dir.z;
            float sy = view.m[1] * axis_dir.x + view.m[5] * axis_dir.y
                      + view.m[9] * axis_dir.z;

            /* Screen-space perpendicular of the projected axis:
             * rotating around a screen vector (sx, sy) means mouse
             * motion along (-sy, sx) produces positive rotation. */
            float perp_x = -sy;
            float perp_y =  sx;
            float perp_len = sqrtf(perp_x * perp_x + perp_y * perp_y);
            if (perp_len > 1e-6f) {
                perp_x /= perp_len;
                perp_y /= perp_len;
            }

            /* Dot mouse delta with the perpendicular direction. */
            float mouse_proj = (float)ev->xrel * perp_x
                              + (float)ev->yrel * perp_y;
            float rot_amount = mouse_proj * GIZMO_ROTATE_SPEED;

            switch (ed->gizmo.active_axis) {
            case GIZMO_AXIS_X: delta.x = rot_amount; break;
            case GIZMO_AXIS_Y: delta.y = rot_amount; break;
            case GIZMO_AXIS_Z: delta.z = rot_amount; break;
            default: break;
            }
        } else {
            vec3_t eye = editor_camera_eye_position(&ed->viewport.camera);
            float cam_dist = viewport_gizmo_screen_scale(
                &ed->gizmo.position, &eye);
            float speed = cam_dist * GIZMO_DRAG_SPEED;

            switch (ed->gizmo.active_axis) {
            case GIZMO_AXIS_X:
                delta.x = (float)ev->xrel * speed;
                break;
            case GIZMO_AXIS_Y:
                delta.y = -(float)ev->yrel * speed;
                break;
            case GIZMO_AXIS_Z:
                delta.z = (float)ev->xrel * speed;
                break;
            default:
                break;
            }
        }

        apply_gizmo_drag(ed, delta);
        ed->gizmo_drag_accum.x += delta.x;
        ed->gizmo_drag_accum.y += delta.y;
        ed->gizmo_drag_accum.z += delta.z;
        return true;
    }

    /* Right mouse or middle mouse drag: orbit or pan the viewport camera. */
    if (ed->ui.right_mouse_down || ed->ui.middle_mouse_down) {
        SDL_Keymod mod = SDL_GetModState();
        if (mod & KMOD_SHIFT) {
            /* Shift+drag: pan. */
            editor_camera_pan(&ed->viewport.camera,
                               -(float)ev->xrel * CAMERA_PAN_SPEED,
                               (float)ev->yrel * CAMERA_PAN_SPEED);
        } else {
            /* Drag: orbit. */
            editor_camera_orbit(&ed->viewport.camera,
                                 -(float)ev->xrel * CAMERA_ORBIT_SPEED,
                                 -(float)ev->yrel * CAMERA_ORBIT_SPEED);
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
static bool handle_key_down(scene_editor_t *ed, const SDL_KeyboardEvent *ev) {
    SDL_Keycode key = ev->keysym.sym;

    /* If TUI is active, route keys to TUI input handler first. */
    if (ed->ui.tui_active) {
        return handle_tui_key(ed, ev);
    }

    /* Gizmo nudge: arrow keys apply exact rotation steps when an axis
     * is active (either during drag or after selecting an axis). */
    if (ed->gizmo.active_axis != GIZMO_AXIS_NONE &&
        ed->gizmo.mode == GIZMO_MODE_ROTATE) {
        float step_deg = 0.0f;
        if (key == SDLK_UP)   step_deg =  45.0f;
        if (key == SDLK_DOWN) step_deg = -45.0f;
        if (step_deg != 0.0f) {
            vec3_t delta = {0, 0, 0};
            switch (ed->gizmo.active_axis) {
            case GIZMO_AXIS_X: delta.x = step_deg; break;
            case GIZMO_AXIS_Y: delta.y = step_deg; break;
            case GIZMO_AXIS_Z: delta.z = step_deg; break;
            default: break;
            }
            apply_gizmo_drag(ed, delta);
            ed->gizmo_drag_accum.x += delta.x;
            ed->gizmo_drag_accum.y += delta.y;
            ed->gizmo_drag_accum.z += delta.z;
            /* If not mid-drag, send command immediately. */
            if (!ed->gizmo.dragging) {
                send_gizmo_commands(ed, delta);
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
    case SDLK_ESCAPE:
        ed->ui.tui_active = false;
        panel_layout_focus_viewport(&ed->layout);
        return true;

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

    /* Entity operations — ignore key repeat (held key). */
    case SDLK_DELETE:
    case SDLK_x:
        if (ev->repeat) break;
        if (ev->keysym.mod & KMOD_SHIFT) {
            ed->ui.action = UI_ACTION_DELETE_SELECTED;
            return true;
        }
        if (key == SDLK_DELETE) {
            ed->ui.action = UI_ACTION_DELETE_SELECTED;
            return true;
        }
        break;

    /* Transform mode shortcuts */
    case SDLK_g:
        ed->ui.action = UI_ACTION_MODE_TRANSLATE;
        return true;
    case SDLK_r:
        ed->ui.action = UI_ACTION_MODE_ROTATE;
        return true;
    case SDLK_s:
        ed->ui.action = UI_ACTION_MODE_SCALE;
        return true;

    /* Keyboard zoom: +/- (also = for unshifted plus). */
    case SDLK_PLUS:
    case SDLK_EQUALS:
        editor_camera_zoom(&ed->viewport.camera, -CAMERA_ZOOM_SPEED);
        return true;
    case SDLK_MINUS:
        editor_camera_zoom(&ed->viewport.camera, CAMERA_ZOOM_SPEED);
        return true;

    case SDLK_a:
        break;

    /* Snap view shortcuts (number row keys). */
    case SDLK_1:
        if (ev->keysym.mod & KMOD_CTRL) {
            editor_camera_snap_view(&ed->viewport.camera, EDITOR_VIEW_BACK);
        } else {
            editor_camera_snap_view(&ed->viewport.camera, EDITOR_VIEW_FRONT);
        }
        return true;
    case SDLK_3:
        if (ev->keysym.mod & KMOD_CTRL) {
            editor_camera_snap_view(&ed->viewport.camera, EDITOR_VIEW_LEFT);
        } else {
            editor_camera_snap_view(&ed->viewport.camera, EDITOR_VIEW_RIGHT);
        }
        return true;
    case SDLK_7:
        if (ev->keysym.mod & KMOD_CTRL) {
            editor_camera_snap_view(&ed->viewport.camera, EDITOR_VIEW_BOTTOM);
        } else {
            editor_camera_snap_view(&ed->viewport.camera, EDITOR_VIEW_TOP);
        }
        return true;
    case SDLK_5:
        editor_camera_toggle_projection(&ed->viewport.camera);
        return true;
    case SDLK_f:
        /* Frame selection: if entities selected, frame their AABB. */
        if (edit_selection_count(&ed->selection) > 0) {
            /* TODO: compute selection AABB and call
             * editor_camera_frame_selection(). For now, reset camera. */
            editor_camera_reset(&ed->viewport.camera);
        }
        return true;

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
            editor_camera_zoom(&ed->viewport.camera,
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

    case SDL_TEXTINPUT:
        return handle_text_input(ed, event->text.text);

    default:
        break;
    }

    return false;
}
