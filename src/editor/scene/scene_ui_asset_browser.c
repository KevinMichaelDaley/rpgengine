/**
 * @file scene_ui_asset_browser.c
 * @brief Asset browser panel Clay UI layout.
 *
 * Builds the asset browser panel as a scrollable tree with
 * collapsible sections. Entries are clickable to spawn entities
 * or load assets.
 *
 * Non-static functions (1 / 4 limit):
 *   scene_ui_build_asset_browser
 */

#include "ferrum/editor/scene/scene_ui.h"
#include "ferrum/editor/scene/scene_main.h"
#include "ferrum/editor/scene/scene_panel.h"
#include "ferrum/editor/panels/asset_browser.h"
#include "ferrum/editor/panels/asset_ref_widget.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/editor/scene/scene_asset_load.h"
#include "ferrum/entity/entity_attrs.h"
#include "ferrum/editor/ui/clay_theme.h"
#include "ferrum/editor/ui/clay_fonts.h"
#include "clay.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Constants                                                                 */
/* ------------------------------------------------------------------------ */

/** Panel background color. */
static const Clay_Color COLOR_PANEL_BG = {
    THEME_BG_PANEL_R, THEME_BG_PANEL_G,
    THEME_BG_PANEL_B, THEME_BG_PANEL_A
};

/** Title bar background (unfocused). */
static const Clay_Color COLOR_TITLE_BG = {
    THEME_ACCENT_R, THEME_ACCENT_G, THEME_ACCENT_B, 60
};

/** Section header background. */
static const Clay_Color COLOR_SECTION_BG = {40, 42, 48, 255};

/** Section header when collapsed. */
static const Clay_Color COLOR_SECTION_COLLAPSED = {35, 37, 42, 255};

/** Spawn action row hover highlight. */
static const Clay_Color COLOR_ROW_BG = {0, 0, 0, 0};

/** Selected asset highlight. */
static const Clay_Color COLOR_SELECTION_BG = {
    THEME_ACCENT_R, THEME_ACCENT_G, THEME_ACCENT_B, 60
};

/** Section expand/collapse indicators. */
static const char *INDICATOR_EXPANDED  = "v ";
static const char *INDICATOR_COLLAPSED = "> ";

/* ------------------------------------------------------------------------ */
/* Click context                                                             */
/* ------------------------------------------------------------------------ */

static struct {
    scene_editor_t *ed;
    uint16_t        section_id;
    uint8_t         entry_type;
    uint8_t         asset_type; /**< edit_asset_type_t for asset files. */
    char            command[ASSET_BROWSER_PATH_MAX];
} s_click_ctx[ASSET_BROWSER_MAX_VISIBLE];

/* ------------------------------------------------------------------------ */
/* Hover callbacks                                                           */
/* ------------------------------------------------------------------------ */

/**
 * @brief Hover callback for an asset browser entry click.
 *
 * Section headers toggle collapse. Spawn actions queue a spawn command.
 * Asset files queue a "spawn mesh <path>" command.
 */
