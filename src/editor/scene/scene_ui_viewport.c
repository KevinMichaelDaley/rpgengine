/**
 * @file scene_ui_viewport.c
 * @brief Viewport panel Clay UI layout: display 3D FBO textures.
 *
 * Iterates all active BSP leaf viewports and creates a floating
 * Clay container for each, displaying its FBO color texture.
 * The focused viewport shows a title bar overlay and mode toolbar.
 *
 * Non-static functions (1 / 4 limit):
 *   scene_ui_build_viewport
 */

#include "ferrum/editor/scene/scene_ui.h"
#include "ferrum/editor/scene/scene_main.h"
#include "ferrum/editor/scene/scene_panel.h"
#include "ferrum/editor/scene/scene_viewport_render.h"
#include "ferrum/editor/scene/viewport_bsp/viewport_bsp.h"
#include "ferrum/editor/scene/viewport_bsp/viewport_state.h"
#include "ferrum/editor/viewport/transform_basis.h"
#include "ferrum/editor/viewport/viewport_nav.h"
#include "ferrum/editor/viewport/viewport_shading.h"
#include "ferrum/editor/ui/clay_theme.h"
#include "ferrum/editor/ui/clay_fonts.h"
#include "clay.h"

#include <stdio.h>
#include <string.h>

/* ---- Constants ---- */

/** Button background color (slightly lighter than panel). */
static const Clay_Color COLOR_BTN_BG = {50, 52, 58, 255};


/** Per-viewport number label buffers (Clay defers rendering, so each
 *  viewport needs its own buffer to avoid overwriting). */
static char s_vp_num[VIEWPORT_MAX_COUNT][8];

/** Static buffer for mode label. */
static char s_mode_label[32];

/** Static buffer for basis label. */
static char s_basis_label[32];

/** Static buffer for nav mode label. */
static char s_nav_label[32];

/** Static buffer for shading mode label. */
static char s_shading_label[32];

/* ---- Internal: build a single viewport panel ---- */

/**
 * @brief Build the Clay UI for a single viewport leaf.
 *
 * @param ed        Editor context.
 * @param vs        Viewport state for this leaf.
 * @param vp_index      Viewport slot index (0-15) for unique Clay IDs.
 * @param display_num   Sequential display number (1-N) for title text.
 * @param is_focused    Whether this is the focused viewport.
 */
