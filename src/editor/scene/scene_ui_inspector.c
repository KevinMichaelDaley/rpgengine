/**
 * @file scene_ui_inspector.c
 * @brief Inspector panel Clay UI layout builder.
 *
 * Displays properties of the first selected entity (position, rotation,
 * scale, name, type). Shows "No selection" when nothing is selected.
 *
 * Uses pixel-based scroll tracking to handle heterogeneous content
 * heights (text rows, headers, separators, future thumbnails/tables).
 *
 * All runtime text is formatted into static buffers that persist
 * through the render pass (Clay_String stores only a pointer).
 *
 * Non-static functions: scene_ui_build_inspector (1, within 4-function limit).
 */

#include "ferrum/editor/scene/scene_ui.h"
#include "ferrum/editor/scene/scene_main.h"
#include "ferrum/editor/scene/scene_panel.h"
#include "ferrum/editor/ui/clay_theme.h"
#include "ferrum/editor/ui/clay_fonts.h"
#include "clay.h"
#include <stdio.h>
#include <string.h>

/* ---- Entity type name table ---- */

static const char *entity_type_names[] = {
    "box", "sphere", "capsule", "marker", "mesh", "halfspace"
};
#define ENTITY_TYPE_NAME_COUNT \
    (sizeof(entity_type_names) / sizeof(entity_type_names[0]))

/* ---- Static text buffers ---- */

/**
 * Static buffers for inspector text. Clay_String only holds a pointer,
 * so the backing memory must survive until Clay_EndLayout() / render.
 *
 * Layout:
 *   [0]  Name row:     "Name: EntityName"
 *   [1]  Type row:     "Type: box"
 *   [2]  Position X:   "  X: 1.23"
 *   [3]  Position Y:   "  Y: 4.56"
 *   [4]  Position Z:   "  Z: 7.89"
 *   [5]  Rotation X
 *   [6]  Rotation Y
 *   [7]  Rotation Z
 *   [8]  Scale X
 *   [9]  Scale Y
 *   [10] Scale Z
 */
#define INSP_BUF_COUNT 11
#define INSP_BUF_SIZE  320
static char s_insp_bufs[INSP_BUF_COUNT][INSP_BUF_SIZE];
static int32_t s_insp_lens[INSP_BUF_COUNT];

/** Separator height in pixels. */
#define INSP_SEP_HEIGHT 2

/* ---- Helpers ---- */

/**
 * @brief Format a "Label: Value" string into the static buffer at slot.
 */
static void format_label_value(int slot, const char *label, const char *value) {
    int n = snprintf(s_insp_bufs[slot], INSP_BUF_SIZE, "%s: %s", label, value);
    if (n < 0) n = 0;
    if (n >= INSP_BUF_SIZE) n = INSP_BUF_SIZE - 1;
    s_insp_lens[slot] = (int32_t)n;
}

/**
 * @brief Format a vec3 axis value into three consecutive static buffer slots.
 * @param base_slot  First slot index (uses base_slot, base_slot+1, base_slot+2).
 */
static void format_vec3(int base_slot, const float v[3]) {
    static const char *axes[3] = {"  X", "  Y", "  Z"};
    for (int i = 0; i < 3; ++i) {
        int n = snprintf(s_insp_bufs[base_slot + i], INSP_BUF_SIZE,
                         "%s: %.2f", axes[i], (double)v[i]);
        if (n < 0) n = 0;
        if (n >= INSP_BUF_SIZE) n = INSP_BUF_SIZE - 1;
        s_insp_lens[base_slot + i] = (int32_t)n;
    }
}

/**
 * @brief Check if an element at [y_cursor, y_cursor+h) is visible.
 *
 * An element is visible if it overlaps [scroll_px, scroll_px + visible_h).
 *
 * @param y_cursor    Top of this element in content space (pixels).
 * @param h           Height of this element in pixels.
 * @param scroll_px   Current scroll offset in pixels.
 * @param visible_h   Visible viewport height in pixels.
 * @return true if any part of the element is within the viewport.
 */
static bool is_visible(float y_cursor, float h,
                        float scroll_px, float visible_h) {
    float top = y_cursor;
    float bot = y_cursor + h;
    return bot > scroll_px && top < scroll_px + visible_h;
}

