/**
 * @file scene_ui_outliner.c
 * @brief Outliner panel Clay UI layout: toolbar + entity list.
 *
 * Builds the outliner panel layout during the Clay layout phase.
 * Contains one public function (scene_ui_build_outliner) and
 * static helpers for hover callbacks and sub-sections.
 */

#include "ferrum/editor/scene/scene_ui.h"
#include "ferrum/editor/scene/scene_main.h"
#include "ferrum/editor/scene/scene_panel.h"
#include "ferrum/editor/ui/clay_theme.h"
#include "ferrum/editor/ui/clay_fonts.h"
#include "clay.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Constants                                                                 */
/* ------------------------------------------------------------------------ */

/** Maximum entities visible in the outliner at once. */
#define OUTLINER_MAX_VISIBLE 128

/**
 * @brief Static fallback name buffers for entities without names.
 *
 * Clay_String only stores a pointer — the backing memory must
 * survive until Clay_EndLayout() / render. Stack locals won't work.
 */
static char s_outliner_names[OUTLINER_MAX_VISIBLE][32];

/** Panel background color. */
static const Clay_Color COLOR_PANEL_BG = {
    THEME_BG_PANEL_R, THEME_BG_PANEL_G,
    THEME_BG_PANEL_B, THEME_BG_PANEL_A
};

/** Title bar background: normal (unfocused). */
static const Clay_Color COLOR_TITLE_BG = {
    THEME_ACCENT_R, THEME_ACCENT_G, THEME_ACCENT_B, 60
};

/** Title bar background: focused (brighter). */
static const Clay_Color COLOR_TITLE_BG_FOCUSED = {
    THEME_ACCENT_R, THEME_ACCENT_G, THEME_ACCENT_B, 140
};

/** Selection highlight background. */
static const Clay_Color COLOR_SELECTION_BG = {
    THEME_SELECTION_R, THEME_SELECTION_G,
    THEME_SELECTION_B, THEME_SELECTION_A
};

/** Default entity row background (transparent). */
static const Clay_Color COLOR_ROW_BG = {0, 0, 0, 0};

/* ------------------------------------------------------------------------ */
/* Per-frame click context for entity rows                                   */
/* ------------------------------------------------------------------------ */

/**
 * @brief Click context for entity row hover callbacks.
 *
 * Clay_OnHover only accepts a single void* userdata. To pass both the
 * editor pointer and the entity ID, we store them in a static array
 * indexed per visible row. This is safe because the layout is built
 * and consumed within a single frame.
 */
static struct {
    scene_editor_t *ed;
    uint32_t        entity_id;
} s_entity_click_ctx[OUTLINER_MAX_VISIBLE];

/* ------------------------------------------------------------------------ */
/* Hover callbacks                                                           */
/* ------------------------------------------------------------------------ */

/**
 * @brief Hover callback for an entity row click with modifier support.
 *
 * Plain click: clear selection, select this entity.
 * Ctrl+click: toggle (add if unselected, deselect if selected).
 * Shift+click: range select from last clicked entity to this one
 *   (noop if last outliner click was a deselect).
 * Ctrl+Shift+click: range deselect from last clicked to this one
 *   (noop if last outliner click was a select).
 */
static void on_entity_row_hover(Clay_ElementId id, Clay_PointerData data,
                                 void *user) {
    (void)id;
    struct {
        scene_editor_t *ed;
        uint32_t        entity_id;
    } *ctx = user;

    if (data.state != CLAY_POINTER_DATA_PRESSED_THIS_FRAME) return;

    SDL_Keymod mod = SDL_GetModState();
    bool ctrl  = (mod & KMOD_CTRL) != 0;
    bool shift = (mod & KMOD_SHIFT) != 0;

    if (ctrl && shift) {
        /* Ctrl+Shift: range deselect (noop if last was select). */
        ctx->ed->ui.action = UI_ACTION_RANGE_DESELECT;
        ctx->ed->ui.action_target = ctx->entity_id;
    } else if (shift) {
        /* Shift: range select (noop if last was deselect). */
        ctx->ed->ui.action = UI_ACTION_RANGE_SELECT;
        ctx->ed->ui.action_target = ctx->entity_id;
    } else if (ctrl) {
        /* Ctrl: toggle individual entity. */
        bool selected = edit_selection_contains(&ctx->ed->selection,
                                                 ctx->entity_id);
        if (selected) {
            ctx->ed->ui.action = UI_ACTION_DESELECT_ENTITY;
        } else {
            ctx->ed->ui.action = UI_ACTION_SELECT_ENTITY;
        }
        ctx->ed->ui.action_target = ctx->entity_id;
    } else {
        /* Plain click: replace selection with this entity. */
        ctx->ed->ui.action = UI_ACTION_REPLACE_SELECTION;
        ctx->ed->ui.action_target = ctx->entity_id;
    }
}

