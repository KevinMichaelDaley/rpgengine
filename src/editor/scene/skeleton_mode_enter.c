/**
 * @file skeleton_mode_enter.c
 * @brief Enter and exit skeleton editing mode (K key).
 *
 * Skeleton mode can be entered two ways:
 * 1. Select an .fskel asset in the asset browser → K opens it for editing
 * 2. Select a mesh/prefab asset → K creates a new skeleton with preview
 * 3. Select an entity with a skeleton → K opens its skeleton
 *
 * Non-static functions (4 / 4 limit):
 *   skeleton_mode_enter
 *   skeleton_mode_enter_asset
 *   skeleton_mode_exit
 *   skeleton_mode_state_reset
 */

#include "ferrum/editor/scene/skeleton_mode.h"
#include "ferrum/editor/scene/scene_main.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/editor/edit_skeleton_registry.h"
#include "ferrum/editor/anim/skeleton_builder.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/entity/entity_attrs.h"
#include "ferrum/math/mat4.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void skeleton_mode_state_reset(skeleton_mode_state_t *state) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
}

/**
 * @brief Create a new skeleton with a single root bone and register it.
 */
static bool create_initial_skeleton_(edit_skeleton_registry_t *reg,
                                       const char *fname) {
    skeleton_def_t skel;
    if (!skeleton_def_init(&skel, 1, 0)) return false;
    strncpy(skel.joint_names[0], "root", SKELETON_JOINT_NAME_MAX - 1);
    skel.parent_indices[0] = UINT32_MAX;
    skel.rest_local[0] = mat4_identity();
    skel.rest_world[0] = mat4_identity();
    skel.tail_positions = (float *)calloc(3, sizeof(float));
    if (skel.tail_positions) {
        skel.tail_positions[1] = 0.5f;
    }
    edit_skeleton_registry_add(reg, fname, &skel, NULL, 0);
    return true;
}

/**
 * @brief Common activation logic (hide entities, set state).
 */
static void activate_(scene_editor_t *ed, const char *fname,
                        const char *full_path, uint32_t entity_id) {
    /* Hide all entities for clean editing. */
    ed->skeleton_mode.hidden_count = 0;
    uint32_t cap = ed->entities.capacity;
    for (uint32_t i = 0; i < cap; i++) {
        edit_entity_t *e = edit_entity_store_get_mut(&ed->entities, i);
        if (!e || !e->active || e->hidden) continue;
        /* Keep the preview entity visible if one was specified. */
        if (i == entity_id) continue;
        if (ed->skeleton_mode.hidden_count < SKELETON_MODE_MAX_HIDDEN) {
            ed->skeleton_mode.hidden_ids[ed->skeleton_mode.hidden_count++] = i;
        }
        e->hidden = true;
    }

    ed->skeleton_mode.active = true;
    ed->skeleton_mode.entity_id = entity_id;
    strncpy(ed->skeleton_mode.skel_path, fname,
            sizeof(ed->skeleton_mode.skel_path) - 1);
    strncpy(ed->skeleton_mode.skel_full_path, full_path,
            sizeof(ed->skeleton_mode.skel_full_path) - 1);

    if (entity_id != UINT32_MAX) {
        ed->active_object_id = entity_id;
    }
}

