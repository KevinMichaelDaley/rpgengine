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
#include "ferrum/editor/scene/skeleton_mode.h"
#include "ferrum/editor/ui/clay_theme.h"
#include "ferrum/editor/ui/clay_fonts.h"
#include "ferrum/editor/ui/inline_field.h"
#include "ferrum/editor/edit_bone_selection.h"
#include "ferrum/editor/edit_skeleton_registry.h"
#include "ferrum/editor/scene/scene_viewport_bone_overlay.h"
#include "ferrum/entity/entity_attrs.h"
#include "ferrum/animation/bone_collider.h"

#include "clay.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- Forward declarations ---- */
static bool is_visible_(float y, float h, float scroll_px, float visible_h);
static void emit_text_row_(uint32_t *clay_idx, float *y, float row_h,
                            float scroll_px, float visible_h,
                            int buf_slot, Clay_Color color, uint16_t font_id);

/* ---- Static text buffers for bone properties ---- */

/** Buffer slots for bone inspector:
 * [0] "Bone: <index> (<name>)"
 * [1] "  Count: <n>"
 * [2..4] Head X/Y/Z
 * [5..7] Tail X/Y/Z
 * [8] "  Shape: <type>"
 * [9] "  Mass: <val>"
 */
#define BONE_BUF_COUNT 24
#define BONE_BUF_SIZE  128
static char s_bone_bufs[BONE_BUF_COUNT][BONE_BUF_SIZE];
static int32_t s_bone_lens[BONE_BUF_COUNT];

/* ---- Collision inspector inline fields ---- */

/** @brief Field IDs for collision parameter editing. */
enum {
    COLLIDER_FIELD_PARAM0 = 100, /* params[0]: radius / half_x */
    COLLIDER_FIELD_PARAM1,       /* params[1]: height / half_y */
    COLLIDER_FIELD_PARAM2,       /* params[2]: axis / half_z */
    COLLIDER_FIELD_MASS,         /* mass */
    COLLIDER_FIELD_GROUP,        /* collision_group */
};

/** @brief Inline field states for collision parameters. */
static inline_field_state_t s_coll_fields[5];

/** @brief Click context for shape type cycling. */
static struct {
    scene_editor_t *ed;
    uint32_t        bone_index;
} s_shape_click_ctx;

/** @brief Click context for collision field editing. */
static struct {
    scene_editor_t *ed;
    uint32_t        bone_index;
    uint32_t        field_id;
    float          *target;     /* Pointer to the float being edited. */
} s_field_click_ctx[5];

