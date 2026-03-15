/**
 * @file scene_frame.c
 * @brief Scene editor per-frame update — server sync, action dispatch.
 *
 * Implements the three public functions declared in scene_frame.h:
 *   - scene_frame_pump: read TCP responses, update local entity store
 *   - scene_frame_dispatch_action: send UI actions as server commands
 *   - scene_frame_request_entity_list: request full entity refresh
 *
 * Non-static functions: 3 (under 4-function limit).
 */

#include "ferrum/editor/scene/scene_frame.h"
#include "ferrum/editor/scene/scene_main.h"
#include "ferrum/editor/scene/scene_cmd.h"
#include "ferrum/editor/scene/scene_connection.h"
#include "ferrum/editor/scene/scene_sync.h"
#include "ferrum/editor/ctrl_cmd_defs.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/math/quat.h"
#include "ferrum/editor/json_parse.h"
#include "ferrum/editor/viewport/transform_basis.h"
#include "ferrum/editor/viewport/viewport_gizmo.h"
#include "ferrum/renderer/video_capture.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Constants                                                                 */
/* ------------------------------------------------------------------------ */

/** Stack buffer size for reading TCP response lines. */
#define RESPONSE_BUF_SIZE 65536

/** Arena size for JSON parsing of entity list responses. */
#define JSON_ARENA_SIZE   65536

/** Maximum number of recently-deleted entity IDs to track as tombstones.
 *  Prevents stale entity list responses from re-creating deleted entities. */
#define DELETE_TOMBSTONE_CAP 1024

/** Tombstone set of recently-deleted entity IDs. Cleared on each
 *  entity list refresh (bump of entity_refresh_gen). */
static uint32_t s_delete_tombstones[DELETE_TOMBSTONE_CAP];
static uint32_t s_delete_tombstone_count;

/* ------------------------------------------------------------------------ */
/* Static helpers                                                            */
/* ------------------------------------------------------------------------ */

/**
 * @brief Parse a vec3 JSON array into a float[3].
 *
 * Reads up to 3 numeric elements from a JSON array value into the
 * output float array. Elements that are missing or non-numeric are
 * left unchanged.
 *
 * @param arr  JSON value of type JSON_ARRAY (may be NULL).
 * @param out  Output float array (must have at least 3 elements).
 */
static void parse_vec3(const json_value_t *arr, float out[3])
{
    if (!arr || arr->type != JSON_ARRAY) {
        return;
    }
    for (int i = 0; i < 3 && (uint32_t)i < arr->array.count; i++) {
        const json_value_t *elem = json_array_get(arr, (uint32_t)i);
        if (elem && elem->type == JSON_NUMBER) {
            out[i] = (float)elem->number;
        }
    }
}

/**
 * @brief Process a list_entities response and rebuild the local entity store.
 *
 * Clears all existing entities from the local store, then recreates each
 * entity from the server's list using edit_entity_store_restore() to
 * preserve server-assigned IDs.
 *
 * Expected JSON structure (already parsed to json_value_t):
 *   {"id":N,"ok":true,"result":[{"id":0,"name":"player","type":"box",
 *     "pos":[x,y,z],"rot":[x,y,z],"scale":[x,y,z]}, ...]}
 *
 * @param ed      Editor context (entities store will be modified).
 * @param result  The "result" JSON array value from the parsed response.
 */
