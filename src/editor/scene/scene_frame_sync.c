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
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/editor/json_parse.h"
#include "ferrum/math/quat.h"

#include <string.h>

/* ------------------------------------------------------------------------ */
/* Static helpers                                                            */
/* ------------------------------------------------------------------------ */

/** @brief Parse a vec3 JSON array into a float[3]. */
static void parse_vec3_(const json_value_t *arr, float out[3]) {
    if (!arr || arr->type != JSON_ARRAY) return;
    for (int i = 0; i < 3 && (uint32_t)i < arr->array.count; i++) {
        const json_value_t *elem = json_array_get(arr, (uint32_t)i);
        if (elem && elem->type == JSON_NUMBER) {
            out[i] = (float)elem->number;
        }
    }
}

/**
 * @brief Apply a single entity from a sync response to the local store.
 *
 * Creates or updates the entity at the server-assigned ID slot.
 */
static void apply_entity_(scene_editor_t *ed, const json_value_t *item) {
    if (!item || item->type != JSON_OBJECT) return;

    const json_value_t *id_val = json_object_get(item, "id");
    if (!id_val || id_val->type != JSON_NUMBER) return;
    uint32_t eid = (uint32_t)id_val->number;

    /* Build snapshot. */
    edit_entity_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.active = true;
    snapshot.scale[0] = 1.0f;
    snapshot.scale[1] = 1.0f;
    snapshot.scale[2] = 1.0f;
    snapshot.orientation = (quat_t){0.0f, 0.0f, 0.0f, 1.0f};
    snapshot.body_index = UINT32_MAX;

    /* Type. */
    const json_value_t *type_val = json_object_get(item, "type");
    if (type_val && type_val->type == JSON_STRING) {
        char type_name[32];
        memset(type_name, 0, sizeof(type_name));
        json_string_copy(type_val, type_name, sizeof(type_name));
        uint32_t type_id = edit_entity_type_by_name(type_name);
        snapshot.type = (type_id != UINT32_MAX) ? type_id
                                                : EDIT_ENTITY_TYPE_BOX;
    }

    /* Name. */
    const json_value_t *name_val = json_object_get(item, "name");
    if (name_val && name_val->type == JSON_STRING) {
        json_string_copy(name_val, snapshot.name, sizeof(snapshot.name));
    }

    /* Position. */
    parse_vec3_(json_object_get(item, "pos"), snapshot.pos);

    /* Orientation quaternion. */
    const json_value_t *orient_val = json_object_get(item, "orient");
    if (orient_val && orient_val->type == JSON_ARRAY &&
        orient_val->array.count >= 4) {
        snapshot.orientation.x = (float)orient_val->array.items[0].number;
        snapshot.orientation.y = (float)orient_val->array.items[1].number;
        snapshot.orientation.z = (float)orient_val->array.items[2].number;
        snapshot.orientation.w = (float)orient_val->array.items[3].number;
        snapshot.orientation = quat_normalize_safe(
            snapshot.orientation, 1e-8f);
    }

    /* Derive euler cache. */
    {
        static const float RAD_TO_DEG = 180.0f / 3.14159265358979323846f;
        quat_t cq = snapshot.orientation;
        if (cq.w < 0.0f) {
            cq.x = -cq.x; cq.y = -cq.y;
            cq.z = -cq.z; cq.w = -cq.w;
        }
        quat_to_euler_yxz(cq,
                           &snapshot.rot[0], &snapshot.rot[1],
                           &snapshot.rot[2]);
        snapshot.rot[0] *= RAD_TO_DEG;
        snapshot.rot[1] *= RAD_TO_DEG;
        snapshot.rot[2] *= RAD_TO_DEG;
    }

    /* Scale. */
    parse_vec3_(json_object_get(item, "scale"), snapshot.scale);

    /* Preserve pending_delete flag. */
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
