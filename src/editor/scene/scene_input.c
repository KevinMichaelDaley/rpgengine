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

#include <SDL2/SDL.h>
#include <string.h>

/* ---- Internal helpers ---- */

/**
 * @brief Handle mouse button down: start divider drag, change focus,
 *        and update Clay mouse state.
 */
static bool handle_mouse_down(scene_editor_t *ed, const SDL_MouseButtonEvent *ev) {
    if (ev->button == SDL_BUTTON_LEFT) {
        ed->ui.mouse_down = true;
        ed->ui.mouse_clicked = true;

        /* Check for divider drag start */
        divider_id_t div = panel_layout_divider_hit_test(&ed->layout,
                                                          ev->x, ev->y);
        if (div != DIVIDER_NONE) {
            ed->dragging_divider = div;
            return true;
        }

        /* Click-to-focus */
        panel_id_t hit = panel_layout_hit_test(&ed->layout, ev->x, ev->y);
        panel_layout_set_focus(&ed->layout, hit);
    }
    return false; /* let Clay also handle the click */
}

/**
 * @brief Handle mouse button up: stop divider drag, update Clay state.
 */
static bool handle_mouse_up(scene_editor_t *ed, const SDL_MouseButtonEvent *ev) {
    if (ev->button == SDL_BUTTON_LEFT) {
        ed->ui.mouse_down = false;
        if (ed->dragging_divider != DIVIDER_NONE) {
            ed->dragging_divider = DIVIDER_NONE;
            return true;
        }
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

    if (ed->dragging_divider == DIVIDER_NONE) return false;

    int delta = (ed->dragging_divider == DIVIDER_BOTTOM) ? ev->yrel : ev->xrel;
    panel_layout_drag_divider(&ed->layout, ed->dragging_divider, delta);
    return true;
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
            /* Log the command as "> input". */
            char log_line[UI_TUI_LOG_LINE];
            snprintf(log_line, sizeof(log_line), "> %s", ui->tui_input);
            scene_ui_tui_log(ui, log_line);

            /* Copy command to tui_cmd before clearing input. */
            memcpy(ui->tui_cmd, ui->tui_input, (size_t)(ui->tui_input_len + 1));
            ui->action = UI_ACTION_TUI_COMMAND;

            /* Clear input for next command. */
            ui->tui_input[0] = '\0';
            ui->tui_input_len = 0;
            ui->tui_cursor = 0;
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
        panel_layout_focus_next(&ed->layout);
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

    /* Entity operations */
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

    case SDLK_a:
        break;

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
            panel_layout_resize(&ed->layout,
                                event->window.data1, event->window.data2);
            return true;
        }
        break;

    case SDL_MOUSEBUTTONDOWN:
        return handle_mouse_down(ed, &event->button);

    case SDL_MOUSEBUTTONUP:
        return handle_mouse_up(ed, &event->button);

    case SDL_MOUSEMOTION:
        return handle_mouse_motion(ed, &event->motion);

    case SDL_KEYDOWN:
        return handle_key_down(ed, &event->key);

    case SDL_TEXTINPUT:
        return handle_text_input(ed, event->text.text);

    default:
        break;
    }

    return false;
}
