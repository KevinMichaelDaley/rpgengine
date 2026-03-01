/**
 * @file edit_dispatch_exec.c
 * @brief Command execution — parse JSON, dispatch, build response.
 */

#include "ferrum/editor/edit_dispatch.h"
#include "ferrum/editor/edit_history.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief Write an error response JSON into the buffer.
 * @return Number of bytes written.
 */
static uint32_t write_error_(char *buf, uint32_t cap, double req_id,
                              const char *error_code) {
    int n = snprintf(buf, cap,
                     "{\"id\":%.0f,\"ok\":false,\"error\":\"%s\"}\n",
                     req_id, error_code);
    if (n < 0 || (uint32_t)n >= cap) return 0;
    return (uint32_t)n;
}

/**
 * @brief Write a success response with a result value.
 * @return Number of bytes written.
 */
static uint32_t write_success_(char *buf, uint32_t cap, double req_id,
                                const json_value_t *result) {
    /* Build response: {"id":N,"ok":true,"result":...}\n */
    int offset = snprintf(buf, cap, "{\"id\":%.0f,\"ok\":true,\"result\":",
                          req_id);
    if (offset < 0 || (uint32_t)offset >= cap) return 0;

    size_t result_len = json_write(result, buf + offset, cap - (uint32_t)offset);
    if (result_len == 0) {
        /* Fallback: null result. */
        result_len = (size_t)snprintf(buf + offset,
                                       cap - (uint32_t)offset, "null");
    }
    offset += (int)result_len;
    if ((uint32_t)offset + 2 >= cap) return 0;
    buf[offset++] = '}';
    buf[offset++] = '\n';
    buf[offset]   = '\0';
    return (uint32_t)offset;
}

uint32_t edit_dispatch_exec(edit_dispatch_t *dispatch,
                            const char *json, uint32_t json_len,
                            char *resp_buf, uint32_t resp_cap) {
    if (!dispatch || !json || json_len == 0 || !resp_buf || resp_cap == 0)
        return 0;

    /* Parse the JSON command. */
    json_arena_t parse_arena;
    json_arena_init(&parse_arena, dispatch->parse_arena_buf,
                    dispatch->parse_arena_cap);

    json_value_t root;
    if (!json_parse(json, json_len, &parse_arena, &root)) {
        return write_error_(resp_buf, resp_cap, 0, "parse_error");
    }

    /* Extract "id" field. */
    double req_id = 0;
    const json_value_t *id_val = json_object_get(&root, "id");
    if (id_val && id_val->type == JSON_NUMBER) {
        req_id = id_val->number;
    }

    /* Extract "cmd" field. */
    const json_value_t *cmd_val = json_object_get(&root, "cmd");
    if (!cmd_val || cmd_val->type != JSON_STRING) {
        return write_error_(resp_buf, resp_cap, req_id, "missing_cmd");
    }

    /* Look up handler. */
    edit_cmd_handler_fn handler = edit_dispatch_lookup(
        dispatch, cmd_val->string.ptr, cmd_val->string.len);
    if (!handler) {
        return write_error_(resp_buf, resp_cap, req_id, "unknown_command");
    }

    /* Extract optional "args" field. */
    const json_value_t *args = json_object_get(&root, "args");

    /* Execute handler. */
    json_arena_t resp_arena;
    json_arena_init(&resp_arena, dispatch->resp_arena_buf,
                    dispatch->resp_arena_cap);

    json_value_t result = {.type = JSON_NULL};
    bool ok = handler(dispatch, args, &result, &resp_arena);

    /* ── Record to history (before building response) ─────────── */
    if (dispatch->history) {
        /* NUL-terminate command name. */
        char cmd_name[EDIT_DISPATCH_MAX_CMD_NAME];
        uint32_t clen = cmd_val->string.len;
        if (clen >= sizeof(cmd_name)) clen = sizeof(cmd_name) - 1;
        memcpy(cmd_name, cmd_val->string.ptr, clen);
        cmd_name[clen] = '\0';

        /* Serialize args to JSON string. */
        char args_str[EDIT_HISTORY_ARGS_MAX];
        if (args) {
            size_t alen = json_write(args, args_str, sizeof(args_str));
            if (alen == 0) args_str[0] = '\0';
        } else {
            args_str[0] = '\0';
        }

        /* Serialize result to JSON string. */
        char result_str[EDIT_HISTORY_RESULT_MAX];
        if (ok) {
            size_t rlen = json_write(&result, result_str, sizeof(result_str));
            if (rlen == 0) result_str[0] = '\0';
        } else {
            result_str[0] = '\0';
        }

        const edit_cmd_ctx_t *ctx =
            (const edit_cmd_ctx_t *)dispatch->user_data;
        edit_history_record(dispatch->history, ctx,
                            cmd_name, args_str, result_str, ok);
    }

    if (!ok) {
        return write_error_(resp_buf, resp_cap, req_id, "handler_failed");
    }

    return write_success_(resp_buf, resp_cap, req_id, &result);
}
