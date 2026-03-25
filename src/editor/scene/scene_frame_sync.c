/**
 * @file scene_frame_sync.c
 * @brief Delta-compressed entity sync — response processing.
 *
 * Processes sync_entities responses (delta or full) received from the
 * server. Commands like clone_id return sync-format responses directly;
 * the server may also push periodic full snapshots.
 *
 * Non-static functions: 1 (scene_frame_process_sync_response).
 */

#include "ferrum/editor/scene/scene_frame.h"
#include "ferrum/editor/scene/scene_main.h"
#include "ferrum/editor/scene/scene_connection.h"
#include "ferrum/editor/scene/scene_sync.h"
#include "ferrum/editor/scene/scene_asset_load.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_entity_json.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/editor/json_parse.h"
#include "ferrum/entity/entity_attrs.h"
#include "ferrum/math/quat.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Static helpers                                                            */
/* ------------------------------------------------------------------------ */

/**
 * @brief Apply a single entity from a sync response to the local store.
 *
 * Uses the shared edit_entity_json_parse() to deserialize ALL entity
 * fields from the JSON object, then restores or updates the entity
 * in the local store. Triggers mesh loading for MESH entities with
 * a mesh_path attribute.
 */
static void apply_entity_(scene_editor_t *ed, const json_value_t *item) {
    if (!item || item->type != JSON_OBJECT) return;

    const json_value_t *id_val = json_object_get(item, "id");
    if (!id_val || id_val->type != JSON_NUMBER) return;
    uint32_t eid = (uint32_t)id_val->number;

    /* Parse all fields into a snapshot using shared deserializer. */
    edit_entity_t snapshot;
    edit_entity_json_parse(item, &snapshot);

    /* Preserve pending_delete flag from local state. */
    const edit_entity_t *existing = edit_entity_store_get(&ed->entities, eid);
    if (existing && existing->pending_delete) {
        snapshot.pending_delete = true;
    }

    /* Stamp with current refresh generation (for full sync stale detection). */
    snapshot.refresh_gen = ed->entity_refresh_gen;

    /* Restore or update. */
    if (!edit_entity_store_restore(&ed->entities, eid, &snapshot)) {
        edit_entity_t *existing_mut =
            edit_entity_store_get_mut(&ed->entities, eid);
        if (existing_mut) {
            bool was_pending = existing_mut->pending_delete;
            memcpy(existing_mut, &snapshot, sizeof(edit_entity_t));
            existing_mut->active = true;
            if (was_pending) {
                existing_mut->pending_delete = true;
            }
        }
    }

    /* Trigger mesh load for entities with a mesh_path attribute. */
    uint8_t attr_type = 0;
    uint8_t attr_size = 0;
    edit_entity_t *ent_mut = edit_entity_store_get_mut(&ed->entities, eid);
    if (ent_mut) {
        const void *mp = entity_attrs_get(&ent_mut->attrs,
                                           SCRIPT_KEY_MESH_PATH,
                                           &attr_type, &attr_size);
        if (mp && attr_type == SCRIPT_ATTR_STR) {
            const char *mesh_path = (const char *)mp;
            if (mesh_path[0] != '\0') {
                scene_load_entity_mesh(ed, eid, mesh_path);
            }
        }

        /* Trigger collision mesh load if the entity has a separate
         * collision mesh path. This overrides the render mesh for
         * snap raycasting and physics collision. */
        const void *cp = entity_attrs_get(&ent_mut->attrs,
                                           SCRIPT_KEY_COLLISION_MESH_PATH,
                                           &attr_type, &attr_size);
        if (cp && attr_type == SCRIPT_ATTR_STR) {
            const char *col_path = (const char *)cp;
            if (col_path[0] != '\0') {
                scene_load_entity_collision_mesh(ed, eid, col_path);
            }
        }

        /* Trigger skeleton binding if the entity has a skel_path.
         * Skip for ARMATURE entities — they have no mesh to promote.
         * For mesh entities, promotes static mesh to skeletal. */
        if (ent_mut->type != EDIT_ENTITY_TYPE_ARMATURE) {
            const void *sp = entity_attrs_get(&ent_mut->attrs,
                                               SCRIPT_KEY_SKEL_PATH,
                                               &attr_type, &attr_size);
            if (sp && attr_type == SCRIPT_ATTR_STR) {
                const char *skel_path = (const char *)sp;
                if (skel_path[0] != '\0') {
                    scene_load_entity_skeleton(ed, eid, skel_path);
                }
            }
        }

        /* For ARMATURE entities, load the skeleton into the registry
         * so bone overlay can render it. */
        if (ent_mut->type == EDIT_ENTITY_TYPE_ARMATURE) {
            const void *sp = entity_attrs_get(&ent_mut->attrs,
                                               SCRIPT_KEY_SKEL_PATH,
                                               &attr_type, &attr_size);
            if (sp && attr_type == SCRIPT_ATTR_STR) {
                const char *skel_path = (const char *)sp;
                if (skel_path[0] != '\0') {
                    const char *fn = skel_path;
                    for (const char *pp = skel_path; *pp; pp++)
                        if (*pp == '/') fn = pp + 1;
                    if (!edit_skeleton_registry_get(&ed->skeleton_registry, fn)) {
                        char full[512];
                        snprintf(full, sizeof(full), "%s/%s",
                                 ed->config.asset_dir, skel_path);
                        edit_skeleton_registry_load(&ed->skeleton_registry, full);
                    }
                }
            }
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Public functions                                                          */
/* ------------------------------------------------------------------------ */

void scene_frame_process_sync_response(struct scene_editor *ed,
                                        const json_value_t *result_val) {
    if (!ed || !result_val || result_val->type != JSON_OBJECT) return;

    /* Extract common fields. */
    const json_value_t *version_val = json_object_get(result_val, "version");
    const json_value_t *full_val    = json_object_get(result_val, "full");
    const json_value_t *ents_val    = json_object_get(result_val, "entities");
    const json_value_t *ts_val      = json_object_get(result_val, "tombstones");

    bool is_full = true; /* default to full if flag missing */
    if (full_val && full_val->type == JSON_BOOL) {
        is_full = full_val->boolean;
    }

    if (is_full) {
        /* Full sync — same as paginated list_entities handling. */
        const json_value_t *total_val  = json_object_get(result_val, "total");
        const json_value_t *offset_val = json_object_get(result_val, "offset");

        uint32_t offset = 0, total = 0;
        if (offset_val && offset_val->type == JSON_NUMBER)
            offset = (uint32_t)offset_val->number;
        if (total_val && total_val->type == JSON_NUMBER)
            total = (uint32_t)total_val->number;

        /* First page: bump refresh generation. */
        if (offset == 0) {
            ed->entity_refresh_gen++;
        }

        /* Apply entities from this page. */
        if (ents_val && ents_val->type == JSON_ARRAY) {
            for (uint32_t i = 0; i < ents_val->array.count; i++) {
                apply_entity_(ed, &ents_val->array.items[i]);
            }

            uint32_t received = offset + ents_val->array.count;

            /* Last page: prune stale entities. */
            if (received >= total) {
                for (uint32_t ei = 0; ei < ed->entities.capacity; ei++) {
                    edit_entity_t *ent =
                        edit_entity_store_get_mut(&ed->entities, ei);
                    if (ent && ent->active && ent->pending_delete &&
                        ent->refresh_gen != ed->entity_refresh_gen) {
                        edit_selection_remove(&ed->selection, ei);
                        edit_entity_store_remove(&ed->entities, ei);
                    }
                }
            }

            /* Request next page if more remain. */
            if (received < total) {
                char cmd_buf[256];
                uint32_t cid = scene_connection_next_id(&ed->connection);
                int n = snprintf(cmd_buf, sizeof(cmd_buf),
                    "{\"id\":%u,\"cmd\":\"sync_entities\",\"args\":"
                    "{\"since_version\":0,\"offset\":%u}}\n",
                    (unsigned)cid, received);
                if (n > 0 && (size_t)n < sizeof(cmd_buf)) {
                    ctrl_conn_send_raw(&ed->connection.tcp,
                        cmd_buf, (uint32_t)n);
                    scene_sync_mark_sent(&ed->sync);
                }
            }
        }
    } else {
        /* Delta sync — apply only changed entities and tombstones.
         * Entities in a delta are typically newly created (e.g. clones),
         * so select them to match the user's expectation. */
        if (ents_val && ents_val->type == JSON_ARRAY) {
            for (uint32_t i = 0; i < ents_val->array.count; i++) {
                apply_entity_(ed, &ents_val->array.items[i]);

                /* Select the entity. */
                const json_value_t *id_val =
                    json_object_get(&ents_val->array.items[i], "id");
                if (id_val && id_val->type == JSON_NUMBER) {
                    edit_selection_add(&ed->selection,
                                       (uint32_t)id_val->number);
                }
            }
        }

        /* Remove tombstoned entities. */
        if (ts_val && ts_val->type == JSON_ARRAY) {
            for (uint32_t i = 0; i < ts_val->array.count; i++) {
                if (ts_val->array.items[i].type != JSON_NUMBER) continue;
                uint32_t dead_id = (uint32_t)ts_val->array.items[i].number;
                edit_selection_remove(&ed->selection, dead_id);
                edit_entity_store_remove(&ed->entities, dead_id);
            }
        }
    }

    /* Update server version. */
    if (version_val && version_val->type == JSON_NUMBER) {
        ed->server_version = (uint64_t)version_val->number;
    }
}
