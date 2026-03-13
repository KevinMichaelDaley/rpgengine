/**
 * @file scene_ui_viewport.c
 * @brief Viewport panel Clay UI layout: display 3D FBO texture.
 *
 * Displays the viewport FBO's color texture as a Clay IMAGE element
 * covering the panel rectangle. The 3D scene is rendered into the
 * FBO by scene_viewport_draw.c before Clay layout runs.
 *
 * Also shows a title bar overlay and entity count.
 *
 * Non-static functions (1 / 4 limit):
 *   scene_ui_build_viewport
 */

#include "ferrum/editor/scene/scene_ui.h"
#include "ferrum/editor/scene/scene_main.h"
#include "ferrum/editor/scene/scene_panel.h"
#include "ferrum/editor/scene/scene_viewport_render.h"
#include "ferrum/editor/ui/clay_theme.h"
#include "ferrum/editor/ui/clay_fonts.h"
#include "clay.h"

#include <stdio.h>
#include <string.h>

/* ---- Constants ---- */

/** Title bar background: normal (unfocused). */
static const Clay_Color COLOR_TITLE_BG = {
    THEME_ACCENT_R, THEME_ACCENT_G, THEME_ACCENT_B, 60
};

/** Title bar background: focused (brighter). */
static const Clay_Color COLOR_TITLE_BG_FOCUSED = {
    THEME_ACCENT_R, THEME_ACCENT_G, THEME_ACCENT_B, 140
};

/** Button background color (slightly lighter than panel). */
static const Clay_Color COLOR_BTN_BG = {50, 52, 58, 255};

/** Selection highlight background. */
static const Clay_Color COLOR_SELECTION_BG = {
    THEME_SELECTION_R, THEME_SELECTION_G,
    THEME_SELECTION_B, THEME_SELECTION_A
};

/** Static buffer for viewport status text. */
#define VP_STATUS_BUF_SIZE 128
static char s_vp_status[VP_STATUS_BUF_SIZE];

/* ---- Mode button hover callbacks ---- */

/** @brief Hover callback for mode: Translate. */
static void on_mode_translate_hover(Clay_ElementId id, Clay_PointerData data,
                                     void *user) {
    (void)id;
    scene_editor_t *ed = (scene_editor_t *)user;
    if (data.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
        ed->ui.action = UI_ACTION_MODE_TRANSLATE;
    }
}

/** @brief Hover callback for mode: Rotate. */
static void on_mode_rotate_hover(Clay_ElementId id, Clay_PointerData data,
                                  void *user) {
    (void)id;
    scene_editor_t *ed = (scene_editor_t *)user;
    if (data.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
        ed->ui.action = UI_ACTION_MODE_ROTATE;
    }
}

/** @brief Hover callback for mode: Scale. */
static void on_mode_scale_hover(Clay_ElementId id, Clay_PointerData data,
                                 void *user) {
    (void)id;
    scene_editor_t *ed = (scene_editor_t *)user;
    if (data.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
        ed->ui.action = UI_ACTION_MODE_SCALE;
    }
}

/* ---- Public API ---- */