static void process_entity_list(scene_editor_t *ed, const json_value_t *result)
{
    if (!result || result->type != JSON_ARRAY) {
        return;
    }

    /* Restore each entity from the server's list.
     * Clearing is handled by the caller for paginated responses,
     * or done here for legacy plain-array responses. */
    for (uint32_t i = 0; i < result->array.count; i++) {
        const json_value_t *item = json_array_get(result, i);
        if (!item || item->type != JSON_OBJECT) {
            continue;
        }

        /* Extract entity ID (required). */
        const json_value_t *id_val = json_object_get(item, "id");
        if (!id_val || id_val->type != JSON_NUMBER) {
            continue;
        }
        uint32_t eid = (uint32_t)id_val->number;

        /* Skip tombstoned entities — recently confirmed-deleted but
         * appearing in a stale entity list response. */
        bool tombstoned = false;
        for (uint32_t ti = 0; ti < s_delete_tombstone_count; ti++) {
            if (s_delete_tombstones[ti] == eid) {
                tombstoned = true;
                break;
            }
        }
        if (tombstoned) continue;

        /* Build a snapshot with default values. */
        edit_entity_t snapshot;
        memset(&snapshot, 0, sizeof(snapshot));
        snapshot.active = true;
        snapshot.scale[0] = 1.0f;
        snapshot.scale[1] = 1.0f;
        snapshot.scale[2] = 1.0f;
        snapshot.body_index = UINT32_MAX;

        /* Parse type name into type ID. */
        const json_value_t *type_val = json_object_get(item, "type");
        if (type_val && type_val->type == JSON_STRING) {
            /* json_string_copy gives us a null-terminated copy for lookup. */
            char type_name[32];
            memset(type_name, 0, sizeof(type_name));
            json_string_copy(type_val, type_name, sizeof(type_name));
            uint32_t type_id = edit_entity_type_by_name(type_name);
            snapshot.type = (type_id != UINT32_MAX) ? type_id
                                                    : EDIT_ENTITY_TYPE_BOX;
        }

        /* Parse name. */
        const json_value_t *name_val = json_object_get(item, "name");
        if (name_val && name_val->type == JSON_STRING) {
            json_string_copy(name_val, snapshot.name, sizeof(snapshot.name));
        }

        /* Parse position. */
        const json_value_t *pos_val = json_object_get(item, "pos");
        parse_vec3(pos_val, snapshot.pos);

        /* Parse rotation. */
        const json_value_t *rot_val = json_object_get(item, "rot");
        parse_vec3(rot_val, snapshot.rot);

        /* Sync authoritative orientation from euler cache. */
        {
            static const float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;
            snapshot.orientation = quat_from_euler_yxz(
                snapshot.rot[0] * DEG_TO_RAD,
                snapshot.rot[1] * DEG_TO_RAD,
                snapshot.rot[2] * DEG_TO_RAD);
        }

        /* Parse scale (overwrite the 1,1,1 defaults if present). */
        const json_value_t *scale_val = json_object_get(item, "scale");
        parse_vec3(scale_val, snapshot.scale);

        /* Preserve pending_delete flag if the entity is already marked
         * for deletion locally — the server hasn't processed the delete
         * yet, so the entity still appears in list responses. */
        const edit_entity_t *existing = edit_entity_store_get(&ed->entities, eid);
        if (existing && existing->pending_delete) {
            snapshot.pending_delete = true;
        }

        /* Stamp with current refresh generation for stale detection. */
        snapshot.refresh_gen = ed->entity_refresh_gen;

        /* Restore the entity at the server-assigned ID slot. If the
         * entity is already active, update it in place so server-
         * authoritative state (e.g. scale after clone) wins. */
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
}

/**
 * @brief Track a delete command ID so we can suppress errors and retry silently.
 */
static void track_delete_cmd(scene_ui_state_t *ui, uint32_t cmd_id)
{
    if (!ui->delete_cmd_ids) return;
    if (ui->delete_cmd_id_count < ui->delete_cmd_id_cap) {
        ui->delete_cmd_ids[ui->delete_cmd_id_count++] = cmd_id;
    }
}

/**
 * @brief Check if a response ID belongs to a tracked delete command.
 *
 * If found, removes it from the tracking list and returns true.
 */
static bool consume_delete_cmd(scene_ui_state_t *ui, uint32_t resp_id)
{
    for (int i = 0; i < ui->delete_cmd_id_count; i++) {
        if (ui->delete_cmd_ids[i] == resp_id) {
            /* Remove by swapping with last. */
            ui->delete_cmd_ids[i] =
                ui->delete_cmd_ids[ui->delete_cmd_id_count - 1];
            ui->delete_cmd_id_count--;
            return true;
        }
    }
    return false;
}

/**
 * @brief Check if a UX action conflicts with pending deletes.
 *
 * Actions that modify entity selection or the entity list can race
 * with in-flight delete commands and must be buffered.
 */
static bool action_conflicts_with_delete(scene_ui_action_t action)
{
    /* Only select/deselect conflict with in-flight deletes because
     * the server's selection state is being used by the delete retry.
     * Spawns and further deletes can proceed — deletes use pending_delete
     * tracking to avoid double-deleting. */
    switch (action) {
    case UI_ACTION_SELECT_ENTITY:
    case UI_ACTION_DESELECT_ENTITY:
        return true;
    default:
        return false;
    }
}

/**
 * @brief Buffer a UX action for later replay after pending deletes resolve.
 */
static void buffer_action(scene_ui_state_t *ui, scene_ui_action_t action,
                           uint32_t target)
{
    if (ui->action_q_count >= UI_ACTION_Q_MAX) return;
    ui->action_q_actions[ui->action_q_count] = action;
    ui->action_q_targets[ui->action_q_count] = target;
    ui->action_q_count++;
}

/**
 * @brief Reconcile pending deletes after an entity list refresh.
 *
 * For each pending delete ID:
 * - If the entity reappeared in the store: re-flag it as pending_delete
 *   and re-send a delete command (the previous delete raced with the list).
 * - If the entity is gone: remove it from the pending list (confirmed deleted).
 */
static void reconcile_pending_deletes(scene_editor_t *ed)
{
    if (!ed->ui.pending_delete_ids || ed->ui.pending_delete_count == 0) return;

    uint32_t write = 0;
    bool need_retry = false;

    for (uint32_t i = 0; i < ed->ui.pending_delete_count; i++) {
        uint32_t eid = ed->ui.pending_delete_ids[i];
        edit_entity_t *ent = edit_entity_store_get_mut(&ed->entities, eid);

        if (ent) {
            /* Entity reappeared — re-flag and keep in pending list. */
            ent->pending_delete = true;
            ed->ui.pending_delete_ids[write] = eid;
            ed->ui.pending_delete_log_ids[write] =
                ed->ui.pending_delete_log_ids[i];
            write++;
            need_retry = true;
        } else {
            /* Entity gone — confirmed deleted. Resolve log entry as success. */
            uint32_t log_id = ed->ui.pending_delete_log_ids[i];
            if (log_id != 0) {
                scene_ui_tui_log_resolve(&ed->ui, log_id, true);
            }
        }
    }

    ed->ui.pending_delete_count = write;

    /* Re-send delete_id for entities that reappeared.
     * Uses per-entity delete_id to avoid selection sync races. */
    if (need_retry && write > 0) {
        for (uint32_t i = 0; i < write; i++) {
            char cmd_buf[256];
            uint32_t cid = scene_connection_next_id(&ed->connection);
            int len = snprintf(cmd_buf, sizeof(cmd_buf),
                "{\"id\":%u,\"cmd\":\"delete_id\",\"args\":"
                "{\"entity_id\":%u}}\n",
                (unsigned)cid,
                (unsigned)ed->ui.pending_delete_ids[i]);
            if (len > 0) {
                ctrl_conn_send_raw(&ed->connection.tcp,
                                   cmd_buf, (uint32_t)len);
                scene_sync_mark_sent(&ed->sync);
                track_delete_cmd(&ed->ui, cid);
            }
            ed->ui.pending_delete_log_ids[i] = cid;
        }
        /* Reset retry timer (next retry after the refresh comes back). */
        ed->ui.delete_retry_timer = UI_DELETE_RETRY_INTERVAL;
    }
}

/**
 * @brief Push a command string into TUI history ring buffer.
 *
 * Skips empty strings and duplicates of the most recent entry.
 */
static void tui_history_push(scene_ui_state_t *ui, const char *cmd)
{
    if (!cmd || cmd[0] == '\0') return;

    if (ui->tui_history_count > 0) {
        int prev = (ui->tui_history_head - 1 + UI_TUI_HISTORY_MAX)
                   % UI_TUI_HISTORY_MAX;
        if (strcmp(ui->tui_history[prev], cmd) == 0) return;
    }

    strncpy(ui->tui_history[ui->tui_history_head], cmd,
            UI_TUI_INPUT_MAX - 1);
    ui->tui_history[ui->tui_history_head][UI_TUI_INPUT_MAX - 1] = '\0';
    ui->tui_history_head = (ui->tui_history_head + 1) % UI_TUI_HISTORY_MAX;
    if (ui->tui_history_count < UI_TUI_HISTORY_MAX)
        ui->tui_history_count++;
}

/**
 * @brief Determine if a response contains an entity-modifying result.
 *
 * A numeric result indicates a spawn (entity ID returned) or delete
 * (count returned). A boolean result indicates select/deselect success.
 * In both cases the local entity list may be stale, so the caller
 * should request a full refresh after entity-modifying commands.
 *
 * @param resp  Parsed command response.
 * @return true if the response indicates an entity-modifying command.
 */
static bool is_entity_modifying_response(const scene_cmd_response_t *resp)
{
    if (!resp->ok || !resp->has_result) {
        return false;
    }
    /* Only numeric results indicate entity-modifying commands:
     * spawn returns entity ID, delete returns count.
     * Boolean results (select/deselect) don't change the entity list.
     * Array results (entity list) are handled separately in pump. */
    return resp->result_is_number;
}

/**
 * @brief Dispatch a TUI command string.
 *
 * Supports simple built-in commands:
 *   spawn box|sphere|capsule [name]
 *   delete
 *   list
 *   {raw JSON}
 *
 * Logs the result or error to the TUI.
 */
/**
 * @brief Push a command into the offline queue.
 *
 * When disconnected, TUI commands are buffered here and flushed
 * when the connection is restored.
 */
static void offline_q_push(scene_ui_state_t *ui, const char *cmd)
{
    if (!ui->offline_q || !cmd) return;
    if (ui->offline_q_count >= UI_TUI_OFFLINE_Q_MAX) {
        scene_ui_tui_log_error(ui, "Offline queue full — command dropped");
        return;
    }
    int slot = (ui->offline_q_head + ui->offline_q_count) % UI_TUI_OFFLINE_Q_MAX;
    strncpy(ui->offline_q[slot], cmd, UI_TUI_INPUT_MAX - 1);
    ui->offline_q[slot][UI_TUI_INPUT_MAX - 1] = '\0';
    ui->offline_q_count++;

    char msg[UI_TUI_LOG_LINE];
    snprintf(msg, sizeof(msg), "(queued) %s", cmd);
    scene_ui_tui_log(ui, msg);
}

static void dispatch_tui_command(scene_editor_t *ed)
{
    const char *cmd = ed->ui.tui_cmd;
    char buf[2048];

    /* If disconnected, queue non-local commands for later. */
    bool is_connected = (ed->connection.state == SCENE_CONN_CONNECTED);
    if (!is_connected) {
        /* Check if this is a local-only command (help, ?) that doesn't
         * need a server connection. Everything else gets queued. */
        bool is_local = false;
        if (cmd[0] != '{') {
            char first[64];
            int fi = 0;
            const char *p = cmd;
            while (*p && *p != ' ' && fi < (int)sizeof(first) - 1)
                first[fi++] = *p++;
            first[fi] = '\0';
            if (strcmp(first, "help") == 0) is_local = true;
            if (strcmp(first, "basis") == 0) is_local = true;
            if (strcmp(first, "stream") == 0) is_local = true;
            size_t clen = strlen(cmd);
            if (clen >= 2 && cmd[clen - 1] == '?') is_local = true;
        }
        if (!is_local) {
            offline_q_push(&ed->ui, cmd);
            return;
        }
    }

    /* Handle raw JSON passthrough. */
    if (cmd[0] == '{') {
        int len = snprintf(buf, sizeof(buf), "%s\n", cmd);
        if (len > 0) {
            ctrl_conn_send_raw(&ed->connection.tcp, buf, (uint32_t)len);
            scene_sync_mark_sent(&ed->sync);
        }
        return;
    }

    /* Extract command word for lookup and help queries. */
    char word[64];
    {
        int wi = 0;
        const char *p = cmd;
        while (*p && *p != ' ' && wi < (int)sizeof(word) - 1) {
            word[wi++] = *p++;
        }
        word[wi] = '\0';
    }

    /* Built-in: help [command] */
    if (strcmp(word, "help") == 0) {
        const char *rest = cmd + 4;
        while (*rest == ' ') rest++;
        if (*rest != '\0') {
            /* Help for a specific command. */
            const ctrl_cmd_def_t *def = ctrl_cmd_defs_find(rest);
            if (def) {
                scene_ui_tui_log(&ed->ui, def->usage);
                scene_ui_tui_log(&ed->ui, def->help);
            } else {
                char err[UI_TUI_LOG_LINE];
                snprintf(err, sizeof(err), "Unknown command: %s", rest);
                scene_ui_tui_log_error(&ed->ui, err);
            }
        } else {
            /* General help: list some common commands. */
            scene_ui_tui_log(&ed->ui, "Commands (use 'help <cmd>' for details):");
            scene_ui_tui_log(&ed->ui, "  spawn, delete, select, deselect");
            scene_ui_tui_log(&ed->ui, "  move, rotate, scale, list_entities");
            scene_ui_tui_log(&ed->ui, "  save, load, physics_pause/resume/step");
            scene_ui_tui_log(&ed->ui, "  Type 'help <command>' for usage.");
        }
        return;
    }

    /* Check for "command ?" syntax — show usage. */
    {
        size_t clen = strlen(cmd);
        if (clen >= 2 && cmd[clen - 1] == '?' &&
            (cmd[clen - 2] == ' ' || clen == 1)) {
            const ctrl_cmd_def_t *def = ctrl_cmd_defs_find(word);
            if (def) {
                scene_ui_tui_log(&ed->ui, def->usage);
                scene_ui_tui_log(&ed->ui, def->help);
            } else {
                char err[UI_TUI_LOG_LINE];
                snprintf(err, sizeof(err), "Unknown command: %s", word);
                scene_ui_tui_log_error(&ed->ui, err);
            }
            return;
        }
    }

    /* Built-in local command: basis [world|local|view|cursor] */
    if (strcmp(word, "basis") == 0) {
        const char *rest = cmd + 5;
        while (*rest == ' ') rest++;
        if (*rest == '\0') {
            /* No argument: cycle to next basis. */
            scene_focused_vp(ed)->gizmo.basis = transform_basis_next(scene_focused_vp(ed)->gizmo.basis);
        } else if (strcmp(rest, "world") == 0) {
            scene_focused_vp(ed)->gizmo.basis = TRANSFORM_BASIS_WORLD;
        } else if (strcmp(rest, "local") == 0) {
            scene_focused_vp(ed)->gizmo.basis = TRANSFORM_BASIS_LOCAL;
        } else if (strcmp(rest, "view") == 0) {
            scene_focused_vp(ed)->gizmo.basis = TRANSFORM_BASIS_VIEW;
        } else if (strncmp(rest, "@cursor", 7) == 0 ||
                   strcmp(rest, "cursor") == 0) {
            scene_focused_vp(ed)->gizmo.basis = TRANSFORM_BASIS_CURSOR;
        } else {
            scene_ui_tui_log_error(&ed->ui,
                "Usage: basis [world|local|view|@cursor]");
            return;
        }
        char msg[64];
        snprintf(msg, sizeof(msg), "Basis: %s",
                 transform_basis_name(scene_focused_vp(ed)->gizmo.basis));
        scene_ui_tui_log_success(&ed->ui, msg);
        return;
    }

    /* Built-in local command: stream [filename.mp4]
     * Starts or stops video capture. With a filename argument, starts
     * capture to that file. Without arguments, stops active capture. */
    if (strcmp(word, "stream") == 0) {
        const char *rest = cmd + 6;
        while (*rest == ' ') rest++;

        if (ed->capture && *rest == '\0') {
            /* Stop active capture. */
            uint64_t frames = fr_video_capture_frames_written(ed->capture);
            fr_video_capture_destroy(ed->capture);
            ed->capture = NULL;
            char msg[UI_TUI_LOG_LINE];
            snprintf(msg, sizeof(msg),
                     "Stream stopped (%llu frames written)",
                     (unsigned long long)frames);
            scene_ui_tui_log_success(&ed->ui, msg);
        } else if (*rest != '\0') {
            /* Start (or restart) capture. */
            if (ed->capture) {
                fr_video_capture_destroy(ed->capture);
                ed->capture = NULL;
            }
            int dw, dh;
            SDL_GL_GetDrawableSize(ed->window, &dw, &dh);
            fr_video_capture_desc_t desc = {
                .width = dw,
                .height = dh,
                .fps = 30,
                .output_path = rest,
            };
            ed->capture = fr_video_capture_create(&desc);
            if (ed->capture) {
                char msg[UI_TUI_LOG_LINE];
                snprintf(msg, sizeof(msg),
                         "Streaming %dx%d @30fps -> %s", dw, dh, rest);
                scene_ui_tui_log_success(&ed->ui, msg);
            } else {
                scene_ui_tui_log_error(&ed->ui,
                    "Failed to start stream (ffmpeg on PATH?)");
            }
        } else {
            scene_ui_tui_log_error(&ed->ui,
                "Usage: stream <file.mp4> | stream (to stop)");
        }
        return;
    }

    /* Look up command definition for validation. */
    const ctrl_cmd_def_t *def = ctrl_cmd_defs_find(word);
    if (!def) {
        char err[UI_TUI_LOG_LINE];
        snprintf(err, sizeof(err),
                 "Unknown command: %s (type 'help' for usage)", word);
        scene_ui_tui_log_error(&ed->ui, err);
        return;
    }

    /* Use ctrl_cmd_build_json to convert text to JSON command. */
    uint32_t cmd_id = scene_connection_next_id(&ed->connection);
    uint32_t json_len = ctrl_cmd_build_json(cmd, buf, sizeof(buf), cmd_id);
    if (json_len == 0) {
        /* Build failed — show usage from the definition table. */
        char err[UI_TUI_LOG_LINE];
        snprintf(err, sizeof(err), "Usage: %s", def->usage);
        scene_ui_tui_log_error(&ed->ui, err);
        return;
    }

    /* Send the JSON command to the server. */
    ctrl_conn_send_raw(&ed->connection.tcp, buf, json_len);
    scene_sync_mark_sent(&ed->sync);

    /* Entity-modifying commands need a follow-up list refresh. */
    const char *wire = def->name;
    if (strcmp(wire, "spawn") == 0 || strcmp(wire, "delete") == 0 ||
        strcmp(wire, "delete_id") == 0 || strcmp(wire, "clone") == 0 ||
        strcmp(wire, "entity_def") == 0) {
        scene_frame_request_entity_list(ed);
    }
}

/* ------------------------------------------------------------------------ */
/* Public functions                                                          */
/* ------------------------------------------------------------------------ */

void scene_frame_pump(struct scene_editor *ed)
{
    if (!ed) {
        return;
    }

    /* Read available TCP data into the connection's receive buffer. */
    scene_connection_pump(&ed->connection);

    /* Static buffer for extracting individual response lines.
     * Must be large enough for paginated entity list responses. */
    static char resp_buf[RESPONSE_BUF_SIZE];
    bool needs_entity_refresh = false;

    /* Process all complete response lines available. */
    for (;;) {
        uint32_t resp_len = scene_connection_pop_response(
            &ed->connection, resp_buf, (uint32_t)sizeof(resp_buf));
        if (resp_len == 0) {
            break;
        }

        /* Parse the JSON response into the structured response type. */
        scene_cmd_response_t resp;
        memset(&resp, 0, sizeof(resp));
        if (!scene_cmd_parse_response(resp_buf, (size_t)resp_len, &resp)) {
            /* Malformed response — skip it. */
            continue;
        }

        /* Track sync state: decrement in-flight counter on ack. */
        bool is_delete_resp = consume_delete_cmd(&ed->ui, resp.id);
        if (resp.ok) {
            scene_sync_mark_acked(&ed->sync);
            scene_sync_update_state(&ed->sync);
            /* Backfill the log entry for this command with green ok. */
            scene_ui_tui_log_resolve(&ed->ui, resp.id, true);

            /* If this was a delete command, immediately remove
             * confirmed-deleted entities from the store so the
             * outliner updates without waiting for a list refresh. */
            if (is_delete_resp) {
                uint32_t write2 = 0;
                for (uint32_t pi = 0;
                     pi < ed->ui.pending_delete_count; pi++) {
                    if (ed->ui.pending_delete_log_ids[pi] == resp.id) {
                        uint32_t eid = ed->ui.pending_delete_ids[pi];
                        edit_selection_remove(&ed->selection, eid);
                        edit_entity_store_remove(&ed->entities, eid);
                        /* Record tombstone so stale entity list responses
                         * don't re-create this entity. */
                        if (s_delete_tombstone_count < DELETE_TOMBSTONE_CAP) {
                            s_delete_tombstones[s_delete_tombstone_count++] =
                                eid;
                        }
                    } else {
                        ed->ui.pending_delete_ids[write2] =
                            ed->ui.pending_delete_ids[pi];
                        ed->ui.pending_delete_log_ids[write2] =
                            ed->ui.pending_delete_log_ids[pi];
                        write2++;
                    }
                }
                ed->ui.pending_delete_count = write2;
            }
        } else if (is_delete_resp) {
            /* Suppress delete errors — retries happen automatically
             * via reconcile_pending_deletes after entity list refresh. */
            scene_sync_mark_acked(&ed->sync);
            scene_sync_update_state(&ed->sync);
        } else {
            /* Backfill the log entry with red X, and log the error. */
            scene_ui_tui_log_resolve(&ed->ui, resp.id, false);
            char err_line[UI_TUI_LOG_LINE];
            snprintf(err_line, sizeof(err_line), "Error: %s", resp.error);
            scene_ui_tui_log_error(&ed->ui, err_line);
        }

        /* Try to parse the full JSON to check if the result contains
         * a paginated entity list response. */
        static uint8_t arena_buf[JSON_ARENA_SIZE];
        json_arena_t arena;
        json_arena_init(&arena, arena_buf, sizeof(arena_buf));

        json_value_t root;
        if (json_parse(resp_buf, (size_t)resp_len, &arena, &root)) {
            const json_value_t *result_val = json_object_get(&root, "result");

            /* Legacy: plain array result (backward compat). */
            if (result_val && result_val->type == JSON_ARRAY) {
                ed->entity_refresh_gen++;
                process_entity_list(ed, result_val);
                /* Remove confirmed-deleted entities (pending_delete + stale gen). */
                for (uint32_t ei = 0; ei < ed->entities.capacity; ei++) {
                    edit_entity_t *ent =
                        edit_entity_store_get_mut(&ed->entities, ei);
                    if (ent && ent->active && ent->pending_delete &&
                        ent->refresh_gen != ed->entity_refresh_gen) {
                        edit_selection_remove(&ed->selection, ei);
                        edit_entity_store_remove(&ed->entities, ei);
                    }
                }
                reconcile_pending_deletes(ed);
                s_delete_tombstone_count = 0;
                continue;
            }

            /* Paginated: {"entities":[...], "total":N, "offset":N} */
            if (result_val && result_val->type == JSON_OBJECT) {
                const json_value_t *ents_val =
                    json_object_get(result_val, "entities");
                const json_value_t *total_val =
                    json_object_get(result_val, "total");
                const json_value_t *offset_val =
                    json_object_get(result_val, "offset");

                if (ents_val && ents_val->type == JSON_ARRAY) {
                    uint32_t offset = 0;
                    uint32_t total = 0;
                    if (offset_val && offset_val->type == JSON_NUMBER)
                        offset = (uint32_t)offset_val->number;
                    if (total_val && total_val->type == JSON_NUMBER)
                        total = (uint32_t)total_val->number;

                    /* First page: bump refresh generation. */
                    if (offset == 0) {
                        ed->entity_refresh_gen++;
                    }

                    /* Restore entities from this page (stamps refresh_gen). */
                    process_entity_list(ed, ents_val);

                    /* Check if this is the last page. */
                    uint32_t received = offset + ents_val->array.count;
                    if (received >= total) {
                        /* Remove entities that are pending_delete AND were
                         * not in this refresh (server confirmed deletion).
                         * Normal entities are kept to avoid selection flicker. */
                        for (uint32_t ei = 0; ei < ed->entities.capacity;
                             ei++) {
                            edit_entity_t *ent =
                                edit_entity_store_get_mut(&ed->entities, ei);
                            if (ent && ent->active && ent->pending_delete &&
                                ent->refresh_gen != ed->entity_refresh_gen) {
                                edit_selection_remove(&ed->selection, ei);
                                edit_entity_store_remove(&ed->entities, ei);
                            }
                        }
                        /* Full refresh complete — reconcile pending deletes
                         * and clear tombstones (server list is authoritative). */
                        reconcile_pending_deletes(ed);
                        s_delete_tombstone_count = 0;
                    }

                    /* Request next page if more entities remain. */
                    if (received < total) {
                        char cmd_buf2[256];
                        uint32_t cid =
                            scene_connection_next_id(&ed->connection);
                        int n = snprintf(cmd_buf2, sizeof(cmd_buf2),
                            "{\"id\":%u,\"cmd\":\"list_entities\","
                            "\"args\":{\"offset\":%u}}\n",
                            cid, received);
                        if (n > 0) {
                            ctrl_conn_send_raw(&ed->connection.tcp,
                                cmd_buf2, (uint32_t)n);
                            scene_sync_mark_sent(&ed->sync);
                        }
                    }
                    continue;
                }
            }
        }

        /* For non-array successful responses, check if entity-modifying. */
        if (is_entity_modifying_response(&resp)) {
            needs_entity_refresh = true;
        }
    }

    /* If any entity-modifying command succeeded, request a full refresh
     * so the local store stays in sync with the server. */
    if (needs_entity_refresh) {
        scene_frame_request_entity_list(ed);
    }

    /* Periodic retry for pending deletes: every N frames, request an entity
     * list refresh which triggers reconcile_pending_deletes to re-send
     * delete commands for entities that still exist on the server. */
    if (ed->ui.pending_delete_count > 0) {
        if (ed->ui.delete_retry_timer == 0) {
            ed->ui.delete_retry_timer = UI_DELETE_RETRY_INTERVAL;
            scene_frame_request_entity_list(ed);
        } else {
            ed->ui.delete_retry_timer--;
        }
    }

    /* When all pending deletes have resolved, replay buffered UX actions. */
    if (ed->ui.pending_delete_count == 0 && ed->ui.action_q_count > 0) {
        for (int i = 0; i < ed->ui.action_q_count; i++) {
            ed->ui.action = ed->ui.action_q_actions[i];
            ed->ui.action_target = ed->ui.action_q_targets[i];
            scene_frame_dispatch_action(ed);
        }
        ed->ui.action_q_count = 0;
    }
}

void scene_frame_dispatch_action(struct scene_editor *ed)
{
    if (!ed || ed->ui.action == UI_ACTION_NONE) {
        return;
    }

    /* When disconnected, only allow TUI input and local-only actions.
     * TUI commands are buffered and sent when the connection is restored. */
    if (ed->connection.state != SCENE_CONN_CONNECTED) {
        switch (ed->ui.action) {
        case UI_ACTION_TUI_COMMAND:
        case UI_ACTION_MODE_NONE:
        case UI_ACTION_MODE_TRANSLATE:
        case UI_ACTION_MODE_ROTATE:
        case UI_ACTION_MODE_SCALE:
            /* These are allowed offline. */
            break;
        default:
            /* All other actions require a live connection. */
            ed->ui.action = UI_ACTION_NONE;
            ed->ui.action_target = 0;
            return;
        }
    }

    /* While deletes are pending, buffer any conflicting actions
     * (select, deselect, spawn, delete) to avoid racing with
     * in-flight delete commands on the server. */
    if (ed->ui.pending_delete_count > 0 &&
        action_conflicts_with_delete(ed->ui.action)) {
        buffer_action(&ed->ui, ed->ui.action, ed->ui.action_target);
        ed->ui.action = UI_ACTION_NONE;
        ed->ui.action_target = 0;
        return;
    }

    char cmd_buf[1024];
    char echo[UI_TUI_LOG_LINE]; /* Human-readable command echo for TUI. */
    echo[0] = '\0';
    int cmd_len = 0;
    uint32_t cmd_id = 0;
    bool is_entity_modify = false;

    switch (ed->ui.action) {
    case UI_ACTION_SPAWN_BOX:
    case UI_ACTION_SPAWN_SPHERE:
    case UI_ACTION_SPAWN_CAPSULE: {
        /* Determine entity type from the action. */
        uint32_t entity_type = EDIT_ENTITY_TYPE_BOX;
        const char *type_name = "box";
        const char *type_prefix = "Box";
        if (ed->ui.action == UI_ACTION_SPAWN_SPHERE) {
            entity_type = EDIT_ENTITY_TYPE_SPHERE;
            type_name = "sphere";
            type_prefix = "Sphere";
        } else if (ed->ui.action == UI_ACTION_SPAWN_CAPSULE) {
            entity_type = EDIT_ENTITY_TYPE_CAPSULE;
            type_name = "capsule";
            type_prefix = "Capsule";
        }

        /* Auto-generate a name using the spawn counter. */
        ed->ui.spawn_counter++;
        char entity_name[EDIT_ENTITY_NAME_MAX];
        snprintf(entity_name, sizeof(entity_name), "%s%u",
                 type_prefix, ed->ui.spawn_counter);

        /* Spawn at the origin. */
        float origin[3] = {0.0f, 0.0f, 0.0f};

        cmd_id = scene_connection_next_id(&ed->connection);
        cmd_len = scene_cmd_format_spawn(
            cmd_buf, sizeof(cmd_buf), cmd_id, entity_type, origin, entity_name);
        is_entity_modify = true;

        /* Echo to TUI + history (include position). */
        snprintf(echo, sizeof(echo), "spawn %s %s %.4g %.4g %.4g",
                 type_name, entity_name,
                 (double)origin[0], (double)origin[1], (double)origin[2]);
        break;
    }

    case UI_ACTION_SELECT_ENTITY: {
        cmd_id = scene_connection_next_id(&ed->connection);
        cmd_len = scene_cmd_format_select(
            cmd_buf, sizeof(cmd_buf), cmd_id, ed->ui.action_target);

        /* Optimistically update local selection. */
        edit_selection_add(&ed->selection, ed->ui.action_target);
        /* New selection always becomes the active object. */
        ed->active_object_id = ed->ui.action_target;

        /* Track for outliner range operations. */
        ed->ui.outliner_last_click_id = ed->ui.action_target;
        ed->ui.outliner_last_was_select = true;

        snprintf(echo, sizeof(echo), "select entity_id=%u",
                 ed->ui.action_target);
        break;
    }

    case UI_ACTION_DESELECT_ENTITY: {
        cmd_id = scene_connection_next_id(&ed->connection);
        cmd_len = scene_cmd_format_deselect(
            cmd_buf, sizeof(cmd_buf), cmd_id, ed->ui.action_target);

        /* Optimistically update local selection. */
        edit_selection_remove(&ed->selection, ed->ui.action_target);

        /* If we deselected the active object, pick another. */
        if (ed->active_object_id == ed->ui.action_target) {
            if (edit_selection_count(&ed->selection) > 0) {
                const uint32_t *ids = edit_selection_ids(&ed->selection);
                ed->active_object_id = ids[0];
            } else {
                ed->active_object_id = EDIT_ENTITY_INVALID_ID;
            }
        }

        /* Track for outliner range operations. */
        ed->ui.outliner_last_click_id = ed->ui.action_target;
        ed->ui.outliner_last_was_select = false;

        snprintf(echo, sizeof(echo), "deselect entity_id=%u",
                 ed->ui.action_target);
        break;
    }

    case UI_ACTION_REPLACE_SELECTION: {
        /* Clear existing selection, then select the target. */
        /* Send deselect commands for currently selected entities first. */
        {
            uint32_t sel_count = edit_selection_count(&ed->selection);
            const uint32_t *sel_ids = edit_selection_ids(&ed->selection);
            for (uint32_t si = 0; si < sel_count; ++si) {
                if (sel_ids[si] == ed->ui.action_target) continue;
                uint32_t dcmd = scene_connection_next_id(&ed->connection);
                int dlen = scene_cmd_format_deselect(
                    cmd_buf, sizeof(cmd_buf), dcmd, sel_ids[si]);
                if (dlen > 0 && ed->connected) {
                    ctrl_conn_send_raw(&ed->connection.tcp,
                                       cmd_buf, (uint32_t)dlen);
                }
            }
        }
        edit_selection_clear(&ed->selection);
        edit_selection_add(&ed->selection, ed->ui.action_target);
        ed->active_object_id = ed->ui.action_target;

        /* Track for range operations. */
        ed->ui.outliner_last_click_id = ed->ui.action_target;
        ed->ui.outliner_last_was_select = true;

        cmd_id = scene_connection_next_id(&ed->connection);
        cmd_len = scene_cmd_format_select(
            cmd_buf, sizeof(cmd_buf), cmd_id, ed->ui.action_target);

        snprintf(echo, sizeof(echo), "select (replace) entity_id=%u",
                 ed->ui.action_target);
        break;
    }

    case UI_ACTION_RANGE_SELECT: {
        /* Shift-click: select all entities between last clicked and target.
         * Noop if last outliner click was a deselect. */
        if (!ed->ui.outliner_last_was_select) break;

        uint32_t from = ed->ui.outliner_last_click_id;
        uint32_t to = ed->ui.action_target;
        uint32_t lo = from < to ? from : to;
        uint32_t hi = from < to ? to : from;
        uint32_t cap = ed->entities.capacity;
        if (hi >= cap) hi = cap - 1;

        for (uint32_t i = lo; i <= hi; ++i) {
            const edit_entity_t *ent =
                edit_entity_store_get(&ed->entities, i);
            if (!ent) continue;
            edit_selection_add(&ed->selection, i);
        }
        ed->active_object_id = ed->ui.action_target;

        snprintf(echo, sizeof(echo), "range select %u..%u", lo, hi);
        break;
    }

    case UI_ACTION_RANGE_DESELECT: {
        /* Ctrl+Shift-click: deselect all between last clicked and target.
         * Noop if last outliner click was a select. */
        if (ed->ui.outliner_last_was_select) break;

        uint32_t from = ed->ui.outliner_last_click_id;
        uint32_t to = ed->ui.action_target;
        uint32_t lo = from < to ? from : to;
        uint32_t hi = from < to ? to : from;
        uint32_t cap = ed->entities.capacity;
        if (hi >= cap) hi = cap - 1;

        for (uint32_t i = lo; i <= hi; ++i) {
            edit_selection_remove(&ed->selection, i);
        }
        /* Update active object. */
        if (edit_selection_count(&ed->selection) > 0) {
            const uint32_t *ids = edit_selection_ids(&ed->selection);
            ed->active_object_id = ids[0];
        } else {
            ed->active_object_id = EDIT_ENTITY_INVALID_ID;
        }

        snprintf(echo, sizeof(echo), "range deselect %u..%u", lo, hi);
        break;
    }

    case UI_ACTION_DELETE_SELECTED: {
        uint32_t sel_count = edit_selection_count(&ed->selection);
        if (sel_count == 0) break;

        /* Send per-entity delete_id commands so we don't depend on
         * server selection being in sync (avoids race conditions). */
        const uint32_t *sel_ids = edit_selection_ids(&ed->selection);
        uint32_t last_deleted = EDIT_ENTITY_INVALID_ID;
        for (uint32_t si = 0; si < sel_count; si++) {
            uint32_t eid = sel_ids[si];
            edit_entity_t *ent =
                edit_entity_store_get_mut(&ed->entities, eid);
            if (ent) {
                ent->pending_delete = true;
                last_deleted = eid;
            }

            /* Send delete_id for this specific entity. */
            uint32_t del_cid = scene_connection_next_id(&ed->connection);
            char del_buf[256];
            int del_n = snprintf(del_buf, sizeof(del_buf),
                "{\"id\":%u,\"cmd\":\"delete_id\",\"args\":"
                "{\"entity_id\":%u}}\n",
                (unsigned)del_cid, (unsigned)eid);
            if (del_n > 0 && ed->connected) {
                ctrl_conn_send_raw(&ed->connection.tcp,
                                   del_buf, (uint32_t)del_n);
                scene_sync_mark_sent(&ed->sync);
            }
            track_delete_cmd(&ed->ui, del_cid);

            /* Add to pending ID list (survives entity list refresh). */
            if (ed->ui.pending_delete_ids &&
                ed->ui.pending_delete_count < ed->ui.pending_delete_cap) {
                ed->ui.pending_delete_ids[ed->ui.pending_delete_count] = eid;
                ed->ui.pending_delete_log_ids[ed->ui.pending_delete_count] =
                    del_cid;
                ed->ui.pending_delete_count++;
            }
        }

        /* Echo delete with the entity IDs being deleted (before clear). */
        {
            int ew = snprintf(echo, sizeof(echo), "delete ids=[");
            for (uint32_t si = 0; si < sel_count && ew < (int)sizeof(echo) - 8; si++) {
                if (si > 0) ew += snprintf(echo + ew, sizeof(echo) - (size_t)ew, ",");
                ew += snprintf(echo + ew, sizeof(echo) - (size_t)ew, "%u", sel_ids[si]);
            }
            snprintf(echo + ew, sizeof(echo) - (size_t)ew, "]");
        }

        /* Don't send a single "delete" command — we already sent
         * per-entity delete_id commands above. Just set flags. */
        cmd_len = 0;
        is_entity_modify = true;
        ed->ui.delete_retry_timer = UI_DELETE_RETRY_INTERVAL;

        /* Clear selection and auto-select the next active entity
         * so the user can rapidly delete in sequence. */
        edit_selection_clear(&ed->selection);

        /* Auto-select: try below first, then above. */
        ed->active_object_id = EDIT_ENTITY_INVALID_ID;
        if (last_deleted != EDIT_ENTITY_INVALID_ID) {
            bool found = false;
            /* Try below. */
            for (uint32_t ni = last_deleted + 1;
                 ni < ed->entities.capacity; ni++) {
                const edit_entity_t *next =
                    edit_entity_store_get(&ed->entities, ni);
                if (next && !next->pending_delete) {
                    edit_selection_add(&ed->selection, ni);
                    ed->active_object_id = ni;
                    found = true;
                    break;
                }
            }
            /* Try above if nothing below. */
            if (!found && last_deleted > 0) {
                for (uint32_t ni = last_deleted - 1; ni != UINT32_MAX; ni--) {
                    const edit_entity_t *prev =
                        edit_entity_store_get(&ed->entities, ni);
                    if (prev && !prev->pending_delete) {
                        edit_selection_add(&ed->selection, ni);
                        ed->active_object_id = ni;
                        break;
                    }
                }
            }
        }
        break;
    }

    case UI_ACTION_MODE_NONE:
        ed->ui.transform_mode = UI_MODE_SELECT;
        gizmo_state_set_mode(&scene_focused_vp(ed)->gizmo, GIZMO_MODE_NONE);
        break;

    case UI_ACTION_MODE_TRANSLATE:
        ed->ui.transform_mode = UI_MODE_TRANSLATE;
        gizmo_state_set_mode(&scene_focused_vp(ed)->gizmo, GIZMO_MODE_TRANSLATE);
        break;

    case UI_ACTION_MODE_ROTATE:
        ed->ui.transform_mode = UI_MODE_ROTATE;
        gizmo_state_set_mode(&scene_focused_vp(ed)->gizmo, GIZMO_MODE_ROTATE);
        break;

    case UI_ACTION_MODE_SCALE:
        ed->ui.transform_mode = UI_MODE_SCALE;
        gizmo_state_set_mode(&scene_focused_vp(ed)->gizmo, GIZMO_MODE_SCALE);
        break;

    case UI_ACTION_TUI_COMMAND:
        dispatch_tui_command(ed);
        break;

    case UI_ACTION_NONE:
        /* Already checked above, but satisfy the compiler. */
        break;
    }

    /* Send the formatted command if we produced one. */
    if (cmd_len > 0) {
        ctrl_conn_send_raw(&ed->connection.tcp, cmd_buf, (uint32_t)cmd_len);
        scene_sync_mark_sent(&ed->sync);

        /* Echo command with pending status — backfilled to ok/X
         * when the server response arrives. */
        if (echo[0] != '\0') {
            scene_ui_tui_log_pending(&ed->ui, echo, cmd_id);
            tui_history_push(&ed->ui, echo);
        }

        /* For entity-modifying commands (spawn, delete), also queue a
         * list_entities request so the local store refreshes when the
         * server has processed both commands in order. */
        if (is_entity_modify) {
            scene_frame_request_entity_list(ed);
        }
    }

    /* Clear the action so it is not re-dispatched next frame. */
    ed->ui.action = UI_ACTION_NONE;
    ed->ui.action_target = 0;
}

void scene_frame_request_entity_list(struct scene_editor *ed)
{
    if (!ed) {
        return;
    }

    char cmd_buf[256];
    uint32_t cmd_id = scene_connection_next_id(&ed->connection);
    int cmd_len = scene_cmd_format_list(cmd_buf, sizeof(cmd_buf), cmd_id);

    if (cmd_len > 0) {
        ctrl_conn_send_raw(&ed->connection.tcp, cmd_buf, (uint32_t)cmd_len);
        scene_sync_mark_sent(&ed->sync);
    }
}

void scene_frame_flush_offline_queue(struct scene_editor *ed)
{
    if (!ed || !ed->ui.offline_q) return;

    int count = ed->ui.offline_q_count;
    if (count == 0) return;

    char msg[UI_TUI_LOG_LINE];
    snprintf(msg, sizeof(msg), "Flushing %d queued command(s)...", count);
    scene_ui_tui_log(&ed->ui, msg);

    for (int i = 0; i < count; i++) {
        int slot = (ed->ui.offline_q_head + i) % UI_TUI_OFFLINE_Q_MAX;
        /* Replay by copying into tui_cmd and dispatching. */
        strncpy(ed->ui.tui_cmd, ed->ui.offline_q[slot], UI_TUI_INPUT_MAX - 1);
        ed->ui.tui_cmd[UI_TUI_INPUT_MAX - 1] = '\0';
        dispatch_tui_command(ed);
    }

    ed->ui.offline_q_head = 0;
    ed->ui.offline_q_count = 0;
}
