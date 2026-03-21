/**
 * @file skeleton_mode_enter.c
 * @brief Enter and exit skeleton editing mode (K key).
 *
 * Non-static functions (3 / 4 limit):
 *   skeleton_mode_enter
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

bool skeleton_mode_enter(scene_editor_t *ed) {
    if (!ed) return false;
    if (ed->skeleton_mode.active) return false;
    if (ed->prefab_mode.active) return false;

    /* Require exactly one selected entity. */
    uint32_t sel_count = edit_selection_count(&ed->selection);
    if (sel_count != 1) return false;

    const uint32_t *sel_ids = edit_selection_ids(&ed->selection);
    uint32_t eid = sel_ids[0];

    const edit_entity_t *ent = edit_entity_store_get(&ed->entities, eid);
    if (!ent || !ent->active) return false;

    /* Get skeleton path. */
    uint8_t at = 0, as = 0;
    const void *sp = entity_attrs_get(&ent->attrs, SCRIPT_KEY_SKEL_PATH,
                                       &at, &as);
    const char *skel_path = NULL;
    char auto_path[256] = {0};

    if (sp && at == SCRIPT_ATTR_STR && ((const char *)sp)[0] != '\0') {
        skel_path = (const char *)sp;
    } else {
        /* No skeleton — create an auto path and register empty skeleton. */
        snprintf(auto_path, sizeof(auto_path), "skeletons/%s.fskel",
                 ent->name[0] ? ent->name : "skeleton");
        skel_path = auto_path;

        /* Create initial skeleton with a single root bone. */
        skeleton_def_t empty;
        if (!skeleton_def_init(&empty, 1, 0)) return false;
        strncpy(empty.joint_names[0], "root",
                SKELETON_JOINT_NAME_MAX - 1);
        empty.parent_indices[0] = UINT32_MAX;
        empty.rest_local[0] = mat4_identity();
        empty.rest_world[0] = mat4_identity();

        /* Allocate tail_positions for the root bone. */
        empty.tail_positions = (float *)calloc(3, sizeof(float));
        if (empty.tail_positions) {
            empty.tail_positions[1] = 0.5f; /* Tail 0.5 units up. */
        }

        /* Extract filename for registry key. */
        const char *fname = skel_path;
        for (const char *p = skel_path; *p; p++) {
            if (*p == '/') fname = p + 1;
        }
        edit_skeleton_registry_add(&ed->skeleton_registry, fname,
                                     &empty, NULL, 0);

        /* Set attr on entity. */
        edit_entity_t *ent_mut = edit_entity_store_get_mut(&ed->entities, eid);
        if (ent_mut) {
            entity_attrs_set(&ent_mut->attrs, SCRIPT_KEY_SKEL_PATH,
                              SCRIPT_ATTR_STR, skel_path,
                              (uint8_t)(strlen(skel_path) + 1));
        }
    }

    /* Extract filename for registry lookup. */
    const char *fname = skel_path;
    for (const char *p = skel_path; *p; p++) {
        if (*p == '/') fname = p + 1;
    }

    /* Verify skeleton exists in registry. */
    const edit_skeleton_entry_t *se =
        edit_skeleton_registry_get(&ed->skeleton_registry, fname);
    if (!se) return false;

    /* Hide all entities except the target. */
    ed->skeleton_mode.hidden_count = 0;
    uint32_t cap = ed->entities.capacity;
    for (uint32_t i = 0; i < cap; i++) {
        if (i == eid) continue;
        edit_entity_t *e = edit_entity_store_get_mut(&ed->entities, i);
        if (!e || !e->active || e->hidden) continue;

        if (ed->skeleton_mode.hidden_count < SKELETON_MODE_MAX_HIDDEN) {
            ed->skeleton_mode.hidden_ids[ed->skeleton_mode.hidden_count++] = i;
        }
        e->hidden = true;
    }

    /* Activate skeleton mode. */
    ed->skeleton_mode.active = true;
    ed->skeleton_mode.entity_id = eid;
    strncpy(ed->skeleton_mode.skel_path, fname,
            sizeof(ed->skeleton_mode.skel_path) - 1);
    snprintf(ed->skeleton_mode.skel_full_path,
             sizeof(ed->skeleton_mode.skel_full_path),
             "%s/%s", ed->config.asset_dir, skel_path);

    /* Set active object for bone picking. */
    ed->active_object_id = eid;

    return true;
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

    /* Clear bone selection. */
    edit_bone_selection_clear(&ed->bone_selection);

    /* Clear state. */
    skeleton_mode_state_reset(&ed->skeleton_mode);
}