/* ---- Public API ---- */

void scene_ui_build_inspector(scene_editor_t *ed,
                              const panel_rect_t *rect) {
    if (!ed || !rect || rect->w <= 0 || rect->h <= 0) return;

    float panel_h = (float)rect->h;
    Clay_Color color_text = {THEME_TEXT_R, THEME_TEXT_G,
                              THEME_TEXT_B, THEME_TEXT_A};
    Clay_Color color_accent = {THEME_ACCENT_R, THEME_ACCENT_G,
                                THEME_ACCENT_B, THEME_ACCENT_A};

    /* Visible height for content area (below title bar, minus padding). */
    float title_h = (float)THEME_ROW_HEIGHT;
    float visible_h = panel_h - title_h - (float)(THEME_PADDING_SMALL * 2);
    if (visible_h < 1.0f) visible_h = 1.0f;

    CLAY(CLAY_ID("InspectorRoot"), {
        .layout = {
            .sizing = {CLAY_SIZING_FIXED((float)rect->w),
                       CLAY_SIZING_FIXED((float)rect->h)},
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .padding = {THEME_PADDING_SMALL, THEME_PADDING_SMALL,
                        THEME_PADDING_SMALL, THEME_PADDING_SMALL},
        },
        .backgroundColor = {THEME_BG_PANEL_R, THEME_BG_PANEL_G,
                             THEME_BG_PANEL_B, THEME_BG_PANEL_A},
        .clip = {.horizontal = true, .vertical = true},
        .floating = {
            .attachTo = CLAY_ATTACH_TO_ROOT,
            .offset = {(float)rect->x, (float)rect->y},
        },
    }) {
        /* Title bar */
        bool focused = (ed->layout.focus == PANEL_INSPECTOR);
        uint8_t title_alpha = focused ? 140 : 60;
        CLAY(CLAY_ID("InspectorTitle"), {
            .layout = {
                .sizing = {CLAY_SIZING_GROW(0),
                           CLAY_SIZING_FIXED(THEME_ROW_HEIGHT)},
                .padding = {THEME_PADDING_SMALL, THEME_PADDING_SMALL, 0, 0},
            },
            .backgroundColor = {THEME_ACCENT_R, THEME_ACCENT_G,
                                 THEME_ACCENT_B, title_alpha},
        }) {
            CLAY_TEXT(CLAY_STRING("Inspector"),
                CLAY_TEXT_CONFIG({
                    .fontSize = THEME_FONT_SIZE_UI,
                    .textColor = color_text,
                    .fontId = CLAY_FONT_UI,
                }));
        }

        uint32_t sel_count = edit_selection_count(&ed->selection);

        if (sel_count == 0) {
            ed->ui.inspector_total = 0;
            ed->ui.inspector_visible_lines = 0;
            ed->ui.inspector_scroll = 0;

            CLAY(CLAY_ID("InspectorEmpty"), {
                .layout = {
                    .sizing = {CLAY_SIZING_GROW(0),
                               CLAY_SIZING_FIXED(THEME_ROW_HEIGHT)},
                    .padding = {THEME_PADDING, THEME_PADDING,
                                THEME_PADDING, 0},
                },
            }) {
                CLAY_TEXT(CLAY_STRING("No selection"),
                    CLAY_TEXT_CONFIG({
                        .fontSize = THEME_FONT_SIZE_UI,
                        .textColor = {THEME_TEXT_DIM_R, THEME_TEXT_DIM_G,
                                      THEME_TEXT_DIM_B, THEME_TEXT_DIM_A},
                        .fontId = CLAY_FONT_UI,
                    }));
            }
        } else {
            const uint32_t *sel_ids = edit_selection_ids(&ed->selection);
            uint32_t first_id = sel_ids[0];
            const edit_entity_t *ent =
                edit_entity_store_get(&ed->entities, first_id);
            if (!ent) return;

            /* Format all text into static buffers. */
            const char *display_name = (ent->name[0] != '\0')
                                        ? ent->name : "(unnamed)";
            format_label_value(0, "Name", display_name);

            const char *type_name = "unknown";
            if (ent->type < ENTITY_TYPE_NAME_COUNT) {
                type_name = entity_type_names[ent->type];
            }
            format_label_value(1, "Type", type_name);

            format_vec3(2, ent->pos);   /* slots 2,3,4 */
            format_vec3(5, ent->rot);   /* slots 5,6,7 */
            format_vec3(8, ent->scale); /* slots 8,9,10 */

            /* ---- Compute total content height ---- */
            /* Row heights: name, type, separator, then 3x (header + 3 values). */
            float row_h = (float)THEME_ROW_HEIGHT;
            float sep_h = (float)INSP_SEP_HEIGHT;
            float total_h = row_h     /* Name */
                          + row_h     /* Type */
                          + sep_h     /* Separator */
                          + row_h     /* Position header */
                          + row_h * 3 /* X Y Z */
                          + row_h     /* Rotation header */
                          + row_h * 3 /* X Y Z */
                          + row_h     /* Scale header */
                          + row_h * 3; /* X Y Z */

            /* Store total content height for scroll clamping. */
            ed->ui.inspector_total = (int)total_h;
            ed->ui.inspector_visible_lines = (int)visible_h;

            /* Clamp scroll offset (pixel-based). */
            int max_scroll_px = (int)(total_h - visible_h);
            if (max_scroll_px < 0) max_scroll_px = 0;
            if (ed->ui.inspector_scroll > max_scroll_px)
                ed->ui.inspector_scroll = max_scroll_px;
            if (ed->ui.inspector_scroll < 0)
                ed->ui.inspector_scroll = 0;

            float scroll_px = (float)ed->ui.inspector_scroll;
            bool needs_scrollbar = total_h > visible_h;

            /* ---- Content area (rows + scrollbar) ---- */
            CLAY(CLAY_ID("InspContentArea"), {
                .layout = {
                    .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                    .layoutDirection = CLAY_LEFT_TO_RIGHT,
                },
            }) {
                /* ---- Property rows (windowed by pixel offset) ---- */
                CLAY(CLAY_ID("InspRows"), {
                    .layout = {
                        .sizing = {CLAY_SIZING_GROW(0),
                                   CLAY_SIZING_GROW(0)},
                        .layoutDirection = CLAY_TOP_TO_BOTTOM,
                    },
                }) {
                    float y = 0.0f;
                    uint32_t clay_idx = 0;

                    /* Name */
                    if (is_visible(y, row_h, scroll_px, visible_h)) {
                        Clay_String text = {.length = s_insp_lens[0],
                                            .chars = s_insp_bufs[0]};
                        CLAY(CLAY_IDI("InspRow", clay_idx), {
                            .layout = {
                                .sizing = {CLAY_SIZING_GROW(0),
                                           CLAY_SIZING_FIXED(row_h)},
                                .padding = {THEME_PADDING, THEME_PADDING,
                                            0, 0},
                            },
                        }) {
                            Clay__OpenTextElement(text,
                                CLAY_TEXT_CONFIG({
                                    .fontSize = THEME_FONT_SIZE_UI,
                                    .textColor = color_text,
                                    .fontId = CLAY_FONT_UI,
                                }));
                        }
                    }
                    y += row_h;
                    clay_idx++;

                    /* Type */
                    if (is_visible(y, row_h, scroll_px, visible_h)) {
                        Clay_String text = {.length = s_insp_lens[1],
                                            .chars = s_insp_bufs[1]};
                        CLAY(CLAY_IDI("InspRow", clay_idx), {
                            .layout = {
                                .sizing = {CLAY_SIZING_GROW(0),
                                           CLAY_SIZING_FIXED(row_h)},
                                .padding = {THEME_PADDING, THEME_PADDING,
                                            0, 0},
                            },
                        }) {
                            Clay__OpenTextElement(text,
                                CLAY_TEXT_CONFIG({
                                    .fontSize = THEME_FONT_SIZE_UI,
                                    .textColor = color_text,
                                    .fontId = CLAY_FONT_UI,
                                }));
                        }
                    }
                    y += row_h;
                    clay_idx++;

                    /* Separator */
                    if (is_visible(y, sep_h, scroll_px, visible_h)) {
                        CLAY(CLAY_IDI("InspRow", clay_idx), {
                            .layout = {
                                .sizing = {CLAY_SIZING_GROW(0),
                                           CLAY_SIZING_FIXED(sep_h)},
                                .padding = {THEME_PADDING, THEME_PADDING,
                                            THEME_PADDING_SMALL,
                                            THEME_PADDING_SMALL},
                            },
                            .backgroundColor = {THEME_ACCENT_R,
                                                 THEME_ACCENT_G,
                                                 THEME_ACCENT_B, 40},
                        }) {}
                    }
                    y += sep_h;
                    clay_idx++;

                    /* Position, Rotation, Scale — 3 sections of
                     * (header + 3 values). */
                    static const char *section_labels[3] = {
                        "Position", "Rotation", "Scale"
                    };
                    int buf_base[3] = {2, 5, 8};

                    for (int sec = 0; sec < 3; sec++) {
                        /* Section header */
                        if (is_visible(y, row_h, scroll_px, visible_h)) {
                            Clay_String hdr = {
                                .length = (int32_t)strlen(
                                    section_labels[sec]),
                                .chars = section_labels[sec],
                            };
                            CLAY(CLAY_IDI("InspRow", clay_idx), {
                                .layout = {
                                    .sizing = {CLAY_SIZING_GROW(0),
                                               CLAY_SIZING_FIXED(row_h)},
                                    .padding = {THEME_PADDING, THEME_PADDING,
                                                THEME_PADDING_SMALL, 0},
                                },
                            }) {
                                Clay__OpenTextElement(hdr,
                                    CLAY_TEXT_CONFIG({
                                        .fontSize = THEME_FONT_SIZE_UI,
                                        .textColor = color_accent,
                                        .fontId = CLAY_FONT_UI,
                                    }));
                            }
                        }
                        y += row_h;
                        clay_idx++;

                        /* X, Y, Z values */
                        for (int axis = 0; axis < 3; axis++) {
                            int buf_slot = buf_base[sec] + axis;
                            if (is_visible(y, row_h, scroll_px, visible_h)) {
                                Clay_String text = {
                                    .length = s_insp_lens[buf_slot],
                                    .chars = s_insp_bufs[buf_slot],
                                };
                                CLAY(CLAY_IDI("InspRow", clay_idx), {
                                    .layout = {
                                        .sizing = {CLAY_SIZING_GROW(0),
                                                   CLAY_SIZING_FIXED(row_h)},
                                        .padding = {THEME_PADDING,
                                                    THEME_PADDING, 0, 0},
                                    },
                                }) {
                                    Clay__OpenTextElement(text,
                                        CLAY_TEXT_CONFIG({
                                            .fontSize = THEME_FONT_SIZE_MONO,
                                            .textColor = color_text,
                                            .fontId = CLAY_FONT_MONO,
                                        }));
                                }
                            }
                            y += row_h;
                            clay_idx++;
                        }
                    }
                }

                /* ---- Scrollbar ---- */
                if (needs_scrollbar) {
                    float thumb_ratio = visible_h / total_h;
                    if (thumb_ratio > 1.0f) thumb_ratio = 1.0f;
                    float thumb_h = visible_h * thumb_ratio;
                    if (thumb_h < 12.0f) thumb_h = 12.0f;

                    float scroll_range = visible_h - thumb_h;
                    float thumb_offset = 0.0f;
                    if (max_scroll_px > 0) {
                        thumb_offset = scroll_range
                                       * (scroll_px / (float)max_scroll_px);
                    }

                    CLAY(CLAY_ID("Insp_ScrollTrack"), {
                        .layout = {
                            .sizing = {CLAY_SIZING_FIXED(8),
                                       CLAY_SIZING_GROW(0)},
                        },
                        .backgroundColor = {25, 27, 33, 255},
                    }) {
                        CLAY(CLAY_ID("Insp_ScrollThumb"), {
                            .layout = {
                                .sizing = {CLAY_SIZING_FIXED(8),
                                           CLAY_SIZING_FIXED(thumb_h)},
                            },
                            .backgroundColor = {80, 85, 95, 255},
                            .cornerRadius = CLAY_CORNER_RADIUS(4),
                            .floating = {
                                .attachTo = CLAY_ATTACH_TO_PARENT,
                                .offset = {0, thumb_offset},
                            },
                        }) {}
                    }
                }
            }
        }
    }
}