static void build_single_viewport(struct scene_editor *ed,
                                  viewport_state_t *vs,
                                  int vp_index,
                                  int display_num,
                                  bool is_focused) {
    panel_rect_t *r = &vs->rect;
    if (r->w <= 0 || r->h <= 0) return;

    float vw = (float)r->w;
    float vh = (float)r->h;

    /* Get this viewport's FBO texture handle. */
    uint32_t tex = vs->color_tex;

    CLAY(CLAY_IDI("VP_Root", vp_index), {
        .layout = {
            .sizing = {CLAY_SIZING_FIXED(vw), CLAY_SIZING_FIXED(vh)},
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
        },
        .floating = {
            .attachTo = CLAY_ATTACH_TO_ROOT,
            .offset = {(float)r->x, (float)r->y},
        },
        .image = {
            .imageData = tex > 0 ? &vs->color_tex : NULL,
        },
        .backgroundColor = tex == 0
            ? (Clay_Color){THEME_BG_VIEWPORT_R, THEME_BG_VIEWPORT_G,
                            THEME_BG_VIEWPORT_B, THEME_BG_VIEWPORT_A}
            : (Clay_Color){0, 0, 0, 0},
    }) {
        /* ---- Small viewport number in top-left corner ---- */
        snprintf(s_vp_num[vp_index], sizeof(s_vp_num[vp_index]),
                 "%d", display_num);

        CLAY(CLAY_IDI("VP_Num", vp_index), {
            .layout = {
                .sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)},
                .padding = {3, 3, 1, 1},
            },
        }) {
            Clay_String num_str = {
                .length = (int32_t)strlen(s_vp_num[vp_index]),
                .chars = s_vp_num[vp_index],
            };
            Clay__OpenTextElement(num_str,
                CLAY_TEXT_CONFIG({
                    .fontSize = THEME_FONT_SIZE_UI,
                    .textColor = is_focused
                        ? (Clay_Color){THEME_TEXT_R, THEME_TEXT_G,
                                       THEME_TEXT_B, 200}
                        : (Clay_Color){THEME_TEXT_DIM_R, THEME_TEXT_DIM_G,
                                       THEME_TEXT_DIM_B, 120},
                    .fontId = CLAY_FONT_UI,
                }));
        }

        /* ---- Mode toolbar (only on focused viewport) ---- */
        if (is_focused) {
            CLAY(CLAY_IDI("VP_ModeBar", vp_index), {
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
                /* Mode cycle button: shows current mode + (.) hotkey. */
                snprintf(s_mode_label, sizeof(s_mode_label), "%s(.)",
                         ui_mode_name(ed->ui.transform_mode));
                Clay_String mode_str = {
                    .length = (int32_t)strlen(s_mode_label),
                    .chars = s_mode_label,
                };
                CLAY(CLAY_IDI("VP_BtnMode", vp_index), {
                    .layout = {
                        .sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(20)},
                        .padding = {THEME_PADDING, THEME_PADDING, 0, 0},
                    },
                    .backgroundColor = COLOR_BTN_BG,
                }) {
                    Clay__OpenTextElement(mode_str,
                        CLAY_TEXT_CONFIG({
                            .fontSize = THEME_FONT_SIZE_UI,
                            .textColor = {THEME_TEXT_R, THEME_TEXT_G,
                                          THEME_TEXT_B, THEME_TEXT_A},
                            .fontId = CLAY_FONT_UI,
                        }));
                }

                /* Separator. */
                CLAY(CLAY_IDI("VP_BasisSep", vp_index), {
                    .layout = {
                        .sizing = {CLAY_SIZING_FIXED(1), CLAY_SIZING_FIXED(16)},
                    },
                    .backgroundColor = {THEME_TEXT_DIM_R, THEME_TEXT_DIM_G,
                                         THEME_TEXT_DIM_B, 80},
                }) {}

                /* Basis cycle button. */
                viewport_state_t *fvp = scene_focused_vp(ed);
                snprintf(s_basis_label, sizeof(s_basis_label), "%s(,)",
                         transform_basis_name(fvp->gizmo.basis));
                Clay_String basis_str = {
                    .length = (int32_t)strlen(s_basis_label),
                    .chars = s_basis_label,
                };
                CLAY(CLAY_IDI("VP_BtnBasis", vp_index), {
                    .layout = {
                        .sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(20)},
                        .padding = {THEME_PADDING, THEME_PADDING, 0, 0},
                    },
                    .backgroundColor = COLOR_BTN_BG,
                }) {
                    Clay__OpenTextElement(basis_str,
                        CLAY_TEXT_CONFIG({
                            .fontSize = THEME_FONT_SIZE_UI,
                            .textColor = {THEME_TEXT_R, THEME_TEXT_G,
                                          THEME_TEXT_B, THEME_TEXT_A},
                            .fontId = CLAY_FONT_UI,
                        }));
                }

                /* Separator. */
                CLAY(CLAY_IDI("VP_NavSep", vp_index), {
                    .layout = {
                        .sizing = {CLAY_SIZING_FIXED(1), CLAY_SIZING_FIXED(16)},
                    },
                    .backgroundColor = {THEME_TEXT_DIM_R, THEME_TEXT_DIM_G,
                                         THEME_TEXT_DIM_B, 80},
                }) {}

                /* Nav mode cycle button. */
                snprintf(s_nav_label, sizeof(s_nav_label), "%s(N)",
                         nav_mode_name(fvp->nav_mode));
                Clay_String nav_str = {
                    .length = (int32_t)strlen(s_nav_label),
                    .chars = s_nav_label,
                };
                CLAY(CLAY_IDI("VP_BtnNav", vp_index), {
                    .layout = {
                        .sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(20)},
                        .padding = {THEME_PADDING, THEME_PADDING, 0, 0},
                    },
                    .backgroundColor = COLOR_BTN_BG,
                }) {
                    Clay__OpenTextElement(nav_str,
                        CLAY_TEXT_CONFIG({
                            .fontSize = THEME_FONT_SIZE_UI,
                            .textColor = {THEME_TEXT_R, THEME_TEXT_G,
                                          THEME_TEXT_B, THEME_TEXT_A},
                            .fontId = CLAY_FONT_UI,
                        }));
                }

                /* Separator. */
                CLAY(CLAY_IDI("VP_ShadeSep", vp_index), {
                    .layout = {
                        .sizing = {CLAY_SIZING_FIXED(1), CLAY_SIZING_FIXED(16)},
                    },
                    .backgroundColor = {THEME_TEXT_DIM_R, THEME_TEXT_DIM_G,
                                         THEME_TEXT_DIM_B, 80},
                }) {}

                /* Shading mode cycle button. */
                snprintf(s_shading_label, sizeof(s_shading_label), "%s(/)",
                         shading_mode_name(fvp->shading_mode));
                Clay_String shade_str = {
                    .length = (int32_t)strlen(s_shading_label),
                    .chars = s_shading_label,
                };
                CLAY(CLAY_IDI("VP_BtnShade", vp_index), {
                    .layout = {
                        .sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(20)},
                        .padding = {THEME_PADDING, THEME_PADDING, 0, 0},
                    },
                    .backgroundColor = COLOR_BTN_BG,
                }) {
                    Clay__OpenTextElement(shade_str,
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
            CLAY(CLAY_IDI("VP_Empty", vp_index), {
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

/* ---- Public API ---- */

void scene_ui_build_viewport(struct scene_editor *ed,
                              const struct panel_rect *rect) {
    if (!ed || !rect || rect->w <= 0 || rect->h <= 0) {
        return;
    }

    /* Iterate all active BSP leaf viewports and build their Clay UI.
     * display_num counts sequentially (1, 2, 3, ...) for the title text. */
    uint8_t focused_vp = ed->vp_bsp.focused_viewport;
    int display_num = 0;
    for (int i = 0; i < VIEWPORT_MAX_COUNT; i++) {
        if (!ed->viewports[i].active) continue;
        if (ed->viewports[i].rect.w <= 0 || ed->viewports[i].rect.h <= 0)
            continue;

        display_num++;
        bool is_focused = ((uint8_t)i == focused_vp);
        build_single_viewport(ed, &ed->viewports[i], i, display_num,
                              is_focused);
    }
}
