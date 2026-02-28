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

/**
 * @brief Check if a string looks like a number (int or float).
 */
static bool looks_numeric_(const char *s) {
    if (!s || !*s) return false;
    if (*s == '-' || *s == '+') s++;
    if (!*s) return false;
    bool has_digit = false;
    while (*s >= '0' && *s <= '9') { s++; has_digit = true; }
    if (*s == '.') { s++; while (*s >= '0' && *s <= '9') { s++; has_digit = true; } }
    return has_digit && *s == '\0';
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
        return 0;
    }
    if (token_count >= 2 && strcmp(tokens[1], "?") == 0) {
        return 0;
    }

    /* Look up command definition. */
    const ctrl_cmd_def_t *def = ctrl_cmd_defs_find(cmd_name);

    /* Use the canonical command name for the JSON wire format. */
    const char *wire_name = def ? def->name : cmd_name;

    /* Special handling for spawn: detect optional name between type and pos.
     * "spawn box 0 5 0"          → type=box, pos=[0,5,0]
     * "spawn box myname 0 5 0"   → type=box, name=myname, pos=[0,5,0] */
    char *name_token = NULL;
    if (def && strcmp(wire_name, "spawn") == 0 && token_count >= 3) {
        /* tokens[1]=type, check if tokens[2] is NOT a number → it's a name. */
        if (!looks_numeric_(tokens[2])) {
            name_token = tokens[2];
            /* Shift tokens to remove the name from the arg stream. */
            for (uint32_t i = 2; i + 1 < token_count; i++) {
                tokens[i] = tokens[i + 1];
            }
            token_count--;
        }
    }

    /* Special handling for select_near / deselect_near: variable arg count.
     * "select_near 5.0"          → {"dist":5.0}
     * "select_near 1 2 3 5.0"    → {"pos":[1,2,3],"dist":5.0} */
    if (def && (strcmp(wire_name, "select_near") == 0 ||
                strcmp(wire_name, "deselect_near") == 0) &&
        token_count >= 2) {
        char args_buf2[512];
        if (token_count == 2) {
            /* Just distance — omit pos so server uses @cursor. */
            float d = strtof(tokens[1], NULL);
            snprintf(args_buf2, sizeof(args_buf2), "{\"dist\":%.6g}", (double)d);
        } else if (token_count >= 5) {
            /* pos + dist */
            float px = strtof(tokens[1], NULL);
            float py = strtof(tokens[2], NULL);
            float pz = strtof(tokens[3], NULL);
            float d  = strtof(tokens[4], NULL);
            snprintf(args_buf2, sizeof(args_buf2),
                     "{\"pos\":[%.6g,%.6g,%.6g],\"dist\":%.6g}",
                     (double)px, (double)py, (double)pz, (double)d);
        } else {
            return 0;  /* Bad arg count. */
        }
        int n2 = snprintf(out, out_cap,
                           "{\"id\":%u,\"cmd\":\"%s\",\"args\":%s}\n",
                           cmd_id, wire_name, args_buf2);
        if (n2 < 0 || (uint32_t)n2 >= out_cap) return 0;
        return (uint32_t)n2;
    }

    /* Build args JSON. */
    char args_buf[2048];
    uint32_t args_len;
    if (def) {
        args_len = build_args_(def->arg_fmt, tokens, token_count, 1,
                               args_buf, sizeof(args_buf));
    } else {
        args_len = (uint32_t)snprintf(args_buf, sizeof(args_buf), "{}");
    }

    if (args_len == 0) {
        snprintf(args_buf, sizeof(args_buf), "{}");
    }

    /* Inject "name" field into spawn args if present. */
    if (name_token && args_len > 1) {
        /* args_buf looks like {"type":"box","pos":[...]}
         * Insert ,"name":"<name>" before the closing }. */
        char injected[2048];
        args_buf[args_len - 1] = '\0'; /* Remove closing } */
        int ni = snprintf(injected, sizeof(injected),
                          "%s,\"name\":\"%s\"}", args_buf, name_token);
        if (ni > 0 && (uint32_t)ni < sizeof(injected)) {
            memcpy(args_buf, injected, (size_t)ni + 1);
            args_len = (uint32_t)ni;
        }
    }

    /* Build final JSON — use canonical name, not alias. */
    int n = snprintf(out, out_cap,
                     "{\"id\":%u,\"cmd\":\"%s\",\"args\":%s}\n",
                     cmd_id, wire_name, args_buf);
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
        } else if (defs[i].alias &&
                   strncmp(defs[i].alias, prefix, prefix_len) == 0) {
            matches[count++] = defs[i].alias;
        }
    }
    return count;
}
