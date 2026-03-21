/**
 * @file scene_ui_bone_list.c
 * @brief Collapsible bone list in the inspector for regular edit mode.
 *
 * Shows all bone names for an entity with a skeleton. Click to select,
 * shift+click to toggle. Mirrors the behavior of the prefab outliner's
 * bone rows but lives inside the inspector panel.
 *
 * Non-static functions (1 / 4 limit):
 *   scene_ui_build_bone_list
 */

#include "ferrum/editor/scene/scene_ui_bone_list.h"
#include "ferrum/editor/scene/scene_main.h"
#include "ferrum/editor/scene/scene_panel.h"
#include "ferrum/editor/edit_bone_selection.h"
#include "ferrum/editor/edit_skeleton_registry.h"
#include "ferrum/editor/ui/clay_theme.h"
#include "ferrum/editor/ui/clay_fonts.h"
#include "ferrum/entity/entity_attrs.h"
#include "clay.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

/* Max visible bone rows. */
#define BONE_LIST_VIS_MAX 256

/* Static name buffers for Clay_String lifetime. */
static char s_bone_names[BONE_LIST_VIS_MAX][80];

/* Header text buffer. */
static char s_header_buf[64];

/* ---- Click context ---- */

typedef struct {
    scene_editor_t *ed;
    uint32_t        entity_id;
    uint32_t        bone_index;
} bone_row_ctx_t;

static bone_row_ctx_t s_bone_row_ctx[BONE_LIST_VIS_MAX];

/**
 * @brief Hover callback for bone row click.
 * Plain click: select single bone. Shift+click: toggle.
 */
static void on_bone_row_hover(Clay_ElementId id, Clay_PointerData data,
                               void *user_data) {
    (void)id;
    if (data.state != CLAY_POINTER_DATA_PRESSED_THIS_FRAME) return;

    bone_row_ctx_t *ctx = (bone_row_ctx_t *)user_data;
    if (!ctx || !ctx->ed) return;

    SDL_Keymod mod = SDL_GetModState();
    if (mod & KMOD_SHIFT) {
        edit_bone_selection_toggle(&ctx->ed->bone_selection,
                                    ctx->entity_id, ctx->bone_index);
    } else {
        edit_bone_selection_clear(&ctx->ed->bone_selection);
        edit_bone_selection_add(&ctx->ed->bone_selection,
                                ctx->entity_id, ctx->bone_index);
    }
}

/** Check if element at [y, y+h) is visible in scroll window. */
static bool is_visible_(float y, float h, float scroll_px, float visible_h) {
    return (y + h) > scroll_px && y < (scroll_px + visible_h);
}

void scene_ui_build_bone_list(scene_editor_t *ed,
                               uint32_t entity_id,
                               float *y_cursor,
                               float scroll_px,
                               float visible_h,
                               uint32_t *clay_idx) {
    if (!ed || ed->prefab_mode.active) return;

    /* Look up entity's skeleton path. */
    const edit_entity_t *ent = edit_entity_store_get(&ed->entities, entity_id);
    if (!ent || !ent->active) return;

    uint8_t at = 0, as = 0;
    const void *sp = entity_attrs_get(&ent->attrs,
                                       SCRIPT_KEY_SKEL_PATH, &at, &as);
    if (!sp || at != SCRIPT_ATTR_STR) return;
    const char *spath = (const char *)sp;
    if (spath[0] == '\0') return;

    /* Extract filename for registry lookup. */
    const char *fname = spath;
    for (const char *p = spath; *p; p++) {
        if (*p == '/') fname = p + 1;
    }

    const edit_skeleton_entry_t *entry =
        edit_skeleton_registry_get(&ed->skeleton_registry, fname);
    if (!entry || entry->skel.joint_count == 0) return;

    const skeleton_def_t *skel = &entry->skel;
    const float row_h = (float)THEME_ROW_HEIGHT;
    const float header_h = 28.0f;

    /* Colors matching prefab outliner. */
    Clay_Color text_bright = {THEME_TEXT_R, THEME_TEXT_G,
                               THEME_TEXT_B, THEME_TEXT_A};
    Clay_Color text_dim = {THEME_TEXT_DIM_R, THEME_TEXT_DIM_G,
                            THEME_TEXT_DIM_B, THEME_TEXT_DIM_A};
    Clay_Color bg_none = {0, 0, 0, 0};
    Clay_Color bg_selected = {THEME_ACCENT_R, THEME_ACCENT_G,
                               THEME_ACCENT_B, 80};
    Clay_Color header_bg = {THEME_ACCENT_R, THEME_ACCENT_G,
                             THEME_ACCENT_B, 40};

    /* ---- "Bones" section header ---- */
    if (is_visible_(*y_cursor, header_h, scroll_px, visible_h)) {
        int header_len = snprintf(s_header_buf, sizeof(s_header_buf),
                                   "Bones (%u)", skel->joint_count);
        CLAY(CLAY_IDI("BoneListHeader", (*clay_idx)++), {
            .layout = {
                .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(header_h)},
                .padding = {THEME_PADDING_SMALL, THEME_PADDING_SMALL, 0, 0},
                .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
            },
            .backgroundColor = header_bg,
        }) {
            Clay_String hdr_str = {.chars = s_header_buf,
                                    .length = header_len};
            Clay__OpenTextElement(hdr_str,
                CLAY_TEXT_CONFIG({
                    .fontSize = THEME_FONT_SIZE_UI,
                    .textColor = text_bright,
                    .fontId = CLAY_FONT_UI,
                }));
        }
    } else {
        (*clay_idx)++;
    }
    *y_cursor += header_h;

    /* ---- Bone rows ---- */
    uint32_t joint_count = skel->joint_count;
    if (joint_count > BONE_LIST_VIS_MAX) joint_count = BONE_LIST_VIS_MAX;

    for (uint32_t bi = 0; bi < joint_count; bi++) {
        if (is_visible_(*y_cursor, row_h, scroll_px, visible_h)) {
            /* Format bone name. */
            const char *bone_name = skel->joint_names[bi];
            int name_len = snprintf(s_bone_names[bi], sizeof(s_bone_names[bi]),
                                     "  %s", bone_name);

            /* Check if this bone is selected. */
            bool selected = edit_bone_selection_contains(
                &ed->bone_selection, entity_id, bi);

            Clay_Color bg = selected ? bg_selected : bg_none;
            Clay_Color text_color = selected ? text_bright : text_dim;

            /* Set up click context. */
            s_bone_row_ctx[bi].ed = ed;
            s_bone_row_ctx[bi].entity_id = entity_id;
            s_bone_row_ctx[bi].bone_index = bi;

            CLAY(CLAY_IDI("BoneListRow", (*clay_idx)++), {
                .layout = {
                    .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(row_h)},
                    .padding = {THEME_INDENT, THEME_PADDING_SMALL, 0, 0},
                    .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                },
                .backgroundColor = bg,
            }) {
                Clay_OnHover(on_bone_row_hover, &s_bone_row_ctx[bi]);
                Clay_String row_str = {
                    .chars = s_bone_names[bi],
                    .length = name_len,
                };
                Clay__OpenTextElement(row_str,
                    CLAY_TEXT_CONFIG({
                        .fontSize = THEME_FONT_SIZE_UI,
                        .textColor = text_color,
                        .fontId = CLAY_FONT_UI,
                    }));
            }
        } else {
            (*clay_idx)++;
        }
        *y_cursor += row_h;
    }
}
