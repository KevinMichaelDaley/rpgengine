/**
 * @file scene_ui_viewport.c
 * @brief Viewport panel Clay UI layout: 2D top-down entity view.
 *
 * Renders a 2D top-down view of entities. Each entity is displayed as
 * a small colored rectangle at its (pos[0], pos[2]) world position
 * mapped to screen coordinates. Selected entities are highlighted
 * with the selection color (orange).
 *
 * Contains one public function (scene_ui_build_viewport) and static
 * helpers for coordinate mapping and sub-sections.
 */

#include "ferrum/editor/scene/scene_ui.h"
#include "ferrum/editor/scene/scene_main.h"
#include "ferrum/editor/ui/clay_theme.h"
#include "ferrum/editor/ui/clay_fonts.h"
#include "clay.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Constants                                                                 */
/* ------------------------------------------------------------------------ */

/** Maximum entities rendered in the viewport to avoid too many Clay elements. */
#define VIEWPORT_MAX_ENTITIES 64

/**
 * @brief Static fallback name buffers for entities without names.
 *
 * Clay_String only stores a pointer — the backing memory must
 * survive until Clay_EndLayout() / render. Stack locals won't work.
 */
static char s_viewport_names[VIEWPORT_MAX_ENTITIES][32];

/** Pixels per world unit for the 2D projection. */
#define VIEWPORT_SCALE 20.0f

/** Size of each entity marker in pixels. */
#define VIEWPORT_MARKER_SIZE 12.0f

/* ---- Color constants ---- */

/** Viewport background color. */
static const Clay_Color COLOR_VIEWPORT_BG = {
    THEME_BG_VIEWPORT_R, THEME_BG_VIEWPORT_G,
    THEME_BG_VIEWPORT_B, THEME_BG_VIEWPORT_A
};

/** Title bar background (accent with reduced alpha). */
static const Clay_Color COLOR_TITLE_BG = {
    THEME_ACCENT_R, THEME_ACCENT_G, THEME_ACCENT_B, 60
};

/** Normal entity marker color (blue accent). */
static const Clay_Color COLOR_MARKER_NORMAL = {
    THEME_ACCENT_R, THEME_ACCENT_G, THEME_ACCENT_B, THEME_ACCENT_A
};

/** Selected entity marker color (orange). */
static const Clay_Color COLOR_MARKER_SELECTED = {
    THEME_SELECTION_R, THEME_SELECTION_G,
    THEME_SELECTION_B, THEME_SELECTION_A
};

/* ------------------------------------------------------------------------ */
/* Static helpers                                                            */
/* ------------------------------------------------------------------------ */

/**
 * @brief Clamp a float value to [min, max].
 * @param val  Value to clamp.
 * @param min  Minimum bound.
 * @param max  Maximum bound.
 * @return Clamped value.
 */