static void on_shape_cycle_(Clay_ElementId id, Clay_PointerData data, void *user) {
    (void)id; (void)user;
    if (data.state != CLAY_POINTER_DATA_PRESSED_THIS_FRAME) return;

    scene_editor_t *ed = s_shape_click_ctx.ed;
    uint32_t bi = s_shape_click_ctx.bone_index;
    if (!ed) return;

    /* Get mutable skeleton (working copy in skeleton mode, else registry). */
    skeleton_def_t *skel = NULL;
    if (ed->skeleton_mode.active) {
        skel = skeleton_mode_get_work_skel(&ed->skeleton_mode);
    } else {
        /* Entity-based: get mutable registry skeleton. */
        if (ed->active_object_id != EDIT_ENTITY_INVALID_ID) {
            const edit_entity_t *ent = edit_entity_store_get(
                &ed->entities, ed->active_object_id);
            if (ent) {
                uint8_t at = 0, as = 0;
                const void *sp = entity_attrs_get(&ent->attrs,
                    SCRIPT_KEY_SKEL_PATH, &at, &as);
                if (sp && at == SCRIPT_ATTR_STR) {
                    const char *fn = (const char *)sp;
                    for (const char *p = fn; *p; p++)
                        if (*p == '/') fn = p + 1;
                    edit_skeleton_entry_t *se =
                        edit_skeleton_registry_get_mut(&ed->skeleton_registry, fn);
                    if (se) skel = &se->skel;
                }
            }
        }
    }
    if (!skel || bi >= skel->joint_count) return;

    /* Allocate colliders if needed. */
    if (!skel->colliders) {
        skel->colliders = (bone_collider_desc_t *)calloc(
            skel->joint_count, sizeof(bone_collider_desc_t));
        if (!skel->colliders) return;
    }

    /* Compute bone length for default params. */
    float bone_len = 0.2f;
    if (skel->tail_positions && skel->rest_world) {
        float hx = skel->rest_world[bi].m[12];
        float hy = skel->rest_world[bi].m[13];
        float hz = skel->rest_world[bi].m[14];
        float tx = skel->tail_positions[bi * 3 + 0];
        float ty = skel->tail_positions[bi * 3 + 1];
        float tz = skel->tail_positions[bi * 3 + 2];
        float dx = tx - hx, dy = ty - hy, dz = tz - hz;
        float l = sqrtf(dx*dx + dy*dy + dz*dz);
        if (l > 0.01f) bone_len = l;
    }
    float bone_radius = bone_len * 0.15f;
    if (bone_radius < 0.01f) bone_radius = 0.01f;

    /* Cycle: none → capsule → box → sphere → none. */
    uint32_t shape = skel->colliders[bi].shape_type;
    shape = (shape + 1) % 4;
    skel->colliders[bi].shape_type = shape;

    /* Default params derived from bone geometry. */
    if (shape == BONE_COLLIDER_CAPSULE) {
        skel->colliders[bi].params[0] = bone_radius;
        skel->colliders[bi].params[1] = bone_len;
        skel->colliders[bi].params[2] = 1.0f;
    } else if (shape == BONE_COLLIDER_BOX) {
        skel->colliders[bi].params[0] = bone_radius;
        skel->colliders[bi].params[1] = bone_len * 0.5f;
        skel->colliders[bi].params[2] = bone_radius;
    } else if (shape == BONE_COLLIDER_SPHERE) {
        skel->colliders[bi].params[0] = bone_len * 0.5f;
    }

    if (ed->skeleton_mode.active) {
        ed->skeleton_mode.dirty = true;
    }
}

static void on_field_click_(Clay_ElementId id, Clay_PointerData data, void *user) {
    (void)id;
    if (data.state != CLAY_POINTER_DATA_PRESSED_THIS_FRAME) return;

    struct {
        scene_editor_t *ed;
        uint32_t bone_index;
        uint32_t field_id;
        float *target;
    } *ctx = user;

    if (!ctx->ed || !ctx->target) return;

    uint32_t slot = ctx->field_id - COLLIDER_FIELD_PARAM0;
    if (slot >= 5) return;

    inline_field_begin(&ctx->ed->field_ctx, &s_coll_fields[slot],
                        ctx->field_id, *ctx->target);
    s_coll_fields[slot].target = ctx->target;
}

