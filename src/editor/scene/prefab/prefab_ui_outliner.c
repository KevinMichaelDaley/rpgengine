/**
 * @file prefab_ui_outliner.c
 * @brief Prefab mode outliner panel: entity tree with bone hierarchy.
 *
 * Renders the prefab hierarchy in the outliner when prefab mode is active.
 * Shows: root entity → Skeleton node (with bones) → child entities.
 * Clicking a bone row selects/toggles it in the bone selection.
 * Shift+click toggles individual bones (multi-select).
 * Includes a scrollbar when the list exceeds visible height.
 *
 * Non-static functions: scene_ui_build_prefab_outliner (1/4).
 */

#include "ferrum/editor/scene/prefab/prefab_ui_outliner.h"
#include "ferrum/editor/scene/prefab/prefab_outliner.h"
#include "ferrum/editor/scene/scene_main.h"
#include "ferrum/editor/scene/scene_panel.h"
#include "ferrum/editor/edit_bone_selection.h"
#include "ferrum/editor/ui/clay_theme.h"
#include "ferrum/editor/ui/clay_fonts.h"
#include "ferrum/editor/edit_skeleton_registry.h"
#include "ferrum/entity/entity_attrs.h"
#include "clay.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

/* Max visible rows in the prefab outliner. */
#define PREFAB_OUTLINER_VIS_MAX 128

/* Static name buffers for Clay_String lifetime. */
static char s_prefab_row_names[PREFAB_OUTLINER_VIS_MAX][80];

/* Title buffer. */
static char s_prefab_title[128];

/* ---- Click context for bone/entity rows ---- */

typedef struct {
    scene_editor_t *ed;
    uint32_t        bone_index;   /**< Bone index (UINT32_MAX = not a bone). */
    uint32_t        entity_id;    /**< Entity ID for entity rows. */
    bool            is_bone;      /**< True if this row is a bone. */
} prefab_row_ctx_t;

static prefab_row_ctx_t s_row_ctx[PREFAB_OUTLINER_VIS_MAX];

/**
 * @brief Hover callback for prefab outliner row click.
 *
 * Plain click: clear selection and select this bone.
 * Shift+click: toggle this bone in the selection (multi-select).
 * Entity rows: no action (future use).
 */
static void on_row_hover(Clay_ElementId id, Clay_PointerData data,
                          void *user_data) {
    (void)id;
    if (data.state != CLAY_POINTER_DATA_PRESSED_THIS_FRAME) return;

    prefab_row_ctx_t *ctx = (prefab_row_ctx_t *)user_data;
    if (!ctx || !ctx->ed) return;

    if (ctx->is_bone) {
        uint32_t root_id = ctx->ed->prefab_mode.root_entity_id;
        SDL_Keymod mod = SDL_GetModState();
        bool shift = (mod & KMOD_SHIFT) != 0;

        if (shift) {
            /* Shift+click: toggle this bone in multi-selection. */
            edit_bone_selection_toggle(&ctx->ed->bone_selection,
                                       root_id, ctx->bone_index);
        } else {
            /* Plain click: replace selection with just this bone. */
            edit_bone_selection_clear(&ctx->ed->bone_selection);
            edit_bone_selection_add(&ctx->ed->bone_selection,
                                    root_id, ctx->bone_index);
        }
    }
}

/* ---- Helpers ---- */

/**
 * @brief Get the skeleton for the prefab root entity.
 * @return Pointer to skeleton_def_t or NULL if not found.
 */
static const skeleton_def_t *get_prefab_skeleton(scene_editor_t *ed) {
    uint32_t root_id = ed->prefab_mode.root_entity_id;
    const edit_entity_t *root = edit_entity_store_get(&ed->entities, root_id);
    if (!root) return NULL;

    /* Read skel_path attr. */
    uint8_t type, size;
    const void *data = entity_attrs_get(&root->attrs,
                                         SCRIPT_KEY_SKEL_PATH, &type, &size);
    if (!data || type != SCRIPT_ATTR_STR || size == 0) return NULL;

    /* Extract filename from path for registry lookup. */
    const char *path = (const char *)data;
    const char *slash = strrchr(path, '/');
    const char *filename = slash ? slash + 1 : path;

    const edit_skeleton_entry_t *entry =
        edit_skeleton_registry_get(&ed->skeleton_registry, filename);
    if (!entry) return NULL;

    return &entry->skel;
}

