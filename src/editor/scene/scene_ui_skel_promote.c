/**
 * @file scene_ui_skel_promote.c
 * @brief Inspector skeleton promotion section for MESH entities.
 *
 * Shows a gear button in the inspector for MESH entities. When expanded,
 * displays an asset_ref_widget for skeleton assignment. On confirm,
 * sets SCRIPT_KEY_SKEL_PATH and triggers skeleton loading.
 *
 * Non-static functions (1 / 4-function rule):
 *   1. scene_ui_build_skel_promote
 */

#include "ferrum/editor/scene/scene_ui_skel_promote.h"
#include "ferrum/editor/scene/scene_main.h"
#include "ferrum/editor/scene/scene_asset_load.h"
#include "ferrum/editor/panels/asset_ref_widget.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/entity/entity_attrs.h"
#include "ferrum/editor/ui/clay_theme.h"
#include "ferrum/editor/ui/clay_fonts.h"
#include "clay.h"

#include <string.h>
#include <stdio.h>

/** @brief Module-local widget state. */
static asset_ref_state_t s_skel_ref;

/** @brief Whether the skeleton section is expanded. */
static bool s_skel_expanded = false;

/** @brief Whether the widget has been initialized. */
static bool s_skel_inited = false;

/** @brief Entity ID the widget was last shown for. */
static uint32_t s_skel_entity = UINT32_MAX;

/** @brief Display buffers for Clay text. */
static char s_skel_path_buf[64];
static int32_t s_skel_path_len;

/* EDIT_ASSET_SKELETON type constant. */
#define EDIT_ASSET_SKELETON 7

/** Check if a row at y with height h is visible. */
static bool is_row_visible(float y, float h, float scroll_px, float visible_h) {
    return (y + h) > scroll_px && y < (scroll_px + visible_h);
}

/** Gear button hover callback context. */
static void on_gear_hover(Clay_ElementId id, Clay_PointerData pointer,
                           void *user_data) {
    (void)id; (void)user_data;
    if (pointer.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
        s_skel_expanded = !s_skel_expanded;
    }
}

/** Confirm button hover callback context. */
static void on_confirm_hover(Clay_ElementId id, Clay_PointerData pointer,
                               void *user_data) {
    (void)id; (void)user_data;
    if (pointer.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
        asset_ref_confirm(&s_skel_ref);
    }
}

/** Focus the widget for asset browser clicks. */
static void on_path_hover(Clay_ElementId id, Clay_PointerData pointer,
                            void *user_data) {
    (void)id;
    struct scene_editor *ed = (struct scene_editor *)user_data;
    if (pointer.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME && ed) {
        s_skel_ref.focused = true;
        ed->ui.active_asset_ref = &s_skel_ref;
    }
}

