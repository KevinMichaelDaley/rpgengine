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
#include "ferrum/editor/json_parse.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Constants                                                                 */
/* ------------------------------------------------------------------------ */

/** Stack buffer size for reading TCP response lines. */
#define RESPONSE_BUF_SIZE 8192

/** Arena size for JSON parsing of entity list responses. */
#define JSON_ARENA_SIZE   4096

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

    /* Clear all existing entities from the local store. */
    for (uint32_t i = 0; i < ed->entities.capacity; i++) {
        const edit_entity_t *existing = edit_entity_store_get(&ed->entities, i);
        if (existing && existing->active) {
            edit_entity_store_remove(&ed->entities, i);
        }
    }

    /* Preserve the current selection — entity IDs are stable across
     * list refreshes because the server assigns persistent IDs. */

    /* Restore each entity from the server's list. */
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

        /* Parse scale (overwrite the 1,1,1 defaults if present). */
        const json_value_t *scale_val = json_object_get(item, "scale");
        parse_vec3(scale_val, snapshot.scale);

        /* Restore the entity at the server-assigned ID slot. */
        edit_entity_store_restore(&ed->entities, eid, &snapshot);
    }
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
static void dispatch_tui_command(scene_editor_t *ed)
{
    const char *cmd = ed->ui.tui_cmd;
    char buf[2048];

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

    /* Stack buffer for extracting individual response lines. */
    char resp_buf[RESPONSE_BUF_SIZE];
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
        if (resp.ok) {
            scene_sync_mark_acked(&ed->sync);
            scene_sync_update_state(&ed->sync);
        } else {
            /* Log server error to TUI in red. */
            char err_line[UI_TUI_LOG_LINE];
            snprintf(err_line, sizeof(err_line), "Error: %s", resp.error);
            scene_ui_tui_log_error(&ed->ui, err_line);
        }

        /* Try to parse the full JSON to check if the result is an array
         * (entity list response). We use a separate arena for this. */
        uint8_t arena_buf[JSON_ARENA_SIZE];
        json_arena_t arena;
        json_arena_init(&arena, arena_buf, sizeof(arena_buf));

        json_value_t root;
        if (json_parse(resp_buf, (size_t)resp_len, &arena, &root)) {
            const json_value_t *result_val = json_object_get(&root, "result");
            if (result_val && result_val->type == JSON_ARRAY) {
                /* This is an entity list response — rebuild local store. */
                process_entity_list(ed, result_val);
                continue;
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
}

void scene_frame_dispatch_action(struct scene_editor *ed)
{
    if (!ed || ed->ui.action == UI_ACTION_NONE) {
        return;
    }

    char cmd_buf[1024];
    int cmd_len = 0;
    uint32_t cmd_id = 0;
    bool is_entity_modify = false;

    switch (ed->ui.action) {
    case UI_ACTION_SPAWN_BOX:
    case UI_ACTION_SPAWN_SPHERE:
    case UI_ACTION_SPAWN_CAPSULE: {
        /* Determine entity type from the action. */
        uint32_t entity_type = EDIT_ENTITY_TYPE_BOX;
        const char *type_prefix = "Box";
        if (ed->ui.action == UI_ACTION_SPAWN_SPHERE) {
            entity_type = EDIT_ENTITY_TYPE_SPHERE;
            type_prefix = "Sphere";
        } else if (ed->ui.action == UI_ACTION_SPAWN_CAPSULE) {
            entity_type = EDIT_ENTITY_TYPE_CAPSULE;
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
        break;
    }

    case UI_ACTION_SELECT_ENTITY: {
        cmd_id = scene_connection_next_id(&ed->connection);
        cmd_len = scene_cmd_format_select(
            cmd_buf, sizeof(cmd_buf), cmd_id, ed->ui.action_target);

        /* Optimistically update local selection. */
        edit_selection_add(&ed->selection, ed->ui.action_target);
        break;
    }

    case UI_ACTION_DESELECT_ENTITY: {
        cmd_id = scene_connection_next_id(&ed->connection);
        cmd_len = scene_cmd_format_deselect(
            cmd_buf, sizeof(cmd_buf), cmd_id, ed->ui.action_target);

        /* Optimistically update local selection. */
        edit_selection_remove(&ed->selection, ed->ui.action_target);
        break;
    }

    case UI_ACTION_DELETE_SELECTED: {
        cmd_id = scene_connection_next_id(&ed->connection);
        cmd_len = scene_cmd_format_delete(cmd_buf, sizeof(cmd_buf), cmd_id);
        is_entity_modify = true;
        break;
    }

    case UI_ACTION_MODE_TRANSLATE:
        ed->ui.transform_mode = UI_MODE_TRANSLATE;
        break;

    case UI_ACTION_MODE_ROTATE:
        ed->ui.transform_mode = UI_MODE_ROTATE;
        break;

    case UI_ACTION_MODE_SCALE:
        ed->ui.transform_mode = UI_MODE_SCALE;
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