bool skeleton_mode_enter(scene_editor_t *ed) {
    if (!ed) return false;
    if (ed->skeleton_mode.active) return false;
    if (ed->prefab_mode.active) return false;

    /* Try entity-based entry: selected entity has a skeleton. */
    uint32_t sel_count = edit_selection_count(&ed->selection);
    if (sel_count == 1) {
        const uint32_t *sel_ids = edit_selection_ids(&ed->selection);
        uint32_t eid = sel_ids[0];
        const edit_entity_t *ent = edit_entity_store_get(&ed->entities, eid);
        if (ent && ent->active) {
            uint8_t at = 0, as = 0;
            const void *sp = entity_attrs_get(&ent->attrs,
                SCRIPT_KEY_SKEL_PATH, &at, &as);
            if (sp && at == SCRIPT_ATTR_STR && ((const char *)sp)[0] != '\0') {
                const char *skel_path = (const char *)sp;
                const char *fname = skel_path;
                for (const char *p = skel_path; *p; p++) {
                    if (*p == '/') fname = p + 1;
                }

                /* Ensure skeleton exists in registry. */
                if (!edit_skeleton_registry_get(&ed->skeleton_registry, fname)) {
                    char full[512];
                    snprintf(full, sizeof(full), "%s/%s",
                             ed->config.asset_dir, skel_path);
                    edit_skeleton_registry_load(&ed->skeleton_registry, full);
                }

                if (edit_skeleton_registry_get(&ed->skeleton_registry, fname)) {
                    char full[512];
                    snprintf(full, sizeof(full), "%s/%s",
                             ed->config.asset_dir, skel_path);
                    activate_(ed, fname, full, eid);
                    return true;
                }
            }

            /* Entity has no skeleton — create one for it. */
            char auto_rel[256];
            snprintf(auto_rel, sizeof(auto_rel), "skeletons/%s.fskel",
                     ent->name[0] ? ent->name : "skeleton");
            const char *fname = auto_rel;
            for (const char *p = auto_rel; *p; p++) {
                if (*p == '/') fname = p + 1;
            }

            if (!create_initial_skeleton_(&ed->skeleton_registry, fname)) {
                return false;
            }

            /* Set skel_path attr on entity. */
            edit_entity_t *ent_mut = edit_entity_store_get_mut(&ed->entities, eid);
            if (ent_mut) {
                entity_attrs_set(&ent_mut->attrs, SCRIPT_KEY_SKEL_PATH,
                                  SCRIPT_ATTR_STR, auto_rel,
                                  (uint8_t)(strlen(auto_rel) + 1));
            }

            char full[512];
            snprintf(full, sizeof(full), "%s/%s",
                     ed->config.asset_dir, auto_rel);
            activate_(ed, fname, full, eid);
            return true;
        }
    }

    return false;
}

bool skeleton_mode_enter_asset(scene_editor_t *ed, const char *asset_path) {
    if (!ed || !asset_path) return false;
    if (ed->skeleton_mode.active) return false;
    if (ed->prefab_mode.active) return false;

    const char *fname = asset_path;
    for (const char *p = asset_path; *p; p++) {
        if (*p == '/') fname = p + 1;
    }

    /* Check extension. */
    const char *ext = strrchr(fname, '.');
    if (!ext) return false;

    char full[512];
    snprintf(full, sizeof(full), "%s/%s", ed->config.asset_dir, asset_path);

    if (strcmp(ext, ".fskel") == 0) {
        /* Open existing skeleton. */
        if (!edit_skeleton_registry_get(&ed->skeleton_registry, fname)) {
            edit_skeleton_registry_load(&ed->skeleton_registry, full);
        }
        const edit_skeleton_entry_t *se =
            edit_skeleton_registry_get(&ed->skeleton_registry, fname);
        if (!se) return false;
        /* Reject invalid skeletons (missing tail_positions). */
        if (se->skel.joint_count > 0 && !se->skel.tail_positions) {
            return false;
        }
        activate_(ed, fname, full, UINT32_MAX);
        return true;
    }

    if (strcmp(ext, ".fvma") == 0 || strcmp(ext, ".fpfab") == 0) {
        /* Create new skeleton for a mesh/prefab preview. */
        char skel_fname[256];
        size_t base_len = (size_t)(ext - fname);
        if (base_len >= sizeof(skel_fname) - 7) base_len = sizeof(skel_fname) - 7;
        memcpy(skel_fname, fname, base_len);
        memcpy(skel_fname + base_len, ".fskel", 7);

        if (!edit_skeleton_registry_get(&ed->skeleton_registry, skel_fname)) {
            create_initial_skeleton_(&ed->skeleton_registry, skel_fname);
        }

        char skel_full[512];
        snprintf(skel_full, sizeof(skel_full), "%s/skeletons/%s",
                 ed->config.asset_dir, skel_fname);
        activate_(ed, skel_fname, skel_full, UINT32_MAX);
        /* TODO: load mesh/prefab as ghost preview. */
        return true;
    }

    return false;
}

void skeleton_mode_exit(scene_editor_t *ed) {
    if (!ed || !ed->skeleton_mode.active) return;

    /* Restore hidden entities. */
    for (uint32_t i = 0; i < ed->skeleton_mode.hidden_count; i++) {
        uint32_t hid = ed->skeleton_mode.hidden_ids[i];
        edit_entity_t *e = edit_entity_store_get_mut(&ed->entities, hid);
        if (e && e->active) {
            e->hidden = false;
        }
    }

    edit_bone_selection_clear(&ed->bone_selection);
    skeleton_mode_state_reset(&ed->skeleton_mode);
}
