/**
 * @file client_state_dispatch.c
 * @brief Dispatch incoming controller messages to client state handlers.
 *
 * Parses JSON lines and routes queries/commands to cursor, camera, etc.
 *
 * Non-static functions: init, drain (2).
 */

#include "ferrum/editor/client/client_state_dispatch.h"
#include "ferrum/editor/client/client_state_socket.h"
#include "ferrum/editor/client/client_cursor.h"
#include "ferrum/editor/json_parse.h"

#include <stdio.h>
#include <string.h>

void client_state_dispatch_init(client_state_dispatch_t *disp,
                                 client_state_socket_t *socket,
                                 editor_cursor_t *cursor) {
    if (!disp) return;
    disp->socket = socket;
    disp->cursor = cursor;
}

/* ── Query handlers ───────────────────────────────────────────────── */

/**
 * @brief Handle a "cursor" query — respond with cursor state.
 */
static void handle_query_cursor_(client_state_dispatch_t *disp) {
    if (!disp->cursor || !disp->socket) return;

    char buf[256];
    int n = snprintf(buf, sizeof(buf),
        "{\"cursor\":[%.6g,%.6g,%.6g],"
        "\"grid_size\":%.6g,\"snap\":%s}",
        (double)disp->cursor->position.x,
        (double)disp->cursor->position.y,
        (double)disp->cursor->position.z,
        (double)disp->cursor->grid_size,
        disp->cursor->snap_enabled ? "true" : "false");
    if (n > 0) {
        client_state_socket_send(disp->socket, buf, (uint32_t)n);
    }
}

/* ── Command handlers ─────────────────────────────────────────────── */

/**
 * @brief Handle a "set_cursor" command — update cursor position.
 */
static void handle_cmd_set_cursor_(client_state_dispatch_t *disp,
                                    const json_value_t *root) {
    if (!disp->cursor) return;

    const json_value_t *pos_val = json_object_get(root, "pos");
    if (!pos_val || pos_val->type != JSON_ARRAY ||
        pos_val->array.count < 3) return;

    float x = (float)pos_val->array.items[0].number;
    float y = (float)pos_val->array.items[1].number;
    float z = (float)pos_val->array.items[2].number;
    editor_cursor_set_position(disp->cursor, (vec3_t){x, y, z});
}

/**
 * @brief Handle a "move_cursor" command — move cursor by delta.
 */
static void handle_cmd_move_cursor_(client_state_dispatch_t *disp,
                                     const json_value_t *root) {
    if (!disp->cursor) return;

    const json_value_t *delta_val = json_object_get(root, "delta");
    if (!delta_val || delta_val->type != JSON_ARRAY ||
        delta_val->array.count < 3) return;

    float dx = (float)delta_val->array.items[0].number;
    float dy = (float)delta_val->array.items[1].number;
    float dz = (float)delta_val->array.items[2].number;
    editor_cursor_move(disp->cursor, (vec3_t){dx, dy, dz});
}

/**
 * @brief Handle a "cursor_visible" command — toggle cursor visibility.
 */
static void handle_cmd_cursor_visible_(client_state_dispatch_t *disp) {
    if (!disp->cursor) return;
    editor_cursor_toggle_visible(disp->cursor);
}

/* ── Dispatch a single line ───────────────────────────────────────── */

/**
 * @brief Parse and dispatch a single JSON line.
 */
static void dispatch_line_(client_state_dispatch_t *disp,
                            const char *line, uint32_t len) {
    uint8_t arena_buf[2048];
    json_arena_t arena;
    json_arena_init(&arena, arena_buf, sizeof(arena_buf));

    json_value_t root;
    if (!json_parse(line, len, &arena, &root)) return;
    if (root.type != JSON_OBJECT) return;

    /* Check for query. */
    const json_value_t *query = json_object_get(&root, "query");
    if (query && query->type == JSON_STRING) {
        char qname[64];
        json_string_copy(query, qname, sizeof(qname));

        if (strcmp(qname, "cursor") == 0) {
            handle_query_cursor_(disp);
        }
        /* Future: "camera", "selection", etc. */
        return;
    }

    /* Check for command. */
    const json_value_t *cmd = json_object_get(&root, "cmd");
    if (cmd && cmd->type == JSON_STRING) {
        char cname[64];
        json_string_copy(cmd, cname, sizeof(cname));

        if (strcmp(cname, "set_cursor") == 0) {
            handle_cmd_set_cursor_(disp, &root);
        } else if (strcmp(cname, "move_cursor") == 0) {
            handle_cmd_move_cursor_(disp, &root);
        } else if (strcmp(cname, "cursor_visible") == 0) {
            handle_cmd_cursor_visible_(disp);
        }
        /* Future: "set_camera", "grab_begin", "grab_end", etc. */
        return;
    }
}

uint32_t client_state_dispatch_drain(client_state_dispatch_t *disp) {
    if (!disp || !disp->socket) return 0;

    uint32_t count = 0;
    char line[4096];
    uint32_t len;
    while ((len = client_state_socket_pop_line(disp->socket,
                                                line, sizeof(line))) > 0) {
        dispatch_line_(disp, line, len);
        count++;
    }
    return count;
}
