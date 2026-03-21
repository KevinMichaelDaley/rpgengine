/**
 * @file scene_ui_bone_inspector.c
 * @brief Bone property display in the inspector panel.
 *
 * When bones are selected, displays bone index, head/tail positions,
 * and collider shape properties.
 *
 * Non-static functions (1 / 4 limit):
 *   scene_ui_build_bone_inspector
 */

#include "ferrum/editor/scene/scene_ui_bone_inspector.h"
#include "ferrum/editor/scene/scene_main.h"
#include "ferrum/editor/scene/scene_panel.h"
#include "ferrum/editor/ui/clay_theme.h"
#include "ferrum/editor/ui/clay_fonts.h"
#include "ferrum/editor/edit_bone_selection.h"
#include "ferrum/editor/edit_skeleton_registry.h"
#include "ferrum/editor/scene/scene_viewport_bone_overlay.h"
#include "ferrum/entity/entity_attrs.h"

#include "clay.h"
#include <stdio.h>
#include <string.h>

/* ---- Static text buffers for bone properties ---- */

/** Buffer slots for bone inspector:
 * [0] "Bone: <index> (<name>)"
 * [1] "  Count: <n>"
 * [2..4] Head X/Y/Z
 * [5..7] Tail X/Y/Z
 * [8] "  Shape: <type>"
 * [9] "  Mass: <val>"
 */
#define BONE_BUF_COUNT 10
#define BONE_BUF_SIZE  128
static char s_bone_bufs[BONE_BUF_COUNT][BONE_BUF_SIZE];
static int32_t s_bone_lens[BONE_BUF_COUNT];

/** @brief Check if element at [y, y+h) is visible in scroll window. */
static bool is_visible_(float y, float h, float scroll_px, float visible_h) {
    return (y + h) > scroll_px && y < (scroll_px + visible_h);
}

/** @brief Emit a single text row using Clay. */
static void emit_text_row_(uint32_t *clay_idx, float *y, float row_h,
                            float scroll_px, float visible_h,
                            int buf_slot, Clay_Color color, uint16_t font_id) {
    if (is_visible_(*y, row_h, scroll_px, visible_h)) {
        Clay_String text = {.length = s_bone_lens[buf_slot],
                            .chars = s_bone_bufs[buf_slot]};
        CLAY(CLAY_IDI("BoneInspRow", *clay_idx), {
            .layout = {
                .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(row_h)},
                .padding = {THEME_PADDING, THEME_PADDING, 0, 0},
            },
        }) {
            Clay__OpenTextElement(text,
                CLAY_TEXT_CONFIG({
                    .fontSize = THEME_FONT_SIZE_MONO,
                    .textColor = color,
                    .fontId = font_id,
                }));
        }
    }
    *y += row_h;
    (*clay_idx)++;
}