void scene_ui_build_viewport(struct scene_editor *ed,
                              const struct panel_rect *rect) {
    if (!ed || !rect || rect->w <= 0 || rect->h <= 0) {
        return;
    }

    float panel_w = (float)rect->w;
    float panel_h = (float)rect->h;

    /* Get the FBO color texture handle for display. */
    uint32_t tex = viewport_render_get_texture(&ed->viewport);

    /* Format status text. */
    snprintf(s_vp_status, sizeof(s_vp_status),
             "Viewport  Entities: %u", ed->entities.active_count);

    /* Root floating container positioned at the panel rectangle. */
    CLAY(CLAY_ID("Viewport_Root"), {
        .layout = {
            .sizing = {CLAY_SIZING_FIXED(panel_w),
                       CLAY_SIZING_FIXED(panel_h)},
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
        },
        .floating = {
            .attachTo = CLAY_ATTACH_TO_ROOT,
            .offset = {(float)rect->x, (float)rect->y},
        },
        /* Display the FBO texture as the viewport background.
         * imageData points to the GL texture handle (uint32_t). */
        .image = {
            .imageData = tex > 0 ? &ed->viewport.color_tex : NULL,
        },
        .backgroundColor = tex == 0
            ? (Clay_Color){THEME_BG_VIEWPORT_R, THEME_BG_VIEWPORT_G,
                            THEME_BG_VIEWPORT_B, THEME_BG_VIEWPORT_A}
            : (Clay_Color){0, 0, 0, 0},
    }) {
        /* ---- Title bar overlay ---- */
        bool vp_focused = (ed->layout.focus == PANEL_VIEWPORT);
        CLAY(CLAY_ID("Viewport_Title"), {
            .layout = {
                .sizing = {CLAY_SIZING_GROW(0),
                           CLAY_SIZING_FIXED(THEME_ROW_HEIGHT)},
                .padding = {THEME_PADDING_SMALL, THEME_PADDING_SMALL, 0, 0},
                .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
            },
            .backgroundColor = vp_focused ? COLOR_TITLE_BG_FOCUSED
                                          : COLOR_TITLE_BG,
        }) {
            Clay_String status_str = {
                .length = (int32_t)strlen(s_vp_status),
                .chars = s_vp_status,
            };
            Clay__OpenTextElement(status_str,
                CLAY_TEXT_CONFIG({
                    .fontSize = THEME_FONT_SIZE_UI,
                    .textColor = {THEME_TEXT_R, THEME_TEXT_G,
                                  THEME_TEXT_B, THEME_TEXT_A},
                    .fontId = CLAY_FONT_UI,
                }));
        }

        /* ---- Mode toolbar overlay (far left, below title) ---- */
        {
            Clay_Color translate_bg = (ed->ui.transform_mode == UI_MODE_TRANSLATE)
                                        ? COLOR_SELECTION_BG : COLOR_BTN_BG;
            Clay_Color rotate_bg = (ed->ui.transform_mode == UI_MODE_ROTATE)
                                     ? COLOR_SELECTION_BG : COLOR_BTN_BG;
            Clay_Color scale_bg = (ed->ui.transform_mode == UI_MODE_SCALE)
                                    ? COLOR_SELECTION_BG : COLOR_BTN_BG;

            CLAY(CLAY_ID("VP_ModeBar"), {
                .layout = {
                    .sizing = {CLAY_SIZING_FIT(0),
                               CLAY_SIZING_FIXED(THEME_ROW_HEIGHT + THEME_PADDING)},
                    .layoutDirection = CLAY_LEFT_TO_RIGHT,
                    .padding = {THEME_PADDING_SMALL, THEME_PADDING_SMALL,
                                THEME_PADDING_SMALL, THEME_PADDING_SMALL},
                    .childGap = THEME_PADDING_SMALL,
                    .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                },
                .backgroundColor = {THEME_BG_PANEL_R, THEME_BG_PANEL_G,
                                     THEME_BG_PANEL_B, 180},
            }) {
                CLAY_TEXT(CLAY_STRING("Mode:"),
                    CLAY_TEXT_CONFIG({
                        .fontSize = THEME_FONT_SIZE_UI,
                        .textColor = {THEME_TEXT_DIM_R, THEME_TEXT_DIM_G,
                                      THEME_TEXT_DIM_B, THEME_TEXT_DIM_A},
                        .fontId = CLAY_FONT_UI,
                    }));

                CLAY(CLAY_ID("VP_BtnTranslate"), {
                    .layout = {
                        .sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(20)},
                        .padding = {THEME_PADDING, THEME_PADDING, 0, 0},
                    },
                    .backgroundColor = translate_bg,
                }) {
                    Clay_OnHover(on_mode_translate_hover, ed);
                    CLAY_TEXT(CLAY_STRING("Move(G)"),
                        CLAY_TEXT_CONFIG({
                            .fontSize = THEME_FONT_SIZE_UI,
                            .textColor = {THEME_TEXT_R, THEME_TEXT_G,
                                          THEME_TEXT_B, THEME_TEXT_A},
                            .fontId = CLAY_FONT_UI,
                        }));
                }

                CLAY(CLAY_ID("VP_BtnRotate"), {
                    .layout = {
                        .sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(20)},
                        .padding = {THEME_PADDING, THEME_PADDING, 0, 0},
                    },
                    .backgroundColor = rotate_bg,
                }) {
                    Clay_OnHover(on_mode_rotate_hover, ed);
                    CLAY_TEXT(CLAY_STRING("Rot(R)"),
                        CLAY_TEXT_CONFIG({
                            .fontSize = THEME_FONT_SIZE_UI,
                            .textColor = {THEME_TEXT_R, THEME_TEXT_G,
                                          THEME_TEXT_B, THEME_TEXT_A},
                            .fontId = CLAY_FONT_UI,
                        }));
                }

                CLAY(CLAY_ID("VP_BtnScale"), {
                    .layout = {
                        .sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(20)},
                        .padding = {THEME_PADDING, THEME_PADDING, 0, 0},
                    },
                    .backgroundColor = scale_bg,
                }) {
                    Clay_OnHover(on_mode_scale_hover, ed);
                    CLAY_TEXT(CLAY_STRING("Scale(S)"),
                        CLAY_TEXT_CONFIG({
                            .fontSize = THEME_FONT_SIZE_UI,
                            .textColor = {THEME_TEXT_R, THEME_TEXT_G,
                                          THEME_TEXT_B, THEME_TEXT_A},
                            .fontId = CLAY_FONT_UI,
                        }));
                }
            }
        }

        /* ---- Empty scene message (only when no entities and no FBO) ---- */
        if (ed->entities.active_count == 0 && tex == 0) {
            CLAY(CLAY_ID("Viewport_Empty"), {
                .layout = {
                    .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                    .childAlignment = {
                        .x = CLAY_ALIGN_X_CENTER,
                        .y = CLAY_ALIGN_Y_CENTER,
                    },
                },
            }) {
                CLAY_TEXT(CLAY_STRING("3D viewport not available"),
                    CLAY_TEXT_CONFIG({
                        .fontSize = THEME_FONT_SIZE_UI,
                        .textColor = {THEME_TEXT_DIM_R, THEME_TEXT_DIM_G,
                                      THEME_TEXT_DIM_B, THEME_TEXT_DIM_A},
                        .fontId = CLAY_FONT_UI,
                    }));
            }
        }
    }
}