/**
 * @brief Render a single row in the outliner.
 *
 * @param idx      Row index for Clay ID and buffer storage.
 * @param text     Row display text (copied to static buffer).
 * @param color    Text color.
 * @param bg       Background color.
 * @param ctx      Click context (NULL = no click handler).
 */
static void render_row(uint32_t idx, const char *text,
                       Clay_Color color, Clay_Color bg,
                       prefab_row_ctx_t *ctx) {
    if (idx >= PREFAB_OUTLINER_VIS_MAX) return;

    /* Copy text into static buffer. If text is NULL, assume the caller
     * already wrote directly into s_prefab_row_names[idx]. */
    if (text) {
        strncpy(s_prefab_row_names[idx], text,
                sizeof(s_prefab_row_names[idx]) - 1);
        s_prefab_row_names[idx][sizeof(s_prefab_row_names[idx]) - 1] = '\0';
    }

    CLAY(CLAY_IDI("PrefabOutliner_Row", idx), {
        .layout = {
            .sizing = {CLAY_SIZING_GROW(0),
                       CLAY_SIZING_FIXED(THEME_ROW_HEIGHT)},
            .padding = {THEME_PADDING_SMALL,
                        THEME_PADDING_SMALL, 0, 0},
            .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
        },
        .backgroundColor = bg,
    }) {
        if (ctx) {
            Clay_OnHover(on_row_hover, ctx);
        }

        Clay_String row_str = {
            .length = (int32_t)strlen(s_prefab_row_names[idx]),
            .chars = s_prefab_row_names[idx],
        };
        Clay__OpenTextElement(row_str,
            CLAY_TEXT_CONFIG({
                .fontSize = THEME_FONT_SIZE_UI,
                .textColor = color,
                .fontId = CLAY_FONT_UI,
            }));
    }
}

/**
 * @brief Count total rows that the prefab outliner would render.
 *
 * Counts: root entity (1) + skeleton header (1 if skel) + tree entries
 * + child entities. Used for scroll calculations.
 */
static uint32_t count_total_rows(scene_editor_t *ed,
                                  const skeleton_def_t *skel,
                                  const prefab_outliner_t *tree) {
    uint32_t total = 1; /* Root entity row. */

    if (skel) {
        total += 1; /* "Skeleton" group header. */
        total += prefab_outliner_count(tree);
    }

    /* Count child entities parented to root. */
    uint32_t root_id = ed->prefab_mode.root_entity_id;
    uint32_t cap = ed->entities.capacity;
    for (uint32_t i = 0; i < cap; i++) {
        if (i == root_id) continue;
        const edit_entity_t *ent =
            edit_entity_store_get(&ed->entities, i);
        if (!ent || !ent->active) continue;

        uint8_t atype = 0, asize = 0;
        const void *pdata = entity_attrs_get(&ent->attrs,
            SCRIPT_KEY_PARENT_ID, &atype, &asize);
        if (!pdata || atype != SCRIPT_ATTR_U32) continue;
        uint32_t parent_id = *(const uint32_t *)pdata;
        if (parent_id != root_id) continue;

        /* Skip bone-parented entities (already in skeleton tree). */
        const void *bdata = entity_attrs_get(&ent->attrs,
            SCRIPT_KEY_BONE_INDEX, &atype, &asize);
        if (bdata && atype == SCRIPT_ATTR_U32) continue;

        total++;
    }

    return total;
}

/* ---- Public API ---- */