/** @brief Emit a clickable collision parameter row. */
static void emit_collider_field_(scene_editor_t *ed, uint32_t *clay_idx,
                                   float *y, float row_h,
                                   float scroll_px, float visible_h,
                                   int buf_slot, const char *label,
                                   float value, uint32_t field_id,
                                   float *target, uint32_t bone_index) {
    uint32_t slot = field_id - COLLIDER_FIELD_PARAM0;

    /* Check if this field is being edited. */
    bool editing = (slot < 5 && s_coll_fields[slot].active &&
                    s_coll_fields[slot].field_id == field_id);

    if (editing) {
        int n = snprintf(s_bone_bufs[buf_slot], BONE_BUF_SIZE,
                         "  %s: %s|", label, s_coll_fields[slot].buf);
        if (n < 0) n = 0;
        if (n >= BONE_BUF_SIZE) n = BONE_BUF_SIZE - 1;
        s_bone_lens[buf_slot] = (int32_t)n;
    } else {
        int n = snprintf(s_bone_bufs[buf_slot], BONE_BUF_SIZE,
                         "  %s: %.4g", label, (double)value);
        if (n < 0) n = 0;
        if (n >= BONE_BUF_SIZE) n = BONE_BUF_SIZE - 1;
        s_bone_lens[buf_slot] = (int32_t)n;
    }

    Clay_Color color = editing
        ? (Clay_Color){255, 220, 80, 255}
        : (Clay_Color){THEME_TEXT_R, THEME_TEXT_G, THEME_TEXT_B, THEME_TEXT_A};

    if (is_visible_(*y, row_h, scroll_px, visible_h)) {
        Clay_String text = {.length = s_bone_lens[buf_slot],
                            .chars = s_bone_bufs[buf_slot]};

        /* Set up click context. */
        if (slot < 5) {
            s_field_click_ctx[slot].ed = ed;
            s_field_click_ctx[slot].bone_index = bone_index;
            s_field_click_ctx[slot].field_id = field_id;
            s_field_click_ctx[slot].target = target;
        }

        CLAY(CLAY_IDI("BoneInspRow", *clay_idx), {
            .layout = {
                .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(row_h)},
                .padding = {THEME_PADDING, THEME_PADDING, 0, 0},
            },
            .backgroundColor = editing
                ? (Clay_Color){THEME_ACCENT_R, THEME_ACCENT_G, THEME_ACCENT_B, 30}
                : (Clay_Color){0, 0, 0, 0},
        }) {
            if (slot < 5) {
                Clay_OnHover(on_field_click_, &s_field_click_ctx[slot]);
            }
            Clay__OpenTextElement(text,
                CLAY_TEXT_CONFIG({
                    .fontSize = THEME_FONT_SIZE_MONO,
                    .textColor = color,
                    .fontId = CLAY_FONT_MONO,
                }));
        }
    }
    *y += row_h;
    (*clay_idx)++;

    /* If field was being edited and was just committed, write value back. */
    if (slot < 5 && !s_coll_fields[slot].active && target) {
        /* Check if commit happened (field deactivated with a new value). */
    }
}

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

    /* Get the skeleton — skeleton mode uses working copy, else registry. */
    skeleton_def_t *skel_mut = NULL;
    const skeleton_def_t *skel = NULL;

    if (ed->skeleton_mode.active) {
        skel_mut = skeleton_mode_get_work_skel(&ed->skeleton_mode);
        skel = skel_mut;
    } else {
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
        const char *fname = spath;
        for (const char *p = spath; *p; p++) {
            if (*p == '/') fname = p + 1;
        }
        edit_skeleton_entry_t *entry =
            edit_skeleton_registry_get_mut(&ed->skeleton_registry, fname);
        if (!entry || entry->skel.joint_count == 0) return;
        skel_mut = &entry->skel;
        skel = skel_mut;
    }
    if (!skel || skel->joint_count == 0) return;
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

    /* ---- Collision section ---- */
    {
        /* Allocate colliders on first access if needed. */
        if (!skel_mut->colliders && skel_mut) {
            skel_mut->colliders = (bone_collider_desc_t *)calloc(
                skel_mut->joint_count, sizeof(bone_collider_desc_t));
        }

        /* Separator. */
        emit_text_row_(clay_idx, y_cursor, sep_h, scroll_px, visible_h,
                        1, color_accent, CLAY_FONT_UI);

        /* Section header. */
        {
            int n = snprintf(s_bone_bufs[8], BONE_BUF_SIZE, "Collision");
            s_bone_lens[8] = (n > 0 && n < BONE_BUF_SIZE) ? (int32_t)n : 0;
            emit_text_row_(clay_idx, y_cursor, row_h, scroll_px, visible_h,
                            8, color_accent, CLAY_FONT_UI);
        }

        if (skel_mut->colliders) {
            bone_collider_desc_t *cd = &skel_mut->colliders[bi];

            /* Shape type (clickable to cycle). */
            static const char *shape_names[] = {
                "none", "capsule", "box", "sphere", "hull", "point"
            };
            const char *sname = (cd->shape_type < 6)
                ? shape_names[cd->shape_type] : "unknown";
            int n = snprintf(s_bone_bufs[9], BONE_BUF_SIZE,
                             "  Shape: %s  (click to cycle)", sname);
            s_bone_lens[9] = (n > 0 && n < BONE_BUF_SIZE) ? (int32_t)n : 0;

            s_shape_click_ctx.ed = ed;
            s_shape_click_ctx.bone_index = bi;

            if (is_visible_(*y_cursor, row_h, scroll_px, visible_h)) {
                Clay_String text = {.length = s_bone_lens[9],
                                    .chars = s_bone_bufs[9]};
                CLAY(CLAY_IDI("BoneInspRow", *clay_idx), {
                    .layout = {
                        .sizing = {CLAY_SIZING_GROW(0),
                                   CLAY_SIZING_FIXED(row_h)},
                        .padding = {THEME_PADDING, THEME_PADDING, 0, 0},
                    },
                }) {
                    Clay_OnHover(on_shape_cycle_, &s_shape_click_ctx);
                    Clay__OpenTextElement(text,
                        CLAY_TEXT_CONFIG({
                            .fontSize = THEME_FONT_SIZE_MONO,
                            .textColor = color_text,
                            .fontId = CLAY_FONT_MONO,
                        }));
                }
            }
            *y_cursor += row_h;
            (*clay_idx)++;

            /* Per-shape parameter fields. */
            switch (cd->shape_type) {
            case BONE_COLLIDER_CAPSULE:
                emit_collider_field_(ed, clay_idx, y_cursor, row_h,
                    scroll_px, visible_h, 10, "Radius", cd->params[0],
                    COLLIDER_FIELD_PARAM0, &cd->params[0], bi);
                emit_collider_field_(ed, clay_idx, y_cursor, row_h,
                    scroll_px, visible_h, 11, "Height", cd->params[1],
                    COLLIDER_FIELD_PARAM1, &cd->params[1], bi);
                break;
            case BONE_COLLIDER_BOX:
                emit_collider_field_(ed, clay_idx, y_cursor, row_h,
                    scroll_px, visible_h, 10, "Half X", cd->params[0],
                    COLLIDER_FIELD_PARAM0, &cd->params[0], bi);
                emit_collider_field_(ed, clay_idx, y_cursor, row_h,
                    scroll_px, visible_h, 11, "Half Y", cd->params[1],
                    COLLIDER_FIELD_PARAM1, &cd->params[1], bi);
                emit_collider_field_(ed, clay_idx, y_cursor, row_h,
                    scroll_px, visible_h, 12, "Half Z", cd->params[2],
                    COLLIDER_FIELD_PARAM2, &cd->params[2], bi);
                break;
            case BONE_COLLIDER_SPHERE:
                emit_collider_field_(ed, clay_idx, y_cursor, row_h,
                    scroll_px, visible_h, 10, "Radius", cd->params[0],
                    COLLIDER_FIELD_PARAM0, &cd->params[0], bi);
                break;
            default:
                break;
            }

            /* Mass field. */
            emit_collider_field_(ed, clay_idx, y_cursor, row_h,
                scroll_px, visible_h, 13, "Mass", cd->mass,
                COLLIDER_FIELD_MASS, &cd->mass, bi);

            /* CCD toggle. */
            n = snprintf(s_bone_bufs[14], BONE_BUF_SIZE,
                         "  CCD: %s", cd->ccd_enabled ? "ON" : "off");
            s_bone_lens[14] = (n > 0 && n < BONE_BUF_SIZE) ? (int32_t)n : 0;
            emit_text_row_(clay_idx, y_cursor, row_h, scroll_px, visible_h,
                            14, color_text, CLAY_FONT_MONO);

            /* Kinematic toggle. */
            n = snprintf(s_bone_bufs[15], BONE_BUF_SIZE,
                         "  Kinematic: %s", cd->is_kinematic ? "ON" : "off");
            s_bone_lens[15] = (n > 0 && n < BONE_BUF_SIZE) ? (int32_t)n : 0;
            emit_text_row_(clay_idx, y_cursor, row_h, scroll_px, visible_h,
                            15, color_text, CLAY_FONT_MONO);

            /* Collision group. */
            emit_collider_field_(ed, clay_idx, y_cursor, row_h,
                scroll_px, visible_h, 16, "Group",
                (float)cd->collision_group,
                COLLIDER_FIELD_GROUP,
                (float *)&cd->collision_group, bi);
        }
    }

    /* Write back committed inline field values. */
    if (skel_mut && skel_mut->colliders) {
        for (int fi = 0; fi < 5; fi++) {
            if (!s_coll_fields[fi].active && s_coll_fields[fi].field_id != 0) {
                /* Field was committed — value already written via target pointer
                 * during inline_field_commit in the key handler. */
                s_coll_fields[fi].field_id = 0;
            }
        }
    }
}
