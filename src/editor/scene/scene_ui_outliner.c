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
#include "ferrum/editor/ui/clay_theme.h"
#include "ferrum/editor/ui/clay_fonts.h"
#include "clay.h"

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

/** Button background color (slightly lighter than panel). */
static const Clay_Color COLOR_BTN_BG = {50, 52, 58, 255};

/** Panel background color. */
static const Clay_Color COLOR_PANEL_BG = {
    THEME_BG_PANEL_R, THEME_BG_PANEL_G,
    THEME_BG_PANEL_B, THEME_BG_PANEL_A
};

/** Title bar background (accent with reduced alpha). */
static const Clay_Color COLOR_TITLE_BG = {
    THEME_ACCENT_R, THEME_ACCENT_G, THEME_ACCENT_B, 60
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

/** @brief Hover callback for the "Box" spawn button. */
static void on_spawn_box_hover(Clay_ElementId id, Clay_PointerData data,
                                void *user) {
    (void)id;
    scene_editor_t *ed = (scene_editor_t *)user;
    if (data.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
        ed->ui.action = UI_ACTION_SPAWN_BOX;
    }
}

/** @brief Hover callback for the "Sphere" spawn button. */
static void on_spawn_sphere_hover(Clay_ElementId id, Clay_PointerData data,
                                   void *user) {
    (void)id;
    scene_editor_t *ed = (scene_editor_t *)user;
    if (data.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
        ed->ui.action = UI_ACTION_SPAWN_SPHERE;
    }
}

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

/** @brief Hover callback for the "Capsule" spawn button. */
static void on_spawn_capsule_hover(Clay_ElementId id, Clay_PointerData data,
                                    void *user) {
    (void)id;
    scene_editor_t *ed = (scene_editor_t *)user;
    if (data.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
        ed->ui.action = UI_ACTION_SPAWN_CAPSULE;
    }
}

/**
 * @brief Hover callback for an entity row click (select/deselect toggle).
 *
 * The userdata points into s_entity_click_ctx, which carries both the
 * editor pointer and the target entity ID.
 */
static void on_entity_row_hover(Clay_ElementId id, Clay_PointerData data,
                                 void *user) {
    (void)id;
    struct {
        scene_editor_t *ed;
        uint32_t        entity_id;
    } *ctx = user;

    if (data.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
        bool selected = edit_selection_contains(&ctx->ed->selection,
                                                 ctx->entity_id);
        if (selected) {
            ctx->ed->ui.action = UI_ACTION_DESELECT_ENTITY;
        } else {
            ctx->ed->ui.action = UI_ACTION_SELECT_ENTITY;
        }
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
        .floating = {
            .attachTo = CLAY_ATTACH_TO_ROOT,
            .offset = {(float)rect->x, (float)rect->y},
        },
    }) {
        /* ---- Title bar ---- */
        CLAY(CLAY_ID("Outliner_Title"), {
            .layout = {
                .sizing = {CLAY_SIZING_GROW(0),
                           CLAY_SIZING_FIXED(THEME_ROW_HEIGHT)},
                .padding = {THEME_PADDING_SMALL, THEME_PADDING_SMALL, 0, 0},
            },
            .backgroundColor = COLOR_TITLE_BG,
        }) {
            CLAY_TEXT(CLAY_STRING("Outliner"),
                CLAY_TEXT_CONFIG({
                    .fontSize = THEME_FONT_SIZE_UI,
                    .textColor = {THEME_TEXT_R, THEME_TEXT_G,
                                  THEME_TEXT_B, THEME_TEXT_A},
                    .fontId = CLAY_FONT_UI,
                }));
        }

        /* ---- Toolbar row: Create buttons ---- */
        CLAY(CLAY_ID("Outliner_Toolbar"), {
            .layout = {
                .sizing = {CLAY_SIZING_GROW(0),
                           CLAY_SIZING_FIXED(THEME_ROW_HEIGHT + THEME_PADDING)},
                .layoutDirection = CLAY_LEFT_TO_RIGHT,
                .padding = {THEME_PADDING_SMALL, THEME_PADDING_SMALL,
                            THEME_PADDING_SMALL, THEME_PADDING_SMALL},
                .childGap = THEME_PADDING_SMALL,
                .childAlignment = {
                    .y = CLAY_ALIGN_Y_CENTER,
                },
            },
        }) {
            /* "Create:" label */
            CLAY_TEXT(CLAY_STRING("Create:"),
                CLAY_TEXT_CONFIG({
                    .fontSize = THEME_FONT_SIZE_UI,
                    .textColor = {THEME_TEXT_DIM_R, THEME_TEXT_DIM_G,
                                  THEME_TEXT_DIM_B, THEME_TEXT_DIM_A},
                    .fontId = CLAY_FONT_UI,
                }));

            /* Box button */
            CLAY(CLAY_ID("Outliner_BtnBox"), {
                .layout = {
                    .sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(20)},
                    .padding = {THEME_PADDING, THEME_PADDING, 0, 0},
                },
                .backgroundColor = COLOR_BTN_BG,
            }) {
                Clay_OnHover(on_spawn_box_hover, ed);
                CLAY_TEXT(CLAY_STRING("Box"),
                    CLAY_TEXT_CONFIG({
                        .fontSize = THEME_FONT_SIZE_UI,
                        .textColor = {THEME_TEXT_R, THEME_TEXT_G,
                                      THEME_TEXT_B, THEME_TEXT_A},
                        .fontId = CLAY_FONT_UI,
                    }));
            }

            /* Sphere button */
            CLAY(CLAY_ID("Outliner_BtnSphere"), {
                .layout = {
                    .sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(20)},
                    .padding = {THEME_PADDING, THEME_PADDING, 0, 0},
                },
                .backgroundColor = COLOR_BTN_BG,
            }) {
                Clay_OnHover(on_spawn_sphere_hover, ed);
                CLAY_TEXT(CLAY_STRING("Sphere"),
                    CLAY_TEXT_CONFIG({
                        .fontSize = THEME_FONT_SIZE_UI,
                        .textColor = {THEME_TEXT_R, THEME_TEXT_G,
                                      THEME_TEXT_B, THEME_TEXT_A},
                        .fontId = CLAY_FONT_UI,
                    }));
            }

            /* Capsule button */
            CLAY(CLAY_ID("Outliner_BtnCapsule"), {
                .layout = {
                    .sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(20)},
                    .padding = {THEME_PADDING, THEME_PADDING, 0, 0},
                },
                .backgroundColor = COLOR_BTN_BG,
            }) {
                Clay_OnHover(on_spawn_capsule_hover, ed);
                CLAY_TEXT(CLAY_STRING("Capsule"),
                    CLAY_TEXT_CONFIG({
                        .fontSize = THEME_FONT_SIZE_UI,
                        .textColor = {THEME_TEXT_R, THEME_TEXT_G,
                                      THEME_TEXT_B, THEME_TEXT_A},
                        .fontId = CLAY_FONT_UI,
                    }));
            }
        }

        /* ---- Toolbar row: Mode buttons ---- */
        CLAY(CLAY_ID("Outliner_ModeBar"), {
            .layout = {
                .sizing = {CLAY_SIZING_GROW(0),
                           CLAY_SIZING_FIXED(THEME_ROW_HEIGHT + THEME_PADDING)},
                .layoutDirection = CLAY_LEFT_TO_RIGHT,
                .padding = {THEME_PADDING_SMALL, THEME_PADDING_SMALL,
                            THEME_PADDING_SMALL, THEME_PADDING_SMALL},
                .childGap = THEME_PADDING_SMALL,
                .childAlignment = {
                    .y = CLAY_ALIGN_Y_CENTER,
                },
            },
        }) {
            /* "Mode:" label */
            CLAY_TEXT(CLAY_STRING("Mode:"),
                CLAY_TEXT_CONFIG({
                    .fontSize = THEME_FONT_SIZE_UI,
                    .textColor = {THEME_TEXT_DIM_R, THEME_TEXT_DIM_G,
                                  THEME_TEXT_DIM_B, THEME_TEXT_DIM_A},
                    .fontId = CLAY_FONT_UI,
                }));

            /* Active mode gets the selection highlight color. */
            Clay_Color translate_bg = (ed->ui.transform_mode == UI_MODE_TRANSLATE)
                                        ? COLOR_SELECTION_BG : COLOR_BTN_BG;
            Clay_Color rotate_bg = (ed->ui.transform_mode == UI_MODE_ROTATE)
                                     ? COLOR_SELECTION_BG : COLOR_BTN_BG;
            Clay_Color scale_bg = (ed->ui.transform_mode == UI_MODE_SCALE)
                                    ? COLOR_SELECTION_BG : COLOR_BTN_BG;

            /* Translate button (G) */
            CLAY(CLAY_ID("Outliner_BtnTranslate"), {
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

            /* Rotate button (R) */
            CLAY(CLAY_ID("Outliner_BtnRotate"), {
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

            /* Scale button (S) */
            CLAY(CLAY_ID("Outliner_BtnScale"), {
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

        /* ---- Entity list ---- */
        CLAY(CLAY_ID("Outliner_EntityList"), {
            .layout = {
                .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
                .childGap = 1,
            },
        }) {
            uint32_t visible_index = 0;
            uint32_t capacity = ed->entities.capacity;

            for (uint32_t i = 0; i < capacity; ++i) {
                if (visible_index >= OUTLINER_MAX_VISIBLE) {
                    break;
                }

                const edit_entity_t *ent =
                    edit_entity_store_get(&ed->entities, i);
                if (!ent) {
                    continue;
                }

                /* Determine if this entity is selected. */
                bool selected = edit_selection_contains(&ed->selection, i);

                /* Set up the click context for this row. */
                s_entity_click_ctx[visible_index].ed = ed;
                s_entity_click_ctx[visible_index].entity_id = i;

                /* Choose row background based on selection state. */
                Clay_Color row_bg = selected ? COLOR_SELECTION_BG
                                             : COLOR_ROW_BG;

                /* Build the entity row element. */
                CLAY(CLAY_IDI("Outliner_Row", visible_index), {
                    .layout = {
                        .sizing = {CLAY_SIZING_GROW(0),
                                   CLAY_SIZING_FIXED(THEME_ROW_HEIGHT)},
                        .layoutDirection = CLAY_LEFT_TO_RIGHT,
                        .padding = {THEME_PADDING_SMALL, THEME_PADDING_SMALL,
                                    0, 0},
                        .childGap = THEME_PADDING_SMALL,
                        .childAlignment = {
                            .y = CLAY_ALIGN_Y_CENTER,
                        },
                    },
                    .backgroundColor = row_bg,
                }) {
                    /* Attach hover callback for click-to-select/deselect. */
                    Clay_OnHover(on_entity_row_hover,
                                 &s_entity_click_ctx[visible_index]);

                    /* Entity name text (runtime string). */
                    const char *name = ent->name;
                    int32_t name_len = (int32_t)strlen(name);

                    /* If the entity has no name, use a static fallback buffer. */
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
                            .textColor = {THEME_TEXT_R, THEME_TEXT_G,
                                          THEME_TEXT_B, THEME_TEXT_A},
                            .fontId = CLAY_FONT_UI,
                        }));

                    /* Type tag (e.g. "[box]") as dim text. */
                    const char *tag = type_tag_string(ent->type);
                    Clay_String tag_str = {
                        .length = (int32_t)strlen(tag),
                        .chars = tag,
                    };
                    Clay__OpenTextElement(tag_str,
                        CLAY_TEXT_CONFIG({
                            .fontSize = THEME_FONT_SIZE_UI,
                            .textColor = {THEME_TEXT_DIM_R, THEME_TEXT_DIM_G,
                                          THEME_TEXT_DIM_B, THEME_TEXT_DIM_A},
                            .fontId = CLAY_FONT_UI,
                        }));
                }

                visible_index++;
            }
        }
    }
}