void scene_ui_build_prefab_outliner(struct scene_editor *ed,
                                    const struct panel_rect *rect) {
    if (!ed || !rect || rect->w <= 0 || rect->h <= 0) return;
    if (!ed->prefab_mode.active) return;

    float panel_w = (float)rect->w;
    float panel_h = (float)rect->h;

    uint32_t root_id = ed->prefab_mode.root_entity_id;

    /* Persistent outliner tree — rebuild only when dirty or first use. */
    static prefab_outliner_t s_tree;
    static uint32_t s_tree_gen = 0;
    static uint32_t s_tree_root_id = UINT32_MAX;

    const skeleton_def_t *skel = get_prefab_skeleton(ed);

    /* Invalidate on root change, dirty generation bump, or
     * skeleton becoming available after initial empty build. */
    static bool s_had_skel = false;
    bool needs_rebuild = s_tree_root_id != root_id
                         || s_tree_gen != ed->prefab_mode.dirty_gen
                         || (skel && !s_had_skel);
    if (needs_rebuild) {
        s_had_skel = (skel != NULL);
        prefab_outliner_init(&s_tree);
        if (skel) {
            prefab_outliner_build(&s_tree, skel, &ed->entities, root_id);
        }
        s_tree_gen = ed->prefab_mode.dirty_gen;
        s_tree_root_id = root_id;
    }
    prefab_outliner_t *tree = &s_tree;

    /* Build title string. */
    if (ed->prefab_mode.name[0] != '\0') {
        snprintf(s_prefab_title, sizeof(s_prefab_title),
                 "Prefab: %.118s", ed->prefab_mode.name);
    } else {
        snprintf(s_prefab_title, sizeof(s_prefab_title), "Prefab Editor");
    }

    /* Panel background. */
    Clay_Color bg = {THEME_BG_PANEL_R, THEME_BG_PANEL_G,
                     THEME_BG_PANEL_B, THEME_BG_PANEL_A};
    bool focused = (ed->layout.focus == PANEL_OUTLINER);
    Clay_Color title_bg = focused
        ? (Clay_Color){THEME_ACCENT_R, THEME_ACCENT_G, THEME_ACCENT_B, 140}
        : (Clay_Color){THEME_ACCENT_R, THEME_ACCENT_G, THEME_ACCENT_B, 60};

    /* Colors. */
    Clay_Color text_bright = {THEME_TEXT_R, THEME_TEXT_G,
                               THEME_TEXT_B, THEME_TEXT_A};
    Clay_Color text_dim = {THEME_TEXT_DIM_R, THEME_TEXT_DIM_G,
                            THEME_TEXT_DIM_B, THEME_TEXT_DIM_A};
    Clay_Color bg_none = {0, 0, 0, 0};
    Clay_Color bg_selected = {THEME_ACCENT_R, THEME_ACCENT_G,
                               THEME_ACCENT_B, 80};
    Clay_Color bg_group = {40, 42, 48, 255};

    /* ---- Scroll state ---- */
    float list_h = panel_h
                   - (float)THEME_ROW_HEIGHT
                   - (float)(THEME_PADDING_SMALL * 2);
    int vis_lines = (int)(list_h / (float)(THEME_ROW_HEIGHT + 1));
    if (vis_lines < 1) vis_lines = 1;

    uint32_t total_rows = count_total_rows(ed, skel, tree);
    ed->ui.prefab_outliner_total = (int)total_rows;
    ed->ui.prefab_outliner_visible_lines = vis_lines;

    /* Clamp scroll offset. */
    int max_scroll = (int)total_rows - vis_lines;
    if (max_scroll < 0) max_scroll = 0;
    if (ed->ui.prefab_outliner_scroll > max_scroll)
        ed->ui.prefab_outliner_scroll = max_scroll;
    if (ed->ui.prefab_outliner_scroll < 0)
        ed->ui.prefab_outliner_scroll = 0;
    int skip = ed->ui.prefab_outliner_scroll;
    bool needs_scrollbar = (int)total_rows > vis_lines && panel_w > 40.0f;

    CLAY(CLAY_ID("PrefabOutliner_Root"), {
        .layout = {
            .sizing = {CLAY_SIZING_FIXED(panel_w),
                       CLAY_SIZING_FIXED(panel_h)},
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .padding = {THEME_PADDING_SMALL, THEME_PADDING_SMALL,
                        THEME_PADDING_SMALL, THEME_PADDING_SMALL},
        },
        .backgroundColor = bg,
        .clip = {.horizontal = true, .vertical = true},
        .floating = {
            .attachTo = CLAY_ATTACH_TO_ROOT,
            .offset = {(float)rect->x, (float)rect->y},
        },
    }) {
        /* Title bar. */
        CLAY(CLAY_ID("PrefabOutliner_Title"), {
            .layout = {
                .sizing = {CLAY_SIZING_GROW(0),
                           CLAY_SIZING_FIXED(THEME_ROW_HEIGHT)},
                .padding = {THEME_PADDING_SMALL, THEME_PADDING_SMALL, 0, 0},
            },
            .backgroundColor = title_bg,
        }) {
            Clay_String title_str = {
                .length = (int32_t)strlen(s_prefab_title),
                .chars = s_prefab_title,
            };
            Clay__OpenTextElement(title_str,
                CLAY_TEXT_CONFIG({
                    .fontSize = THEME_FONT_SIZE_UI,
                    .textColor = text_bright,
                    .fontId = CLAY_FONT_UI,
                }));
        }

        /* List area: rows + scrollbar side by side. */
        CLAY(CLAY_ID("PrefabOutliner_ListArea"), {
            .layout = {
                .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                .layoutDirection = CLAY_LEFT_TO_RIGHT,
            },
        }) {
            /* ---- Entry rows ---- */
            CLAY(CLAY_ID("PrefabOutliner_List"), {
                .layout = {
                    .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                    .layoutDirection = CLAY_TOP_TO_BOTTOM,
                    .childGap = 1,
                },
            }) {
                /* We iterate all "logical" rows (root, skeleton header,
                 * tree entries, child entities) skipping the first `skip`
                 * and rendering up to `vis_lines`. */
                uint32_t logical_row = 0; /* Logical index across all rows. */
                uint32_t rendered = 0;    /* Rendered row count (Clay index). */

                /* ---- Row: Root entity ---- */
                if (logical_row >= (uint32_t)skip &&
                    rendered < (uint32_t)vis_lines &&
                    rendered < PREFAB_OUTLINER_VIS_MAX) {
                    const edit_entity_t *root_ent =
                        edit_entity_store_get(&ed->entities, root_id);
                    char root_label[80];
                    if (root_ent && root_ent->name[0] != '\0') {
                        snprintf(root_label, sizeof(root_label),
                                 "%.79s", root_ent->name);
                    } else {
                        snprintf(root_label, sizeof(root_label),
                                 "entity_%u", root_id);
                    }

                    s_row_ctx[rendered].ed = ed;
                    s_row_ctx[rendered].bone_index = UINT32_MAX;
                    s_row_ctx[rendered].entity_id = root_id;
                    s_row_ctx[rendered].is_bone = false;

                    render_row(rendered, root_label, text_bright, bg_none, NULL);
                    rendered++;
                }
                logical_row++;

                /* ---- Skeleton group node + bones ---- */
                if (skel) {
                    /* "Skeleton" group header. */
                    if (logical_row >= (uint32_t)skip &&
                        rendered < (uint32_t)vis_lines &&
                        rendered < PREFAB_OUTLINER_VIS_MAX) {
                        render_row(rendered, "  Skeleton", text_dim,
                                   bg_group, NULL);
                        rendered++;
                    }
                    logical_row++;

                    /* Bone/collider entries from the tree. */
                    uint32_t tree_count = prefab_outliner_count(tree);
                    for (uint32_t i = 0; i < tree_count; i++) {
                        if (rendered >= (uint32_t)vis_lines ||
                            rendered >= PREFAB_OUTLINER_VIS_MAX) break;

                        if (logical_row < (uint32_t)skip) {
                            logical_row++;
                            continue;
                        }

                        const prefab_outliner_entry_t *e =
                            prefab_outliner_get(tree, i);
                        if (!e) break;

                        /* Indent: +2 (root + skeleton) + entry indent. */
                        int indent_chars = (e->indent + 2) * 2;

                        if (e->is_bone) {
                            snprintf(s_prefab_row_names[rendered],
                                     sizeof(s_prefab_row_names[rendered]),
                                     "%*s[bone] %s", indent_chars, "", e->name);
                        } else {
                            snprintf(s_prefab_row_names[rendered],
                                     sizeof(s_prefab_row_names[rendered]),
                                     "%*s  %s", indent_chars, "", e->name);
                        }

                        /* Set up click context. */
                        s_row_ctx[rendered].ed = ed;
                        s_row_ctx[rendered].bone_index = e->bone_index;
                        s_row_ctx[rendered].entity_id = e->entity_id;
                        s_row_ctx[rendered].is_bone = e->is_bone;

                        /* Highlight selected bones. */
                        Clay_Color row_bg = bg_none;
                        Clay_Color row_text = e->is_bone ? text_bright : text_dim;
                        if (e->is_bone) {
                            bool selected = edit_bone_selection_contains(
                                &ed->bone_selection, root_id, e->bone_index);
                            if (selected) {
                                row_bg = bg_selected;
                            }
                        }

                        render_row(rendered, NULL, row_text, row_bg,
                                   &s_row_ctx[rendered]);
                        rendered++;
                        logical_row++;
                    }
                }

                /* ---- Child entities (not bone-parented) ---- */
                {
                    uint32_t cap = ed->entities.capacity;
                    for (uint32_t i = 0; i < cap; i++) {
                        if (rendered >= (uint32_t)vis_lines ||
                            rendered >= PREFAB_OUTLINER_VIS_MAX) break;

                        if (i == root_id) continue;
                        const edit_entity_t *ent =
                            edit_entity_store_get(&ed->entities, i);
                        if (!ent || !ent->active) continue;

                        /* Check if parented to root. */
                        uint8_t atype = 0, asize = 0;
                        const void *pdata = entity_attrs_get(&ent->attrs,
                            SCRIPT_KEY_PARENT_ID, &atype, &asize);
                        if (!pdata || atype != SCRIPT_ATTR_U32) continue;
                        uint32_t parent_id = *(const uint32_t *)pdata;
                        if (parent_id != root_id) continue;

                        /* Skip bone-parented entities. */
                        const void *bdata = entity_attrs_get(&ent->attrs,
                            SCRIPT_KEY_BONE_INDEX, &atype, &asize);
                        if (bdata && atype == SCRIPT_ATTR_U32) continue;

                        if (logical_row < (uint32_t)skip) {
                            logical_row++;
                            continue;
                        }

                        /* Show as child of root. */
                        char child_label[80];
                        if (ent->name[0] != '\0') {
                            snprintf(child_label, sizeof(child_label),
                                     "  %.76s", ent->name);
                        } else {
                            snprintf(child_label, sizeof(child_label),
                                     "  entity_%u", i);
                        }

                        s_row_ctx[rendered].ed = ed;
                        s_row_ctx[rendered].bone_index = UINT32_MAX;
                        s_row_ctx[rendered].entity_id = i;
                        s_row_ctx[rendered].is_bone = false;

                        render_row(rendered, child_label, text_dim,
                                   bg_none, NULL);
                        rendered++;
                        logical_row++;
                    }
                }
            }

            /* ---- Scrollbar (fixed 8px wide) ---- */
            if (needs_scrollbar) {
                float track_h = list_h;
                float thumb_ratio = (float)vis_lines / (float)total_rows;
                if (thumb_ratio > 1.0f) thumb_ratio = 1.0f;
                float thumb_h = track_h * thumb_ratio;
                if (thumb_h < 12.0f) thumb_h = 12.0f;

                float scroll_range = track_h - thumb_h;
                float thumb_offset = 0.0f;
                if (max_scroll > 0) {
                    thumb_offset = scroll_range
                                   * ((float)ed->ui.prefab_outliner_scroll
                                      / (float)max_scroll);
                }

                CLAY(CLAY_ID("PrefabOutliner_ScrollTrack"), {
                    .layout = {
                        .sizing = {CLAY_SIZING_FIXED(8),
                                   CLAY_SIZING_GROW(0)},
                        .layoutDirection = CLAY_TOP_TO_BOTTOM,
                        .padding = {0, 0, (uint16_t)thumb_offset, 0},
                    },
                    .backgroundColor = {25, 27, 33, 255},
                }) {
                    CLAY(CLAY_ID("PrefabOutliner_ScrollThumb"), {
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
