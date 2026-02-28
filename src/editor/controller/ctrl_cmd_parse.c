/**
 * @file ctrl_cmd_parse.c
 * @brief TUI command text → JSON conversion and tab completion.
 *
 * Converts user-typed text like "spawn box 0 5 0" into proper JSON:
 *   {"id":1,"cmd":"spawn","args":{"type":"box","pos":[0,5,0]}}
 *
 * Non-static functions: ctrl_cmd_build_json, ctrl_cmd_complete (2).
 */

#include "ferrum/editor/ctrl_cmd_defs.h"
#include "ferrum/editor/ctrl_tui.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Token splitting ──────────────────────────────────────────────── */

#define MAX_TOKENS 32

/** @brief Split text into whitespace-separated tokens (in-place). */
static uint32_t tokenize_(char *text, char *tokens[], uint32_t max) {
    uint32_t count = 0;
    char *p = text;
    while (*p && count < max) {
        /* Skip whitespace. */
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        tokens[count++] = p;
        /* Skip non-whitespace. */
        while (*p && !isspace((unsigned char)*p)) p++;
        if (*p) *p++ = '\0';
    }
    return count;
}

/* ── Argument format parser ───────────────────────────────────────── */

/**
 * @brief Append JSON args based on the argument format string.
 *
 * Walks the format descriptors and consumes tokens from the user input.
 * Writes JSON key-value pairs into the output buffer.
 *
 * @return Number of bytes written, or 0 on error.
 */
static uint32_t build_args_(const char *arg_fmt, char *tokens[],
                            uint32_t token_count, uint32_t token_start,
                            char *out, uint32_t out_cap) {
    if (!arg_fmt) {
        /* No arguments expected. */
        return (uint32_t)snprintf(out, out_cap, "{}");
    }

    /* Parse format descriptors. */
    char fmt_copy[256];
    strncpy(fmt_copy, arg_fmt, sizeof(fmt_copy) - 1);
    fmt_copy[sizeof(fmt_copy) - 1] = '\0';

    char *fmt_tokens[MAX_TOKENS];
    uint32_t fmt_count = tokenize_(fmt_copy, fmt_tokens, MAX_TOKENS);

    uint32_t written = 0;
    uint32_t ti = token_start; /* Current token index in user input. */

    written += (uint32_t)snprintf(out + written, out_cap - written, "{");

    for (uint32_t fi = 0; fi < fmt_count; fi++) {
        char *spec = fmt_tokens[fi];
        /* Parse "type:name" */
        char *colon = strchr(spec, ':');
        if (!colon) continue;
        *colon = '\0';
        const char *type = spec;
        const char *name = colon + 1;

        if (fi > 0) {
            written += (uint32_t)snprintf(out + written,
                                          out_cap - written, ",");
        }

        if (strcmp(type, "s") == 0) {
            /* String argument. */
            const char *val = (ti < token_count) ? tokens[ti++] : "";
            written += (uint32_t)snprintf(out + written, out_cap - written,
                                          "\"%s\":\"%s\"", name, val);
        } else if (strcmp(type, "u") == 0) {
            /* Unsigned integer argument. */
            uint32_t val = (ti < token_count)
                               ? (uint32_t)strtoul(tokens[ti++], NULL, 10)
                               : 0;
            written += (uint32_t)snprintf(out + written, out_cap - written,
                                          "\"%s\":%u", name, val);
        } else if (strcmp(type, "f") == 0) {
            /* Single float argument. */
            float val = (ti < token_count)
                            ? strtof(tokens[ti++], NULL)
                            : 0.0f;
            written += (uint32_t)snprintf(out + written, out_cap - written,
                                          "\"%s\":%.6g", name, (double)val);
        } else if (strcmp(type, "f3") == 0) {
            /* Three-float array argument. */
            float x = (ti < token_count) ? strtof(tokens[ti++], NULL) : 0.0f;
            float y = (ti < token_count) ? strtof(tokens[ti++], NULL) : 0.0f;
            float z = (ti < token_count) ? strtof(tokens[ti++], NULL) : 0.0f;
            written += (uint32_t)snprintf(out + written, out_cap - written,
                                          "\"%s\":[%.6g,%.6g,%.6g]",
                                          name, (double)x, (double)y,
                                          (double)z);
        } else if (strcmp(type, "b") == 0) {
            /* Boolean argument. */
            const char *val = (ti < token_count) ? tokens[ti++] : "false";
            bool b = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0);
            written += (uint32_t)snprintf(out + written, out_cap - written,
                                          "\"%s\":%s", name,
                                          b ? "true" : "false");
        }
    }

    written += (uint32_t)snprintf(out + written, out_cap - written, "}");
    return written;
}

/* ── Public API ───────────────────────────────────────────────────── */

uint32_t ctrl_cmd_build_json(const char *input, char *out, uint32_t out_cap,
                             uint32_t cmd_id) {
    if (!input || !out || out_cap < 32) return 0;

    /* Make a mutable copy for tokenization. */
    char buf[CTRL_CMD_MAX_LEN];
    strncpy(buf, input, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *tokens[MAX_TOKENS];
    uint32_t token_count = tokenize_(buf, tokens, MAX_TOKENS);
    if (token_count == 0) return 0;

    const char *cmd_name = tokens[0];

    /* Check for help query: "?command" or "command ?" */
    if (cmd_name[0] == '?') {
        /* Return empty — caller should display help. */
        return 0;
    }
    if (token_count >= 2 && strcmp(tokens[1], "?") == 0) {
        return 0;
    }

    /* Look up command definition. */
    const ctrl_cmd_def_t *def = ctrl_cmd_defs_find(cmd_name);

    /* Build args JSON. */
    char args_buf[2048];
    uint32_t args_len;
    if (def) {
        args_len = build_args_(def->arg_fmt, tokens, token_count, 1,
                               args_buf, sizeof(args_buf));
    } else {
        /* Unknown command — send with empty args. */
        args_len = (uint32_t)snprintf(args_buf, sizeof(args_buf), "{}");
    }

    if (args_len == 0) {
        snprintf(args_buf, sizeof(args_buf), "{}");
    }

    /* Build final JSON. */
    int n = snprintf(out, out_cap,
                     "{\"id\":%u,\"cmd\":\"%s\",\"args\":%s}\n",
                     cmd_id, cmd_name, args_buf);
    if (n < 0 || (uint32_t)n >= out_cap) return 0;
    return (uint32_t)n;
}

uint32_t ctrl_cmd_complete(const char *prefix, const char **matches,
                           uint32_t max_matches) {
    if (!prefix || !matches || max_matches == 0) return 0;

    uint32_t count = 0;
    size_t prefix_len = strlen(prefix);

    uint32_t def_count = 0;
    const ctrl_cmd_def_t *defs = ctrl_cmd_defs_table(&def_count);

    for (uint32_t i = 0; i < def_count && count < max_matches; i++) {
        if (strncmp(defs[i].name, prefix, prefix_len) == 0) {
            matches[count++] = defs[i].name;
        }
    }
    return count;
}