/* ------------------------------------------------------------------------ */
/* Static helper: entity type to short display string                        */
/* ------------------------------------------------------------------------ */

/**
 * @brief Return a short type tag string for display (e.g. "[box]").
 * @param type  Entity type constant.
 * @return Static string; never NULL.
 */
static const char *type_tag_string(uint32_t type) {
    switch (type) {
        case EDIT_ENTITY_TYPE_BOX:       return "[box]";
        case EDIT_ENTITY_TYPE_SPHERE:    return "[sphere]";
        case EDIT_ENTITY_TYPE_CAPSULE:   return "[capsule]";
        case EDIT_ENTITY_TYPE_MARKER:    return "[marker]";
        case EDIT_ENTITY_TYPE_MESH:      return "[mesh]";
        case EDIT_ENTITY_TYPE_HALFSPACE: return "[halfspace]";
        default:                         return "[unknown]";
    }
}

/* ------------------------------------------------------------------------ */
/* Public API                                                                */
/* ------------------------------------------------------------------------ */

/**
 * @brief Build the outliner panel Clay layout.
 *
 * Creates a floating element at the panel rectangle position containing:
 *   1. A title bar with "Outliner" text.
 *   2. A toolbar row with Box, Sphere, Capsule spawn buttons.
 *   3. A scrollable entity list with click-to-select/deselect.
 *
 * @param ed    Scene editor context (non-NULL).
 * @param rect  Panel screen rectangle (non-NULL).
 */