static void on_entry_hover(Clay_ElementId id, Clay_PointerData data,
                            void *user) {
    (void)id;
    struct {
        scene_editor_t *ed;
        uint16_t        section_id;
        uint8_t         entry_type;
        uint8_t         asset_type;
        char            command[ASSET_BROWSER_PATH_MAX];
    } *ctx = user;

    if (data.state != CLAY_POINTER_DATA_PRESSED_THIS_FRAME) return;

    if (ctx->entry_type == ASSET_ENTRY_SECTION_HEADER) {
        /* Toggle section collapse. */
        ctx->ed->ui.action = UI_ACTION_ASSET_TOGGLE_SECTION;
        ctx->ed->ui.asset_browser_toggle_target = ctx->section_id;
        return;
    }

    if (ctx->entry_type == ASSET_ENTRY_SPAWN_ACTION ||
        ctx->entry_type == ASSET_ENTRY_ASSET_FILE) {
        /* Track the selected asset for skeleton mode (K key) etc. */
        if (ctx->entry_type == ASSET_ENTRY_ASSET_FILE) {
            strncpy(ctx->ed->ui.selected_asset_path, ctx->command,
                    sizeof(ctx->ed->ui.selected_asset_path) - 1);
            ctx->ed->ui.selected_asset_path[
                sizeof(ctx->ed->ui.selected_asset_path) - 1] = '\0';
            ctx->ed->ui.selected_asset_type = ctx->asset_type;
        }

        /* Intercept: if an asset_ref_widget has focus and the asset type
         * matches its filter, redirect the click to the widget. */
        if (ctx->ed->ui.active_asset_ref &&
            ctx->entry_type == ASSET_ENTRY_ASSET_FILE) {
            asset_ref_state_t *aref = ctx->ed->ui.active_asset_ref;
            if (aref->filter_type == 0 ||
                aref->filter_type == ctx->asset_type) {
                asset_ref_accept(aref, ctx->command);
                ctx->ed->ui.active_asset_ref = NULL;
                return;
            }
        }

        /* Skeleton mode: clicking a mesh/prefab loads it as ghost preview. */
        if (ctx->ed->skeleton_mode.active &&
            ctx->entry_type == ASSET_ENTRY_ASSET_FILE &&
            (ctx->asset_type == 1 /* MESH */ ||
             ctx->asset_type == 4 /* PREFAB */)) {
            strncpy(ctx->ed->skeleton_mode.preview_path, ctx->command,
                    sizeof(ctx->ed->skeleton_mode.preview_path) - 1);
            ctx->ed->skeleton_mode.preview_path[
                sizeof(ctx->ed->skeleton_mode.preview_path) - 1] = '\0';
            ctx->ed->skeleton_mode.preview_loaded = false;
            char msg[320];
            snprintf(msg, sizeof(msg), "Preview: %s", ctx->command);
            scene_ui_tui_log(&ctx->ed->ui, msg);
            return;
        }

        /* Queue spawn command. */
        if (ctx->entry_type == ASSET_ENTRY_ASSET_FILE) {
            /* Build command based on asset type. */
            switch (ctx->asset_type) {
            case 1: /* EDIT_ASSET_MESH */
                snprintf(ctx->ed->ui.tui_cmd, UI_TUI_INPUT_MAX,
                         "spawn mesh %s", ctx->command);
                break;
            case 7: /* EDIT_ASSET_SKELETON */
                /* If a MESH entity is selected, assign the skeleton
                 * to it directly instead of just loading into registry.
                 * Also send setattr to server for persistence. */
                if (edit_selection_count(&ctx->ed->selection) == 1) {
                    uint32_t sel_id = edit_selection_ids(
                        &ctx->ed->selection)[0];
                    const edit_entity_t *sel_ent =
                        edit_entity_store_get(&ctx->ed->entities, sel_id);
                    if (sel_ent &&
                        sel_ent->type == EDIT_ENTITY_TYPE_MESH) {
                        /* Set skel_path attr locally for immediate effect. */
                        edit_entity_t *mut_ent =
                            edit_entity_store_get_mut(
                                &ctx->ed->entities, sel_id);
                        if (mut_ent) {
                            entity_attrs_set(&mut_ent->attrs,
                                SCRIPT_KEY_SKEL_PATH, SCRIPT_ATTR_STR,
                                ctx->command,
                                (uint8_t)(strlen(ctx->command) + 1));
                            scene_load_entity_skeleton(
                                ctx->ed, sel_id, ctx->command);
                        }
                        /* Send setattr to server for persistence. */
                        snprintf(ctx->ed->ui.tui_cmd, UI_TUI_INPUT_MAX,
                                 "setattr %u %u %s",
                                 sel_id, SCRIPT_KEY_SKEL_PATH,
                                 ctx->command);
                        break;
                    }
                }
                /* No mesh selected — spawn an armature entity. */
                snprintf(ctx->ed->ui.tui_cmd, UI_TUI_INPUT_MAX,
                         "spawn armature 0 0 0 %s", ctx->command);
                break;
            case 4: /* EDIT_ASSET_PREFAB */
                /* Store path for frame dispatch to handle loading. */
                strncpy(ctx->ed->ui.tui_cmd, ctx->command,
                        UI_TUI_INPUT_MAX - 1);
                ctx->ed->ui.tui_cmd[UI_TUI_INPUT_MAX - 1] = '\0';
                ctx->ed->ui.action = UI_ACTION_LOAD_PREFAB;
                return;
            default:
                /* Materials, textures, scripts — list only,
                 * no load action implemented yet. */
                return;
            }
        } else {
            strncpy(ctx->ed->ui.tui_cmd, ctx->command,
                    UI_TUI_INPUT_MAX - 1);
            ctx->ed->ui.tui_cmd[UI_TUI_INPUT_MAX - 1] = '\0';
        }
        ctx->ed->ui.action = UI_ACTION_ASSET_SPAWN;
    }
}

/* ------------------------------------------------------------------------ */
/* Public API                                                                */
/* ------------------------------------------------------------------------ */

