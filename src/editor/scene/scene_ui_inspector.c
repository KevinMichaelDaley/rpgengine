/**
 * @file scene_ui_inspector.c
 * @brief Inspector panel Clay UI layout builder.
 *
 * Displays properties of the first selected entity (position, rotation,
 * scale, name, type). Shows "No selection" when nothing is selected.
 *
 * All runtime text is formatted into static buffers that persist
 * through the render pass (Clay_String stores only a pointer).
 *
 * Non-static functions: scene_ui_build_inspector (1, within 4-function limit).
 */

#include "ferrum/editor/scene/scene_ui.h"
#include "ferrum/editor/scene/scene_main.h"
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
 * @brief Emit a single text row from a static buffer slot.
 */
static void emit_text_row(int slot, uint32_t clay_id_index,
                           uint16_t font_size, Clay_Color color,
                           uint16_t font_id) {
    Clay_String text = {
        .length = s_insp_lens[slot],
        .chars = s_insp_bufs[slot],
    };
    CLAY(CLAY_IDI("InspRow", clay_id_index), {
        .layout = {
            .sizing = {CLAY_SIZING_GROW(0),
                       CLAY_SIZING_FIXED(THEME_ROW_HEIGHT)},
            .padding = {THEME_PADDING, THEME_PADDING, 0, 0},
        },
    }) {
        Clay__OpenTextElement(text,
            CLAY_TEXT_CONFIG({
                .fontSize = font_size,
                .textColor = color,
                .fontId = font_id,
            }));
    }
}

/* ---- Public API ---- */

void scene_ui_build_inspector(scene_editor_t *ed,
                              const panel_rect_t *rect) {
    if (!ed || !rect || rect->w <= 0 || rect->h <= 0) return;

    Clay_Color color_text = {THEME_TEXT_R, THEME_TEXT_G,
                              THEME_TEXT_B, THEME_TEXT_A};
    Clay_Color color_accent = {THEME_ACCENT_R, THEME_ACCENT_G,
                                THEME_ACCENT_B, THEME_ACCENT_A};

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
        .floating = {
            .attachTo = CLAY_ATTACH_TO_ROOT,
            .offset = {(float)rect->x, (float)rect->y},
        },
    }) {
        /* Title bar */
        CLAY(CLAY_ID("InspectorTitle"), {
            .layout = {
                .sizing = {CLAY_SIZING_GROW(0),
                           CLAY_SIZING_FIXED(THEME_ROW_HEIGHT)},
                .padding = {THEME_PADDING_SMALL, THEME_PADDING_SMALL, 0, 0},
            },
            .backgroundColor = {THEME_ACCENT_R, THEME_ACCENT_G,
                                 THEME_ACCENT_B, 60},
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

            /* Emit rows from static buffers. */
            emit_text_row(0, 0, THEME_FONT_SIZE_UI, color_text, CLAY_FONT_UI);
            emit_text_row(1, 1, THEME_FONT_SIZE_UI, color_text, CLAY_FONT_UI);

            /* Separator */
            CLAY(CLAY_ID("InspectorSep"), {
                .layout = {
                    .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(2)},
                    .padding = {THEME_PADDING, THEME_PADDING,
                                THEME_PADDING_SMALL, THEME_PADDING_SMALL},
                },
                .backgroundColor = {THEME_ACCENT_R, THEME_ACCENT_G,
                                     THEME_ACCENT_B, 40},
            }) {}

            /* Position header + XYZ */
            CLAY(CLAY_IDI("InspHeader", 0), {
                .layout = {
                    .sizing = {CLAY_SIZING_GROW(0),
                               CLAY_SIZING_FIXED(THEME_ROW_HEIGHT)},
                    .padding = {THEME_PADDING, THEME_PADDING,
                                THEME_PADDING_SMALL, 0},
                },
            }) {
                CLAY_TEXT(CLAY_STRING("Position"),
                    CLAY_TEXT_CONFIG({
                        .fontSize = THEME_FONT_SIZE_UI,
                        .textColor = color_accent,
                        .fontId = CLAY_FONT_UI,
                    }));
            }
            emit_text_row(2, 10, THEME_FONT_SIZE_MONO, color_text, CLAY_FONT_MONO);
            emit_text_row(3, 11, THEME_FONT_SIZE_MONO, color_text, CLAY_FONT_MONO);
            emit_text_row(4, 12, THEME_FONT_SIZE_MONO, color_text, CLAY_FONT_MONO);

            /* Rotation header + XYZ */
            CLAY(CLAY_IDI("InspHeader", 1), {
                .layout = {
                    .sizing = {CLAY_SIZING_GROW(0),
                               CLAY_SIZING_FIXED(THEME_ROW_HEIGHT)},
                    .padding = {THEME_PADDING, THEME_PADDING,
                                THEME_PADDING_SMALL, 0},
                },
            }) {
                CLAY_TEXT(CLAY_STRING("Rotation"),
                    CLAY_TEXT_CONFIG({
                        .fontSize = THEME_FONT_SIZE_UI,
                        .textColor = color_accent,
                        .fontId = CLAY_FONT_UI,
                    }));
            }
            emit_text_row(5, 13, THEME_FONT_SIZE_MONO, color_text, CLAY_FONT_MONO);
            emit_text_row(6, 14, THEME_FONT_SIZE_MONO, color_text, CLAY_FONT_MONO);
            emit_text_row(7, 15, THEME_FONT_SIZE_MONO, color_text, CLAY_FONT_MONO);

            /* Scale header + XYZ */
            CLAY(CLAY_IDI("InspHeader", 2), {
                .layout = {
                    .sizing = {CLAY_SIZING_GROW(0),
                               CLAY_SIZING_FIXED(THEME_ROW_HEIGHT)},
                    .padding = {THEME_PADDING, THEME_PADDING,
                                THEME_PADDING_SMALL, 0},
                },
            }) {
                CLAY_TEXT(CLAY_STRING("Scale"),
                    CLAY_TEXT_CONFIG({
                        .fontSize = THEME_FONT_SIZE_UI,
                        .textColor = color_accent,
                        .fontId = CLAY_FONT_UI,
                    }));
            }
            emit_text_row(8, 16, THEME_FONT_SIZE_MONO, color_text, CLAY_FONT_MONO);
            emit_text_row(9, 17, THEME_FONT_SIZE_MONO, color_text, CLAY_FONT_MONO);
            emit_text_row(10, 18, THEME_FONT_SIZE_MONO, color_text, CLAY_FONT_MONO);
        }
    }
}