float scene_ui_build_skel_promote(struct scene_editor *ed,
                                    uint32_t entity_id,
                                    float y_cursor,
                                    int scroll_px,
                                    float visible_h,
                                    int clay_idx) {
    if (!ed) return 0.0f;

    /* Only show for MESH entities. */
    const edit_entity_t *ent = edit_entity_store_get(&ed->entities, entity_id);
    if (!ent || ent->type != EDIT_ENTITY_TYPE_MESH) return 0.0f;

    float row_h = (float)THEME_ROW_HEIGHT;
    float consumed = 0.0f;
    float y = y_cursor;

    /* Reset widget if entity changed. */
    if (s_skel_entity != entity_id || !s_skel_inited) {
        asset_ref_init(&s_skel_ref, EDIT_ASSET_SKELETON);
        s_skel_inited = true;
        s_skel_entity = entity_id;
        s_skel_expanded = false;

        /* Pre-populate from existing skel_path attr. */
        uint8_t at = 0, as = 0;
        const void *data = entity_attrs_get(&ent->attrs,
                                              SCRIPT_KEY_SKEL_PATH, &at, &as);
        if (data && at == SCRIPT_ATTR_STR) {
            asset_ref_set_path(&s_skel_ref, (const char *)data);
        }
    }

    /* ---- Gear button row ---- */
    if (is_row_visible(y, row_h, (float)scroll_px, visible_h)) {
        CLAY(CLAY_IDI("SkelPromGear", clay_idx), {
            .layout = {
                .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(row_h)},
                .padding = {THEME_PADDING, THEME_PADDING, 0, 0},
                .childAlignment = {.x = CLAY_ALIGN_X_RIGHT},
            },
        }) {
            Clay_String gear_label = s_skel_expanded
                ? (Clay_String){.chars = "[-] Skeleton", .length = 12}
                : (Clay_String){.chars = "[+] Skeleton", .length = 12};

            CLAY(CLAY_IDI("SkelGearBtn", clay_idx), {
                .layout = {
                    .sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(row_h)},
                    .padding = {4, 4, 2, 2},
                },
                .backgroundColor = {60, 65, 75, 255},
                .cornerRadius = CLAY_CORNER_RADIUS(3),
            }) {
                Clay_OnHover(on_gear_hover, NULL);
                Clay__OpenTextElement(gear_label, CLAY_TEXT_CONFIG({
                    .textColor = {200, 200, 200, 255},
                    .fontSize = THEME_FONT_SIZE_UI,
                    .fontId = CLAY_FONT_MONO,
                }));
            }
        }
    }
    y += row_h;
    consumed += row_h;
    clay_idx++;

    /* ---- Expanded section ---- */
    if (s_skel_expanded) {
        /* Path display / click-to-focus row. */
        if (is_row_visible(y, row_h, (float)scroll_px, visible_h)) {
            /* Format path for display. */
            int n = snprintf(s_skel_path_buf, sizeof(s_skel_path_buf),
                             "%s", s_skel_ref.display[0] ? s_skel_ref.display
                                                         : "(select asset or object)");
            if (n < 0) n = 0;
            if (n >= (int)sizeof(s_skel_path_buf))
                n = (int)sizeof(s_skel_path_buf) - 1;
            s_skel_path_len = (int32_t)n;

            CLAY(CLAY_IDI("SkelPath", clay_idx), {
                .layout = {
                    .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(row_h)},
                    .padding = {THEME_PADDING, THEME_PADDING, 0, 0},
                    .layoutDirection = CLAY_LEFT_TO_RIGHT,
                    .childGap = 4,
                },
            }) {
                /* Path text (click to focus widget for asset browser). */
                CLAY(CLAY_IDI("SkelPathTxt", clay_idx), {
                    .layout = {
                        .sizing = {CLAY_SIZING_GROW(0),
                                   CLAY_SIZING_FIXED(row_h)},
                        .padding = {4, 4, 2, 2},
                    },
                    .backgroundColor = s_skel_ref.focused
                        ? (Clay_Color){50, 55, 90, 255}
                        : (Clay_Color){40, 42, 50, 255},
                    .cornerRadius = CLAY_CORNER_RADIUS(2),
                }) {
                    Clay_OnHover(on_path_hover, ed);
                    Clay__OpenTextElement(
                        (Clay_String){
                            .chars = s_skel_path_buf,
                            .length = s_skel_path_len,
                        },
                        CLAY_TEXT_CONFIG({
                            .textColor = {180, 180, 220, 255},
                            .fontSize = THEME_FONT_SIZE_UI,
                            .fontId = CLAY_FONT_MONO,
                        }));
                }

                /* Confirm button (filled left arrow). */
                CLAY(CLAY_IDI("SkelConfirm", clay_idx), {
                    .layout = {
                        .sizing = {CLAY_SIZING_FIXED(row_h),
                                   CLAY_SIZING_FIXED(row_h)},
                        .childAlignment = {
                            .x = CLAY_ALIGN_X_CENTER,
                            .y = CLAY_ALIGN_Y_CENTER,
                        },
                    },
                    .backgroundColor = s_skel_ref.confirmed
                        ? (Clay_Color){40, 120, 60, 255}
                        : (Clay_Color){70, 75, 85, 255},
                    .cornerRadius = CLAY_CORNER_RADIUS(3),
                }) {
                    Clay_OnHover(on_confirm_hover, NULL);
                    Clay__OpenTextElement(
                        (Clay_String){.chars = "<", .length = 1},
                        CLAY_TEXT_CONFIG({
                            .textColor = {220, 220, 220, 255},
                            .fontSize = THEME_FONT_SIZE_UI,
                            .fontId = CLAY_FONT_MONO,
                        }));
                }
            }
        }
        y += row_h;
        consumed += row_h;
        clay_idx++;

        /* Handle confirmation: spawn armature (if from asset) or bind
         * existing armature entity. Set ARMATURE_ID on the mesh and
         * copy SKEL_PATH for skinning. */
        if (s_skel_ref.confirmed && s_skel_ref.path[0] != '\0') {
            edit_entity_t *mut_ent =
                edit_entity_store_get_mut(&ed->entities, entity_id);
            if (mut_ent) {
                /* Set SKEL_PATH on the mesh for skinning. */
                entity_attrs_set(&mut_ent->attrs, SCRIPT_KEY_SKEL_PATH,
                                 SCRIPT_ATTR_STR, s_skel_ref.path,
                                 (uint8_t)(strlen(s_skel_ref.path) + 1));
                scene_load_entity_skeleton(ed, entity_id, s_skel_ref.path);

                /* Spawn an armature entity at the mesh's position. */
                viewport_state_t *cvp = scene_focused_vp(ed);
                float cx = mut_ent->pos[0];
                float cy = mut_ent->pos[1];
                float cz = mut_ent->pos[2];
                snprintf(ed->ui.tui_cmd, UI_TUI_INPUT_MAX,
                         "spawn armature %s %.4g %.4g %.4g",
                         s_skel_ref.path,
                         (double)cx, (double)cy, (double)cz);
                ed->ui.action = UI_ACTION_TUI_COMMAND;
                (void)cvp;
            }
            s_skel_ref.confirmed = false;
        }
    }

    return consumed;
}