void scene_ui_build_asset_browser(struct scene_editor *ed,
                                    const struct panel_rect *rect) {
    if (!ed || !rect || rect->w <= 0 || rect->h <= 0) return;

    float panel_w = (float)rect->w;
    float panel_h = (float)rect->h;
    asset_browser_t *browser = &ed->asset_browser;

    /* Root floating container. */
    CLAY(CLAY_ID("AssetBrowser_Root"), {
        .layout = {
            .sizing = {CLAY_SIZING_FIXED(panel_w),
                       CLAY_SIZING_FIXED(panel_h)},
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .padding = {THEME_PADDING_SMALL, THEME_PADDING_SMALL,
                        THEME_PADDING_SMALL, THEME_PADDING_SMALL},
        },
        .backgroundColor = COLOR_PANEL_BG,
        .clip = {.horizontal = true, .vertical = true},
        .floating = {
            .attachTo = CLAY_ATTACH_TO_ROOT,
            .offset = {(float)rect->x, (float)rect->y},
        },
    }) {
        /* ---- Title bar ---- */
        CLAY(CLAY_ID("AssetBrowser_Title"), {
            .layout = {
                .sizing = {CLAY_SIZING_GROW(0),
                           CLAY_SIZING_FIXED(THEME_ROW_HEIGHT)},
                .padding = {THEME_PADDING_SMALL, THEME_PADDING_SMALL, 0, 0},
            },
            .backgroundColor = COLOR_TITLE_BG,
        }) {
            CLAY_TEXT(CLAY_STRING("Assets"),
                CLAY_TEXT_CONFIG({
                    .fontSize = THEME_FONT_SIZE_UI,
                    .textColor = {THEME_TEXT_R, THEME_TEXT_G,
                                  THEME_TEXT_B, THEME_TEXT_A},
                    .fontId = CLAY_FONT_UI,
                }));
        }

        /* ---- Entry list area ---- */
        float list_h = panel_h
                       - (float)THEME_ROW_HEIGHT
                       - (float)(THEME_PADDING_SMALL * 2);
        int vis_lines = (int)(list_h / (float)(THEME_ROW_HEIGHT + 1));
        if (vis_lines < 1) vis_lines = 1;

        uint32_t total_visible = asset_browser_visible_count(browser);
        ed->ui.asset_browser_total = (int)total_visible;
        ed->ui.asset_browser_visible_lines = vis_lines;

        /* Clamp scroll. */
        int max_scroll = (int)total_visible - vis_lines;
        if (max_scroll < 0) max_scroll = 0;
        if (ed->ui.asset_browser_scroll > max_scroll)
            ed->ui.asset_browser_scroll = max_scroll;
        if (ed->ui.asset_browser_scroll < 0)
            ed->ui.asset_browser_scroll = 0;
        int skip = ed->ui.asset_browser_scroll;
        bool needs_scrollbar = (int)total_visible > vis_lines
                               && panel_w > 40.0f;

        CLAY(CLAY_ID("AssetBrowser_ListArea"), {
            .layout = {
                .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                .layoutDirection = CLAY_LEFT_TO_RIGHT,
            },
        }) {
            /* ---- Entry rows ---- */
            CLAY(CLAY_ID("AssetBrowser_EntryList"), {
                .layout = {
                    .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                    .layoutDirection = CLAY_TOP_TO_BOTTOM,
                    .childGap = 1,
                },
            }) {
                uint32_t rendered = 0;

                for (uint32_t raw_vi = (uint32_t)skip;
                     raw_vi < total_visible &&
                     rendered < ASSET_BROWSER_MAX_VISIBLE &&
                     rendered < (uint32_t)vis_lines;
                     raw_vi++) {

                    const asset_browser_entry_t *entry =
                        asset_browser_get_visible(browser, raw_vi);
                    if (!entry) break;

                    /* Set up click context. */
                    s_click_ctx[rendered].ed = ed;
                    s_click_ctx[rendered].section_id = entry->section_id;
                    s_click_ctx[rendered].entry_type = entry->type;
                    s_click_ctx[rendered].asset_type = entry->asset_type;
                    strncpy(s_click_ctx[rendered].command, entry->path,
                            ASSET_BROWSER_PATH_MAX - 1);
                    s_click_ctx[rendered].command[ASSET_BROWSER_PATH_MAX - 1] = '\0';

                    /* Row styling based on type. */
                    Clay_Color row_bg;
                    Clay_Color text_color;
                    uint16_t left_pad = (uint16_t)(THEME_PADDING_SMALL
                                       + entry->depth * THEME_INDENT);

                    if (entry->type == ASSET_ENTRY_SECTION_HEADER) {
                        bool collapsed = asset_browser_is_collapsed(
                            browser, entry->section_id);
                        row_bg = collapsed ? COLOR_SECTION_COLLAPSED
                                           : COLOR_SECTION_BG;
                        text_color = (Clay_Color){
                            THEME_TEXT_R, THEME_TEXT_G,
                            THEME_TEXT_B, THEME_TEXT_A};
                    } else {
                        /* Highlight selected asset. */
                        bool is_selected = (ed->ui.selected_asset_path[0] != '\0' &&
                            entry->type == ASSET_ENTRY_ASSET_FILE &&
                            strcmp(entry->path, ed->ui.selected_asset_path) == 0);
                        row_bg = is_selected ? COLOR_SELECTION_BG : COLOR_ROW_BG;
                        text_color = (Clay_Color){
                            THEME_TEXT_R, THEME_TEXT_G,
                            THEME_TEXT_B, THEME_TEXT_A};
                    }

                    CLAY(CLAY_IDI("AssetBrowser_Row", rendered), {
                        .layout = {
                            .sizing = {CLAY_SIZING_GROW(0),
                                       CLAY_SIZING_FIXED(THEME_ROW_HEIGHT)},
                            .layoutDirection = CLAY_LEFT_TO_RIGHT,
                            .padding = {left_pad, THEME_PADDING_SMALL, 0, 0},
                            .childGap = 0,
                            .childAlignment = {
                                .y = CLAY_ALIGN_Y_CENTER,
                            },
                        },
                        .backgroundColor = row_bg,
                    }) {
                        Clay_OnHover(on_entry_hover,
                                     &s_click_ctx[rendered]);

                        /* Section headers get expand/collapse indicator. */
                        if (entry->type == ASSET_ENTRY_SECTION_HEADER) {
                            bool collapsed = asset_browser_is_collapsed(
                                browser, entry->section_id);
                            const char *indicator = collapsed
                                ? INDICATOR_COLLAPSED : INDICATOR_EXPANDED;
                            Clay_String ind_str = {
                                .length = (int32_t)strlen(indicator),
                                .chars = indicator,
                            };
                            Clay__OpenTextElement(ind_str,
                                CLAY_TEXT_CONFIG({
                                    .fontSize = THEME_FONT_SIZE_UI,
                                    .textColor = text_color,
                                    .fontId = CLAY_FONT_UI,
                                }));
                        }

                        /* Entry name. */
                        Clay_String name_str = {
                            .length = (int32_t)strlen(entry->name),
                            .chars = entry->name,
                        };
                        Clay__OpenTextElement(name_str,
                            CLAY_TEXT_CONFIG({
                                .fontSize = THEME_FONT_SIZE_UI,
                                .textColor = text_color,
                                .fontId = CLAY_FONT_UI,
                            }));
                    }

                    rendered++;
                }
            }

            /* ---- Scrollbar ---- */
            if (needs_scrollbar) {
                float track_h = list_h;
                float thumb_ratio = (float)vis_lines / (float)total_visible;
                if (thumb_ratio > 1.0f) thumb_ratio = 1.0f;
                float thumb_h = track_h * thumb_ratio;
                if (thumb_h < 12.0f) thumb_h = 12.0f;

                float scroll_range = track_h - thumb_h;
                float thumb_offset = 0.0f;
                if (max_scroll > 0) {
                    thumb_offset = scroll_range
                                   * ((float)ed->ui.asset_browser_scroll
                                      / (float)max_scroll);
                }

                CLAY(CLAY_ID("AssetBrowser_ScrollTrack"), {
                    .layout = {
                        .sizing = {CLAY_SIZING_FIXED(8),
                                   CLAY_SIZING_GROW(0)},
                        .layoutDirection = CLAY_TOP_TO_BOTTOM,
                        .padding = {0, 0, (uint16_t)thumb_offset, 0},
                    },
                    .backgroundColor = {25, 27, 33, 255},
                }) {
                    CLAY(CLAY_ID("AssetBrowser_ScrollThumb"), {
                        .layout = {
                            .sizing = {CLAY_SIZING_FIXED(8),
                                       CLAY_SIZING_FIXED(thumb_h)},
                        },
                        .backgroundColor = {80, 85, 95, 255},
                        .cornerRadius = CLAY_CORNER_RADIUS(4),
                    }) {}
                }
            }
        }
    }
}