static float clampf(float val, float min, float max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

/* ------------------------------------------------------------------------ */
/* Public API                                                                */
/* ------------------------------------------------------------------------ */

/**
 * @brief Build the viewport panel Clay layout.
 *
 * Creates a floating element at the panel rectangle position containing:
 *   1. A title bar with "Viewport" text.
 *   2. A grid origin indicator.
 *   3. Entity markers at mapped world positions (max 64).
 *   4. "Empty scene" text if no active entities exist.
 *
 * World-to-screen mapping:
 *   screen_x = panel_center_x + pos[0] * 20
 *   screen_y = panel_center_y - pos[2] * 20
 *
 * @param ed    Scene editor context (non-NULL).
 * @param rect  Panel screen rectangle (non-NULL).
 */
void scene_ui_build_viewport(struct scene_editor *ed,
                              const struct panel_rect *rect) {
    if (!ed || !rect || rect->w <= 0 || rect->h <= 0) {
        return;
    }

    float panel_w = (float)rect->w;
    float panel_h = (float)rect->h;

    /* Center of the panel in screen coordinates, used as the origin
     * for the 2D top-down projection. */
    float panel_center_x = (float)rect->x + panel_w * 0.5f;
    float panel_center_y = (float)rect->y + panel_h * 0.5f;

    /* Panel bounds for clamping entity markers. */
    float panel_left   = (float)rect->x;
    float panel_top    = (float)rect->y;
    float panel_right  = (float)rect->x + panel_w - VIEWPORT_MARKER_SIZE;
    float panel_bottom = (float)rect->y + panel_h - VIEWPORT_MARKER_SIZE;

    /* Root floating container positioned at the panel rectangle. */
    CLAY(CLAY_ID("Viewport_Root"), {
        .layout = {
            .sizing = {CLAY_SIZING_FIXED(panel_w),
                       CLAY_SIZING_FIXED(panel_h)},
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .padding = {THEME_PADDING_SMALL, THEME_PADDING_SMALL,
                        THEME_PADDING_SMALL, THEME_PADDING_SMALL},
        },
        .backgroundColor = COLOR_VIEWPORT_BG,
        .floating = {
            .attachTo = CLAY_ATTACH_TO_ROOT,
            .offset = {(float)rect->x, (float)rect->y},
        },
    }) {
        /* ---- Title bar ---- */
        CLAY(CLAY_ID("Viewport_Title"), {
            .layout = {
                .sizing = {CLAY_SIZING_GROW(0),
                           CLAY_SIZING_FIXED(THEME_ROW_HEIGHT)},
                .padding = {THEME_PADDING_SMALL, THEME_PADDING_SMALL, 0, 0},
            },
            .backgroundColor = COLOR_TITLE_BG,
        }) {
            CLAY_TEXT(CLAY_STRING("Viewport"),
                CLAY_TEXT_CONFIG({
                    .fontSize = THEME_FONT_SIZE_UI,
                    .textColor = {THEME_TEXT_R, THEME_TEXT_G,
                                  THEME_TEXT_B, THEME_TEXT_A},
                    .fontId = CLAY_FONT_UI,
                }));
        }

        /* ---- Grid origin indicator ---- */
        CLAY(CLAY_ID("Viewport_Grid"), {
            .layout = {
                .sizing = {CLAY_SIZING_GROW(0),
                           CLAY_SIZING_FIXED(THEME_ROW_HEIGHT)},
                .padding = {THEME_PADDING_SMALL, THEME_PADDING_SMALL, 0, 0},
            },
        }) {
            CLAY_TEXT(CLAY_STRING("Origin (0,0)"),
                CLAY_TEXT_CONFIG({
                    .fontSize = THEME_FONT_SIZE_UI,
                    .textColor = {THEME_TEXT_DIM_R, THEME_TEXT_DIM_G,
                                  THEME_TEXT_DIM_B, THEME_TEXT_DIM_A},
                    .fontId = CLAY_FONT_UI,
                }));
        }

        /* ---- Entity markers or empty-scene message ---- */
        if (ed->entities.active_count == 0) {
            /* No entities: show placeholder text. */
            CLAY(CLAY_ID("Viewport_Empty"), {
                .layout = {
                    .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                    .childAlignment = {
                        .x = CLAY_ALIGN_X_CENTER,
                        .y = CLAY_ALIGN_Y_CENTER,
                    },
                },
            }) {
                CLAY_TEXT(CLAY_STRING("Empty scene"),
                    CLAY_TEXT_CONFIG({
                        .fontSize = THEME_FONT_SIZE_UI,
                        .textColor = {THEME_TEXT_DIM_R, THEME_TEXT_DIM_G,
                                      THEME_TEXT_DIM_B, THEME_TEXT_DIM_A},
                        .fontId = CLAY_FONT_UI,
                    }));
            }
        }
    }

    /* ---- Floating entity markers ---- */
    /* These are placed as independent floating elements over the viewport
     * so that each marker can be positioned at its mapped screen coordinate. */
    uint32_t rendered = 0;
    uint32_t capacity = ed->entities.capacity;

    for (uint32_t i = 0; i < capacity; ++i) {
        if (rendered >= VIEWPORT_MAX_ENTITIES) {
            break;
        }

        const edit_entity_t *ent = edit_entity_store_get(&ed->entities, i);
        if (!ent) {
            continue;
        }

        /* Map world position to screen coordinates.
         * X-axis maps to screen X (right = positive).
         * Z-axis maps to screen Y (forward/up in world = up on screen). */
        float screen_x = panel_center_x + ent->pos[0] * VIEWPORT_SCALE;
        float screen_y = panel_center_y - ent->pos[2] * VIEWPORT_SCALE;

        /* Clamp to panel bounds so markers stay visible. */
        screen_x = clampf(screen_x, panel_left, panel_right);
        screen_y = clampf(screen_y, panel_top, panel_bottom);

        /* Choose marker color based on selection state. */
        bool selected = edit_selection_contains(&ed->selection, i);
        Clay_Color marker_color = selected ? COLOR_MARKER_SELECTED
                                           : COLOR_MARKER_NORMAL;

        /* Entity marker: small colored rectangle. */
        CLAY(CLAY_IDI("Viewport_Marker", rendered), {
            .layout = {
                .sizing = {CLAY_SIZING_FIXED(VIEWPORT_MARKER_SIZE),
                           CLAY_SIZING_FIXED(VIEWPORT_MARKER_SIZE)},
            },
            .backgroundColor = marker_color,
            .floating = {
                .attachTo = CLAY_ATTACH_TO_ROOT,
                .offset = {screen_x, screen_y},
            },
        }) {
            /* Marker body is just the colored rectangle; no children. */
        }

        /* Entity name label below the marker. */
        {
            const char *name = ent->name;
            int32_t name_len = (int32_t)strlen(name);

            /* If the entity has no name, use a static fallback buffer. */
            if (name_len == 0) {
                snprintf(s_viewport_names[rendered], 32,
                         "entity_%u", i);
                name = s_viewport_names[rendered];
                name_len = (int32_t)strlen(name);
            }

            Clay_String label_str = {
                .length = name_len,
                .chars = name,
            };

            CLAY(CLAY_IDI("Viewport_Label", rendered), {
                .layout = {
                    .sizing = {CLAY_SIZING_FIT(0),
                               CLAY_SIZING_FIXED(THEME_ROW_HEIGHT)},
                },
                .floating = {
                    .attachTo = CLAY_ATTACH_TO_ROOT,
                    .offset = {screen_x, screen_y + VIEWPORT_MARKER_SIZE + 2.0f},
                },
            }) {
                Clay__OpenTextElement(label_str,
                    CLAY_TEXT_CONFIG({
                        .fontSize = THEME_FONT_SIZE_UI,
                        .textColor = {THEME_TEXT_R, THEME_TEXT_G,
                                      THEME_TEXT_B, THEME_TEXT_A},
                        .fontId = CLAY_FONT_UI,
                    }));
            }
        }

        rendered++;
    }
}
