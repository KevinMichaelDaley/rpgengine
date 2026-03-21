/**
 * @file prefab_mode_enter.c
 * @brief Enter and exit prefab editor mode.
 *
 * Non-static functions (2 / 4 limit):
 *   prefab_mode_enter
 *   prefab_mode_exit
 */

#include "ferrum/editor/scene/prefab/prefab_mode_enter.h"
#include "ferrum/editor/scene/scene_main.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/editor/edit_skeleton_registry.h"
#include "ferrum/entity/entity_attrs.h"

#include <stdio.h>
#include <string.h>

bool prefab_mode_enter(scene_editor_t *ed) {
    if (!ed) return false;

    /* Already in prefab mode — no-op. */
    if (ed->prefab_mode.active) return false;

    /* Require exactly one selected entity. */
    uint32_t sel_count = edit_selection_count(&ed->selection);
    if (sel_count != 1) return false;

    const uint32_t *sel_ids = edit_selection_ids(&ed->selection);
    uint32_t root_id = sel_ids[0];

    /* Validate the entity exists. */
    const edit_entity_t *ent = edit_entity_store_get(&ed->entities, root_id);
    if (!ent || !ent->active) return false;

    /* Hide all entities except the root and its children. Record which
     * were hidden so we can restore them on exit. Only hide entities
     * that weren't already hidden (don't clobber user-set hidden state). */
    ed->prefab_mode.hidden_count = 0;
    uint32_t cap = ed->entities.capacity;
    for (uint32_t i = 0; i < cap; i++) {
        if (i == root_id) continue;
        edit_entity_t *e = edit_entity_store_get_mut(&ed->entities, i);
        if (!e || !e->active) continue;
        if (e->hidden) continue; /* Already hidden by user — don't record. */

        /* Check if this entity is parented to the root (keep visible). */
        uint8_t pat = 0, pas = 0;
        const void *pid = entity_attrs_get(&e->attrs, SCRIPT_KEY_PARENT_ID,
                                            &pat, &pas);
        if (pid && pat == SCRIPT_ATTR_U32) {
            uint32_t parent_id = *(const uint32_t *)pid;
            if (parent_id == root_id) continue; /* Child of root — keep visible. */
        }

        if (ed->prefab_mode.hidden_count < PREFAB_MODE_MAX_HIDDEN) {
            ed->prefab_mode.hidden_ids[ed->prefab_mode.hidden_count++] = i;
        }
        e->hidden = true;
    }

    /* Activate prefab mode. */
    ed->prefab_mode.active = true;
    ed->prefab_mode.root_entity_id = root_id;
    ed->prefab_mode.dirty = true;  /* Trigger initial hull/outliner build. */
    ed->prefab_mode.dirty_gen++;

    /* Set active object to root so bone picking works. */
    ed->active_object_id = root_id;

    /* Set display name from entity name or fallback. */
    if (ent->name[0] != '\0') {
        snprintf(ed->prefab_mode.name, sizeof(ed->prefab_mode.name),
                 "%s", ent->name);
    } else {
        snprintf(ed->prefab_mode.name, sizeof(ed->prefab_mode.name),
                 "entity_%u", root_id);
    }

    return true;
}

void prefab_mode_exit(scene_editor_t *ed) {
    if (!ed) return;
    if (!ed->prefab_mode.active) return;

    /* Reload the skeleton from disk to undo any in-memory bone edits.
     * Bone gizmo drags/rotates modify rest_local/rest_world on the shared
     * skeleton_def_t in the registry. These edits are stored in the .fpfab
     * file (bone_poses), NOT in the .fskel file. Reloading here restores
     * the registry skeleton to its on-disk state. */
    {
        uint32_t root_id = ed->prefab_mode.root_entity_id;
        const edit_entity_t *root_ent =
            edit_entity_store_get(&ed->entities, root_id);
        if (root_ent) {
            uint8_t sat = 0, sas = 0;
            const void *ssp = entity_attrs_get(&root_ent->attrs,
                SCRIPT_KEY_SKEL_PATH, &sat, &sas);
            if (ssp && sat == SCRIPT_ATTR_STR) {
                const char *sp = (const char *)ssp;
                /* Build full path for reload. */
                char full_path[512];
                int n = snprintf(full_path, sizeof(full_path), "%s/%s",
                                 ed->config.asset_dir, sp);
                if (n > 0 && (size_t)n < sizeof(full_path)) {
                    edit_skeleton_registry_load(&ed->skeleton_registry,
                                                 full_path);
                }
            }
        }
    }

    /* Restore hidden entities. */
    for (uint32_t i = 0; i < ed->prefab_mode.hidden_count; i++) {
        uint32_t eid = ed->prefab_mode.hidden_ids[i];
        edit_entity_t *e = edit_entity_store_get_mut(&ed->entities, eid);
        if (e && e->active) {
            e->hidden = false;
        }
    }

    /* Clear prefab mode state. */
    prefab_mode_state_reset(&ed->prefab_mode);
}
