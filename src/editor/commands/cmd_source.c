/**
 * @file cmd_source.c
 * @brief Editor command: source — execute a file of text commands.
 *
 * Reads a file line-by-line, converts each non-empty/non-comment line
 * to JSON via ctrl_cmd_build_json, and dispatches it through
 * edit_dispatch_exec.  Supports entity_def blocks: lines between
 * "entity_def <name>" and "end" are gathered and dispatched as a
 * single entity_def command.
 *
 * JSON args: {"file": "path/to/script.cmd"}
 * Returns:   number of commands executed successfully.
 *
 * Non-static functions: cmd_source (1).
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_dispatch.h"
#include "ferrum/editor/ctrl_cmd_defs.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

/** @brief Check if a line starts an entity_def block. */
static bool is_entity_def_start_(const char *line) {
    return (strncmp(line, "entity_def ", 11) == 0 ||
            strncmp(line, "edef ", 5) == 0);
}

bool cmd_source(edit_dispatch_t *d, const json_value_t *args,
                json_value_t *result, json_arena_t *arena) {
    (void)arena;
    if (!d || !args) return false;

    /* Extract file path. */
    const json_value_t *file_val = json_object_get(args, "file");
    if (!file_val || file_val->type != JSON_STRING) return false;

    char path[512];
    uint32_t plen = file_val->string.len;
    if (plen >= sizeof(path)) plen = sizeof(path) - 1;
    memcpy(path, file_val->string.ptr, plen);
    path[plen] = '\0';

    /* Open the command script file. */
    FILE *fp = fopen(path, "r");
    if (!fp) return false;

    uint32_t executed = 0;
    uint32_t cmd_id = 1;
    char line[1024];

    /* Block-gathering state for entity_def. */
    bool in_block = false;
    char block_header[512];
    char block_lines[64][512];
    const char *block_ptrs[64];
    uint32_t block_count = 0;

    while (fgets(line, (int)sizeof(line), fp)) {
        /* Strip trailing newline/whitespace. */
        size_t len = strlen(line);
        while (len > 0 && isspace((unsigned char)line[len - 1])) {
            line[--len] = '\0';
        }

        /* Skip empty lines and comments. */
        const char *p = line;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == '\0' || *p == '#') continue;

        /* Block gathering: accumulate entity_def body lines. */
        if (in_block) {
            if (strcmp(p, "end") == 0) {
                /* Close block and dispatch. */
                for (uint32_t i = 0; i < block_count; i++) {
                    block_ptrs[i] = block_lines[i];
                }
                char json_buf[8192];
                uint32_t json_len = ctrl_cmd_build_entity_def_json(
                    block_header, block_ptrs, block_count,
                    json_buf, sizeof(json_buf), cmd_id);
                if (json_len > 0) {
                    char resp[4096];
                    uint32_t resp_len = edit_dispatch_exec(
                        d, json_buf, json_len, resp, sizeof(resp));
                    if (resp_len > 0) executed++;
                    cmd_id++;
                }
                in_block = false;
                block_count = 0;
            } else if (block_count < 64) {
                size_t plen2 = strlen(p);
                if (plen2 >= sizeof(block_lines[0]))
                    plen2 = sizeof(block_lines[0]) - 1;
                memcpy(block_lines[block_count], p, plen2);
                block_lines[block_count][plen2] = '\0';
                block_count++;
            }
            continue;
        }

        /* Check for entity_def block start. */
        if (is_entity_def_start_(p)) {
            in_block = true;
            block_count = 0;
            strncpy(block_header, p, sizeof(block_header) - 1);
            block_header[sizeof(block_header) - 1] = '\0';
            continue;
        }

        /* Convert text command to JSON. */
        char json_buf[4096];
        uint32_t json_len = ctrl_cmd_build_json(p, json_buf,
                                                 sizeof(json_buf), cmd_id);
        if (json_len == 0) continue;

        /* Dispatch the command. */
        char resp[4096];
        uint32_t resp_len = edit_dispatch_exec(d, json_buf, json_len,
                                                resp, sizeof(resp));
        if (resp_len > 0) {
            executed++;
        }
        cmd_id++;
    }

    fclose(fp);

    result->type = JSON_NUMBER;
    result->number = (double)executed;
    return true;
}