void scene_ui_build_outliner(struct scene_editor *ed,
                              const struct panel_rect *rect) {
    if (!ed || !rect || rect->w <= 0 || rect->h <= 0) {
        return;
    }

    float panel_w = (float)rect->w;
    float panel_h = (float)rect->h;



    /* Root floating container positioned at the panel rectangle. */
    CLAY(CLAY_ID("Outliner_Root"), {
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
        bool focused = (ed->layout.focus == PANEL_OUTLINER);
        CLAY(CLAY_ID("Outliner_Title"), {
            .layout = {
                .sizing = {CLAY_SIZING_GROW(0),
                           CLAY_SIZING_FIXED(THEME_ROW_HEIGHT)},
                .padding = {THEME_PADDING_SMALL, THEME_PADDING_SMALL, 0, 0},
            },
            .backgroundColor = focused ? COLOR_TITLE_BG_FOCUSED
                                       : COLOR_TITLE_BG,
        }) {
            CLAY_TEXT(CLAY_STRING("Outliner"),
                CLAY_TEXT_CONFIG({
                    .fontSize = THEME_FONT_SIZE_UI,
                    .textColor = {THEME_TEXT_R, THEME_TEXT_G,
                                  THEME_TEXT_B, THEME_TEXT_A},
                    .fontId = CLAY_FONT_UI,
                }));
        }

        /* ---- Entity list area (rows + scrollbar) ---- */
        /* Calculate visible lines from available height.
         * Subtract title bar + padding. */
        float list_h = panel_h
                       - (float)THEME_ROW_HEIGHT
                       - (float)(THEME_PADDING_SMALL * 2);
        int vis_lines = (int)(list_h / (float)(THEME_ROW_HEIGHT + 1));
        if (vis_lines < 1) vis_lines = 1;

        /* Count total active (non-deleted) entities. */
        uint32_t total_entities = 0;
        uint32_t capacity = ed->entities.capacity;
        for (uint32_t i = 0; i < capacity; ++i) {
            const edit_entity_t *e = edit_entity_store_get(&ed->entities, i);
            if (e && !e->pending_delete) total_entities++;
        }
        ed->ui.outliner_total = (int)total_entities;
        ed->ui.outliner_visible_lines = vis_lines;

        /* Clamp scroll offset. */
        int max_scroll = (int)total_entities - vis_lines;
        if (max_scroll < 0) max_scroll = 0;
        if (ed->ui.outliner_scroll > max_scroll)
            ed->ui.outliner_scroll = max_scroll;
        if (ed->ui.outliner_scroll < 0)
            ed->ui.outliner_scroll = 0;
        int skip = ed->ui.outliner_scroll;
        bool needs_scrollbar = (int)total_entities > vis_lines
                               && panel_w > 40.0f;

        CLAY(CLAY_ID("Outliner_ListArea"), {
            .layout = {
                .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                .layoutDirection = CLAY_LEFT_TO_RIGHT,
            },
        }) {
            /* ---- Entity rows (grows to fill) ---- */
            CLAY(CLAY_ID("Outliner_EntityList"), {
                .layout = {
                    .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                    .layoutDirection = CLAY_TOP_TO_BOTTOM,
                    .childGap = 1,
                },
            }) {
                uint32_t visible_index = 0;
                uint32_t active_index = 0;

                for (uint32_t i = 0; i < capacity; ++i) {
                    if (visible_index >= (uint32_t)vis_lines ||
                        visible_index >= OUTLINER_MAX_VISIBLE) {
                        break;
                    }

                    const edit_entity_t *ent =
                        edit_entity_store_get(&ed->entities, i);
                    if (!ent || ent->pending_delete) {
                        continue;
                    }

                    /* Skip entries before scroll offset. */
                    if ((int)active_index < skip) {
                        active_index++;
                        continue;
                    }
                    active_index++;

                    /* Determine if this entity is selected or pending delete. */
                    bool selected = edit_selection_contains(&ed->selection, i);
                    bool pending = ent->pending_delete;

                    /* Set up the click context for this row. */
                    s_entity_click_ctx[visible_index].ed = ed;
                    s_entity_click_ctx[visible_index].entity_id = i;

                    /* Choose row background based on state. */
                    Clay_Color row_bg = pending  ? (Clay_Color){60, 40, 40, 80}
                                      : selected ? COLOR_SELECTION_BG
                                                 : COLOR_ROW_BG;

                    /* Greyed-out text for pending delete entities. */
                    Clay_Color name_color = pending
                        ? (Clay_Color){THEME_TEXT_DIM_R, THEME_TEXT_DIM_G,
                                       THEME_TEXT_DIM_B, 100}
                        : (Clay_Color){THEME_TEXT_R, THEME_TEXT_G,
                                       THEME_TEXT_B, THEME_TEXT_A};
                    Clay_Color tag_color = pending
                        ? (Clay_Color){THEME_TEXT_DIM_R, THEME_TEXT_DIM_G,
                                       THEME_TEXT_DIM_B, 60}
                        : (Clay_Color){THEME_TEXT_DIM_R, THEME_TEXT_DIM_G,
                                       THEME_TEXT_DIM_B, THEME_TEXT_DIM_A};

                    /* Build the entity row element. */
                    CLAY(CLAY_IDI("Outliner_Row", visible_index), {
                        .layout = {
                            .sizing = {CLAY_SIZING_GROW(0),
                                       CLAY_SIZING_FIXED(THEME_ROW_HEIGHT)},
                            .layoutDirection = CLAY_LEFT_TO_RIGHT,
                            .padding = {THEME_PADDING_SMALL,
                                        THEME_PADDING_SMALL, 0, 0},
                            .childGap = THEME_PADDING_SMALL,
                            .childAlignment = {
                                .y = CLAY_ALIGN_Y_CENTER,
                            },
                        },
                        .backgroundColor = row_bg,
                    }) {
                        /* Only allow clicking non-pending entities. */
                        if (!pending) {
                            Clay_OnHover(on_entity_row_hover,
                                         &s_entity_click_ctx[visible_index]);
                        }

                        const char *name = ent->name;
                        int32_t name_len = (int32_t)strlen(name);

                        if (name_len == 0) {
                            snprintf(s_outliner_names[visible_index], 32,
                                     "entity_%u", i);
                            name = s_outliner_names[visible_index];
                            name_len = (int32_t)strlen(name);
                        }

                        Clay_String name_str = {
                            .length = name_len,
                            .chars = name,
                        };
                        Clay__OpenTextElement(name_str,
                            CLAY_TEXT_CONFIG({
                                .fontSize = THEME_FONT_SIZE_UI,
                                .textColor = name_color,
                                .fontId = CLAY_FONT_UI,
                            }));

                        const char *tag = type_tag_string(ent->type);
                        Clay_String tag_str = {
                            .length = (int32_t)strlen(tag),
                            .chars = tag,
                        };
                        Clay__OpenTextElement(tag_str,
                            CLAY_TEXT_CONFIG({
                                .fontSize = THEME_FONT_SIZE_UI,
                                .textColor = tag_color,
                                .fontId = CLAY_FONT_UI,
                            }));
                    }

                    visible_index++;
                }
            }

            /* ---- Scrollbar (fixed 8px wide) ---- */
            if (needs_scrollbar) {
                float track_h = list_h;
                float thumb_ratio = (float)vis_lines / (float)total_entities;
                if (thumb_ratio > 1.0f) thumb_ratio = 1.0f;
                float thumb_h = track_h * thumb_ratio;
                if (thumb_h < 12.0f) thumb_h = 12.0f;

                float scroll_range = track_h - thumb_h;
                float thumb_offset = 0.0f;
                if (max_scroll > 0) {
                    thumb_offset = scroll_range
                                   * ((float)ed->ui.outliner_scroll
                                      / (float)max_scroll);
                }

                CLAY(CLAY_ID("Outliner_ScrollTrack"), {
                    .layout = {
                        .sizing = {CLAY_SIZING_FIXED(8),
                                   CLAY_SIZING_GROW(0)},
                        .layoutDirection = CLAY_TOP_TO_BOTTOM,
                        .padding = {0, 0, (uint16_t)thumb_offset, 0},
                    },
                    .backgroundColor = {25, 27, 33, 255},
                }) {
                    CLAY(CLAY_ID("Outliner_ScrollThumb"), {
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