void scene_ui_build_bone_inspector(scene_editor_t *ed,
                                    float *y_cursor,
                                    float scroll_px,
                                    float visible_h,
                                    uint32_t *clay_idx) {
    if (!ed || !y_cursor || !clay_idx) return;

    uint32_t bone_count = edit_bone_selection_count(&ed->bone_selection);
    if (bone_count == 0) return;

    /* Look up the skeleton for the active entity. */
    if (ed->active_object_id == EDIT_ENTITY_INVALID_ID) return;
    const edit_entity_t *ent = edit_entity_store_get(
        &ed->entities, ed->active_object_id);
    if (!ent || !ent->active) return;

    uint8_t at = 0, as = 0;
    const void *sp = entity_attrs_get(&ent->attrs, SCRIPT_KEY_SKEL_PATH,
                                       &at, &as);
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
    float row_h = (float)THEME_ROW_HEIGHT;
    float sep_h = (float)2; /* Separator height. */

    Clay_Color color_text = {THEME_TEXT_R, THEME_TEXT_G,
                              THEME_TEXT_B, THEME_TEXT_A};
    Clay_Color color_accent = {THEME_ACCENT_R, THEME_ACCENT_G,
                                THEME_ACCENT_B, THEME_ACCENT_A};
    Clay_Color color_bone = {255, 220, 80, 255}; /* Bone highlight color. */

    /* ---- Separator ---- */
    if (is_visible_(*y_cursor, sep_h, scroll_px, visible_h)) {
        CLAY(CLAY_IDI("BoneInspRow", *clay_idx), {
            .layout = {
                .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(sep_h)},
                .padding = {THEME_PADDING, THEME_PADDING,
                            THEME_PADDING_SMALL, THEME_PADDING_SMALL},
            },
            .backgroundColor = {THEME_ACCENT_R, THEME_ACCENT_G,
                                 THEME_ACCENT_B, 40},
        }) {}
    }
    *y_cursor += sep_h;
    (*clay_idx)++;

    /* ---- Section header ---- */
    {
        int n = snprintf(s_bone_bufs[1], BONE_BUF_SIZE,
                         "Bones (%u selected)", bone_count);
        if (n < 0) n = 0;
        if (n >= BONE_BUF_SIZE) n = BONE_BUF_SIZE - 1;
        s_bone_lens[1] = (int32_t)n;
        emit_text_row_(clay_idx, y_cursor, row_h, scroll_px, visible_h,
                        1, color_accent, CLAY_FONT_UI);
    }

    /* Show details for the first selected bone. */
    uint32_t first_bone_count = 0;
    const uint32_t *selected_bones =
        edit_bone_selection_bones(&ed->bone_selection, &first_bone_count);
    if (!selected_bones || first_bone_count == 0) return;

    uint32_t bi = selected_bones[0];
    if (bi >= skel->joint_count) return;

    /* Bone name/index. */
    {
        const char *bone_name = "";
        if (skel->joint_names) {
            bone_name = skel->joint_names[bi];
        }
        int n = snprintf(s_bone_bufs[0], BONE_BUF_SIZE,
                         "Bone %u: %s", bi,
                         (bone_name[0] != '\0') ? bone_name : "(unnamed)");
        if (n < 0) n = 0;
        if (n >= BONE_BUF_SIZE) n = BONE_BUF_SIZE - 1;
        s_bone_lens[0] = (int32_t)n;
        emit_text_row_(clay_idx, y_cursor, row_h, scroll_px, visible_h,
                        0, color_bone, CLAY_FONT_UI);
    }

    /* Head position. */
    {
        float hx = skel->rest_world[bi].m[12];
        float hy = skel->rest_world[bi].m[13];
        float hz = skel->rest_world[bi].m[14];
        static const char *axes[3] = {"  Head X", "  Head Y", "  Head Z"};
        float vals[3] = {hx, hy, hz};
        for (int i = 0; i < 3; i++) {
            int n = snprintf(s_bone_bufs[2 + i], BONE_BUF_SIZE,
                             "%s: %.3f", axes[i], (double)vals[i]);
            if (n < 0) n = 0;
            if (n >= BONE_BUF_SIZE) n = BONE_BUF_SIZE - 1;
            s_bone_lens[2 + i] = (int32_t)n;
            emit_text_row_(clay_idx, y_cursor, row_h, scroll_px, visible_h,
                            2 + i, color_text, CLAY_FONT_MONO);
        }
    }

    /* Tail position. */
    if (skel->tail_positions) {
        float tx = skel->tail_positions[bi * 3 + 0];
        float ty = skel->tail_positions[bi * 3 + 1];
        float tz = skel->tail_positions[bi * 3 + 2];
        static const char *axes[3] = {"  Tail X", "  Tail Y", "  Tail Z"};
        float vals[3] = {tx, ty, tz};
        for (int i = 0; i < 3; i++) {
            int n = snprintf(s_bone_bufs[5 + i], BONE_BUF_SIZE,
                             "%s: %.3f", axes[i], (double)vals[i]);
            if (n < 0) n = 0;
            if (n >= BONE_BUF_SIZE) n = BONE_BUF_SIZE - 1;
            s_bone_lens[5 + i] = (int32_t)n;
            emit_text_row_(clay_idx, y_cursor, row_h, scroll_px, visible_h,
                            5 + i, color_text, CLAY_FONT_MONO);
        }
    }

    /* Collider shape (if collider data exists). */
    if (skel->colliders) {
        static const char *shape_names[] = {
            "none", "capsule", "box", "sphere", "hull", "point"
        };
        uint32_t shape = skel->colliders[bi].shape_type;
        const char *sname = (shape < 6) ? shape_names[shape] : "unknown";
        int n = snprintf(s_bone_bufs[8], BONE_BUF_SIZE,
                         "  Shape: %s", sname);
        if (n < 0) n = 0;
        if (n >= BONE_BUF_SIZE) n = BONE_BUF_SIZE - 1;
        s_bone_lens[8] = (int32_t)n;
        emit_text_row_(clay_idx, y_cursor, row_h, scroll_px, visible_h,
                        8, color_text, CLAY_FONT_MONO);

        n = snprintf(s_bone_bufs[9], BONE_BUF_SIZE,
                     "  Mass: %.3f", (double)skel->colliders[bi].mass);
        if (n < 0) n = 0;
        if (n >= BONE_BUF_SIZE) n = BONE_BUF_SIZE - 1;
        s_bone_lens[9] = (int32_t)n;
        emit_text_row_(clay_idx, y_cursor, row_h, scroll_px, visible_h,
                        9, color_text, CLAY_FONT_MONO);
    }
}
