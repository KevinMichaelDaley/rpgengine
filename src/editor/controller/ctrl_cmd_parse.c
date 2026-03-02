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

    /* Special handling for spawn: detect optional name between type and pos,
     * and optional rotation/scale after position.
     * "spawn box 0 5 0"                      → type=box, pos=[0,5,0]
     * "spawn box myname 0 5 0"               → type=box, name=myname, pos=[0,5,0]
     * "spawn box 0 5 0 0 90 0"               → + rot=[0,90,0]
     * "spawn box 0 5 0 0 90 0 2 2 2"         → + rot + scale=[2,2,2]
     * "spawn box myname 0 5 0 0 90 0 2 2 2"  → name + pos + rot + scale */
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

        /* After name extraction, remaining numeric tokens after type:
         * tokens[2..4] = pos, tokens[5..7] = rot, tokens[8..10] = scale.
         * Count remaining numeric tokens starting at index 2. */
        uint32_t num_count = 0;
        for (uint32_t i = 2; i < token_count; i++) {
            if (looks_numeric_(tokens[i])) num_count++;
            else break;
        }

        /* Build custom JSON with pos, optional rot, optional scale. */
        if (num_count >= 3) {
            char args_buf2[512];
            float px = strtof(tokens[2], NULL);
            float py = strtof(tokens[3], NULL);
            float pz = strtof(tokens[4], NULL);

            if (num_count >= 9) {
                /* pos + rot + scale. */
                float rx = strtof(tokens[5], NULL);
                float ry = strtof(tokens[6], NULL);
                float rz = strtof(tokens[7], NULL);
                float sx = strtof(tokens[8], NULL);
                float sy = strtof(tokens[9], NULL);
                float sz = strtof(tokens[10], NULL);
                if (name_token) {
                    snprintf(args_buf2, sizeof(args_buf2),
                        "{\"type\":\"%s\",\"name\":\"%s\","
                        "\"pos\":[%.6g,%.6g,%.6g],"
                        "\"rot\":[%.6g,%.6g,%.6g],"
                        "\"scale\":[%.6g,%.6g,%.6g]}",
                        tokens[1], name_token,
                        (double)px, (double)py, (double)pz,
                        (double)rx, (double)ry, (double)rz,
                        (double)sx, (double)sy, (double)sz);
                } else {
                    snprintf(args_buf2, sizeof(args_buf2),
                        "{\"type\":\"%s\","
                        "\"pos\":[%.6g,%.6g,%.6g],"
                        "\"rot\":[%.6g,%.6g,%.6g],"
                        "\"scale\":[%.6g,%.6g,%.6g]}",
                        tokens[1],
                        (double)px, (double)py, (double)pz,
                        (double)rx, (double)ry, (double)rz,
                        (double)sx, (double)sy, (double)sz);
                }
            } else if (num_count >= 6) {
                /* pos + rot. */
                float rx = strtof(tokens[5], NULL);
                float ry = strtof(tokens[6], NULL);
                float rz = strtof(tokens[7], NULL);
                if (name_token) {
                    snprintf(args_buf2, sizeof(args_buf2),
                        "{\"type\":\"%s\",\"name\":\"%s\","
                        "\"pos\":[%.6g,%.6g,%.6g],"
                        "\"rot\":[%.6g,%.6g,%.6g]}",
                        tokens[1], name_token,
                        (double)px, (double)py, (double)pz,
                        (double)rx, (double)ry, (double)rz);
                } else {
                    snprintf(args_buf2, sizeof(args_buf2),
                        "{\"type\":\"%s\","
                        "\"pos\":[%.6g,%.6g,%.6g],"
                        "\"rot\":[%.6g,%.6g,%.6g]}",
                        tokens[1],
                        (double)px, (double)py, (double)pz,
                        (double)rx, (double)ry, (double)rz);
                }
            } else {
                /* pos only. */
                if (name_token) {
                    snprintf(args_buf2, sizeof(args_buf2),
                        "{\"type\":\"%s\",\"name\":\"%s\","
                        "\"pos\":[%.6g,%.6g,%.6g]}",
                        tokens[1], name_token,
                        (double)px, (double)py, (double)pz);
                } else {
                    snprintf(args_buf2, sizeof(args_buf2),
                        "{\"type\":\"%s\","
                        "\"pos\":[%.6g,%.6g,%.6g]}",
                        tokens[1],
                        (double)px, (double)py, (double)pz);
                }
            }
            int n2 = snprintf(out, out_cap,
                               "{\"id\":%u,\"cmd\":\"%s\",\"args\":%s}\n",
                               cmd_id, wire_name, args_buf2);
            if (n2 < 0 || (uint32_t)n2 >= out_cap) return 0;
            return (uint32_t)n2;
        }
    }

    /* Special handling for select_near / deselect_near: variable arg count.
     * "select_near 5.0"                → {"dist":5.0}
     * "select_near 1 2 3 5.0"          → {"pos":[1,2,3],"dist":5.0}
     * "select_near 5.0 &group"         → {"dist":5.0,"group_mask":"&group"}
     * "select_near 1 2 3 5.0 &group"   → all three */
    if (def && (strcmp(wire_name, "select_near") == 0 ||
                strcmp(wire_name, "deselect_near") == 0) &&
        token_count >= 2) {
        /* Check for trailing &group token. */
        const char *mask = NULL;
        uint32_t arg_count = token_count;
        if (arg_count >= 2 && tokens[arg_count - 1][0] == '&') {
            mask = tokens[arg_count - 1];
            arg_count--;
        }

        char args_buf2[512];
        if (arg_count == 2) {
            float d = strtof(tokens[1], NULL);
            if (mask) {
                snprintf(args_buf2, sizeof(args_buf2),
                         "{\"dist\":%.6g,\"group_mask\":\"%s\"}",
                         (double)d, mask);
            } else {
                snprintf(args_buf2, sizeof(args_buf2),
                         "{\"dist\":%.6g}", (double)d);
            }
        } else if (arg_count >= 5) {
            float px = strtof(tokens[1], NULL);
            float py = strtof(tokens[2], NULL);
            float pz = strtof(tokens[3], NULL);
            float d  = strtof(tokens[4], NULL);
            if (mask) {
                snprintf(args_buf2, sizeof(args_buf2),
                         "{\"pos\":[%.6g,%.6g,%.6g],\"dist\":%.6g,"
                         "\"group_mask\":\"%s\"}",
                         (double)px, (double)py, (double)pz, (double)d,
                         mask);
            } else {
                snprintf(args_buf2, sizeof(args_buf2),
                         "{\"pos\":[%.6g,%.6g,%.6g],\"dist\":%.6g}",
                         (double)px, (double)py, (double)pz, (double)d);
            }
        } else {
            return 0;  /* Bad arg count. */
        }
        int n2 = snprintf(out, out_cap,
                           "{\"id\":%u,\"cmd\":\"%s\",\"args\":%s}\n",
                           cmd_id, wire_name, args_buf2);
        if (n2 < 0 || (uint32_t)n2 >= out_cap) return 0;
        return (uint32_t)n2;
    }

    /* Custom parsing for commands with optional group_mask:
     * select_all [&group], select_touching [&group], select_fill [&group],
     * select_regex <pattern> [&group]. */
    if (def && (strcmp(wire_name, "select_all") == 0 ||
                strcmp(wire_name, "select_touching") == 0 ||
                strcmp(wire_name, "select_fill") == 0)) {
        char args_buf2[256];
        if (token_count >= 2 && tokens[1][0] == '&') {
            snprintf(args_buf2, sizeof(args_buf2),
                     "{\"group_mask\":\"%s\"}", tokens[1]);
        } else {
            snprintf(args_buf2, sizeof(args_buf2), "{}");
        }
        int n2 = snprintf(out, out_cap,
                           "{\"id\":%u,\"cmd\":\"%s\",\"args\":%s}\n",
                           cmd_id, wire_name, args_buf2);
        if (n2 < 0 || (uint32_t)n2 >= out_cap) return 0;
        return (uint32_t)n2;
    }

    if (def && strcmp(wire_name, "select_regex") == 0 && token_count >= 2) {
        char args_buf2[512];
        const char *pattern = tokens[1];
        const char *mask = NULL;
        if (token_count >= 3 && tokens[2][0] == '&') {
            mask = tokens[2];
        }
        if (mask) {
            snprintf(args_buf2, sizeof(args_buf2),
                     "{\"pattern\":\"%s\",\"group_mask\":\"%s\"}",
                     pattern, mask);
        } else {
            snprintf(args_buf2, sizeof(args_buf2),
                     "{\"pattern\":\"%s\"}", pattern);
        }
        int n2 = snprintf(out, out_cap,
                           "{\"id\":%u,\"cmd\":\"%s\",\"args\":%s}\n",
                           cmd_id, wire_name, args_buf2);
        if (n2 < 0 || (uint32_t)n2 >= out_cap) return 0;
        return (uint32_t)n2;
    }

    /* Special handling for alias_create: variable arg count.
     * "alias_create @name"              → {"name":"@name"}
     * "alias_create @name x y z"        → {"name":"@name","pos":[x,y,z]}
     * "alias_create @name x y z rx ry rz" → + "rot":[...] */
    if (def && strcmp(wire_name, "alias_create") == 0 && token_count >= 2) {
        char args_buf2[512];
        const char *aname = tokens[1];
        if (token_count == 2) {
            snprintf(args_buf2, sizeof(args_buf2),
                     "{\"name\":\"%s\"}", aname);
        } else if (token_count >= 5 && token_count < 8) {
            float px = strtof(tokens[2], NULL);
            float py = strtof(tokens[3], NULL);
            float pz = strtof(tokens[4], NULL);
            snprintf(args_buf2, sizeof(args_buf2),
                     "{\"name\":\"%s\",\"pos\":[%.6g,%.6g,%.6g]}",
                     aname, (double)px, (double)py, (double)pz);
        } else if (token_count >= 8) {
            float px = strtof(tokens[2], NULL);
            float py = strtof(tokens[3], NULL);
            float pz = strtof(tokens[4], NULL);
            float rx = strtof(tokens[5], NULL);
            float ry = strtof(tokens[6], NULL);
            float rz = strtof(tokens[7], NULL);
            snprintf(args_buf2, sizeof(args_buf2),
                     "{\"name\":\"%s\",\"pos\":[%.6g,%.6g,%.6g],"
                     "\"rot\":[%.6g,%.6g,%.6g]}",
                     aname, (double)px, (double)py, (double)pz,
                     (double)rx, (double)ry, (double)rz);
        } else {
            return 0;
        }
        int n2 = snprintf(out, out_cap,
                           "{\"id\":%u,\"cmd\":\"%s\",\"args\":%s}\n",
                           cmd_id, wire_name, args_buf2);
        if (n2 < 0 || (uint32_t)n2 >= out_cap) return 0;
        return (uint32_t)n2;
    }

    /* Custom parsing for asset_list: optional prefix and type.
     * "asset_list"              → {}
     * "asset_list meshes/"      → {"prefix":"meshes/"}
     * "asset_list meshes/ mesh" → {"prefix":"meshes/","type":"mesh"} */
    if (def && strcmp(wire_name, "asset_list") == 0) {
        char args_buf2[512];
        if (token_count >= 3) {
            snprintf(args_buf2, sizeof(args_buf2),
                     "{\"prefix\":\"%s\",\"type\":\"%s\"}",
                     tokens[1], tokens[2]);
        } else if (token_count == 2) {
            snprintf(args_buf2, sizeof(args_buf2),
                     "{\"prefix\":\"%s\"}", tokens[1]);
        } else {
            snprintf(args_buf2, sizeof(args_buf2), "{}");
        }
        int n2 = snprintf(out, out_cap,
                           "{\"id\":%u,\"cmd\":\"%s\",\"args\":%s}\n",
                           cmd_id, wire_name, args_buf2);
        if (n2 < 0 || (uint32_t)n2 >= out_cap) return 0;
        return (uint32_t)n2;
    }

    /* ── Mesh creation: variable optional args ───────────────────── */

    /* mesh_create_box [w h d] [x y z]
     * 0 args → {}
     * 3 args → {"size":[w,h,d]}
     * 6 args → {"size":[w,h,d],"pos":[x,y,z]} */
    if (def && strcmp(wire_name, "mesh_create_box") == 0) {
        char ab[512];
        uint32_t na = token_count - 1;
        if (na >= 6) {
            snprintf(ab, sizeof(ab),
                "{\"size\":[%.6g,%.6g,%.6g],\"pos\":[%.6g,%.6g,%.6g]}",
                (double)strtof(tokens[1],NULL), (double)strtof(tokens[2],NULL),
                (double)strtof(tokens[3],NULL), (double)strtof(tokens[4],NULL),
                (double)strtof(tokens[5],NULL), (double)strtof(tokens[6],NULL));
        } else if (na >= 3) {
            snprintf(ab, sizeof(ab), "{\"size\":[%.6g,%.6g,%.6g]}",
                (double)strtof(tokens[1],NULL), (double)strtof(tokens[2],NULL),
                (double)strtof(tokens[3],NULL));
        } else {
            snprintf(ab, sizeof(ab), "{}");
        }
        int n2 = snprintf(out, out_cap,
            "{\"id\":%u,\"cmd\":\"%s\",\"args\":%s}\n", cmd_id, wire_name, ab);
        if (n2 < 0 || (uint32_t)n2 >= out_cap) return 0;
        return (uint32_t)n2;
    }

    /* mesh_create_sphere [radius] [segments] [x y z] */
    if (def && strcmp(wire_name, "mesh_create_sphere") == 0) {
        char ab[512];
        uint32_t na = token_count - 1;
        if (na >= 6) {
            /* radius segments rings x y z */
            snprintf(ab, sizeof(ab),
                "{\"radius\":%.6g,\"segments\":%u,\"rings\":%u,"
                "\"pos\":[%.6g,%.6g,%.6g]}",
                (double)strtof(tokens[1],NULL),
                (unsigned)strtoul(tokens[2],NULL,10),
                (unsigned)strtoul(tokens[3],NULL,10),
                (double)strtof(tokens[4],NULL), (double)strtof(tokens[5],NULL),
                (double)strtof(tokens[6],NULL));
        } else if (na >= 5) {
            /* radius segments x y z */
            snprintf(ab, sizeof(ab),
                "{\"radius\":%.6g,\"segments\":%u,\"pos\":[%.6g,%.6g,%.6g]}",
                (double)strtof(tokens[1],NULL),
                (unsigned)strtoul(tokens[2],NULL,10),
                (double)strtof(tokens[3],NULL), (double)strtof(tokens[4],NULL),
                (double)strtof(tokens[5],NULL));
        } else if (na >= 3) {
            /* radius segments rings */
            snprintf(ab, sizeof(ab),
                "{\"radius\":%.6g,\"segments\":%u,\"rings\":%u}",
                (double)strtof(tokens[1],NULL),
                (unsigned)strtoul(tokens[2],NULL,10),
                (unsigned)strtoul(tokens[3],NULL,10));
        } else if (na >= 2) {
            snprintf(ab, sizeof(ab), "{\"radius\":%.6g,\"segments\":%u}",
                (double)strtof(tokens[1],NULL),
                (unsigned)strtoul(tokens[2],NULL,10));
        } else if (na >= 1) {
            snprintf(ab, sizeof(ab), "{\"radius\":%.6g}",
                (double)strtof(tokens[1],NULL));
        } else {
            snprintf(ab, sizeof(ab), "{}");
        }
        int n2 = snprintf(out, out_cap,
            "{\"id\":%u,\"cmd\":\"%s\",\"args\":%s}\n", cmd_id, wire_name, ab);
        if (n2 < 0 || (uint32_t)n2 >= out_cap) return 0;
        return (uint32_t)n2;
    }

    /* mesh_create_cylinder [radius] [height] [segments] [axis] [x y z] */
    if (def && strcmp(wire_name, "mesh_create_cylinder") == 0) {
        char ab[512];
        uint32_t na = token_count - 1;
        if (na >= 7) {
            /* radius height segments axis x y z */
            snprintf(ab, sizeof(ab),
                "{\"radius\":%.6g,\"height\":%.6g,\"segments\":%u,"
                "\"axis\":%u,\"pos\":[%.6g,%.6g,%.6g]}",
                (double)strtof(tokens[1],NULL), (double)strtof(tokens[2],NULL),
                (unsigned)strtoul(tokens[3],NULL,10),
                (unsigned)strtoul(tokens[4],NULL,10),
                (double)strtof(tokens[5],NULL), (double)strtof(tokens[6],NULL),
                (double)strtof(tokens[7],NULL));
        } else if (na >= 6) {
            /* radius height segments x y z  (axis defaults to 1/Y) */
            snprintf(ab, sizeof(ab),
                "{\"radius\":%.6g,\"height\":%.6g,\"segments\":%u,"
                "\"axis\":1,\"pos\":[%.6g,%.6g,%.6g]}",
                (double)strtof(tokens[1],NULL), (double)strtof(tokens[2],NULL),
                (unsigned)strtoul(tokens[3],NULL,10),
                (double)strtof(tokens[4],NULL), (double)strtof(tokens[5],NULL),
                (double)strtof(tokens[6],NULL));
        } else if (na >= 4) {
            snprintf(ab, sizeof(ab),
                "{\"radius\":%.6g,\"height\":%.6g,\"segments\":%u,\"axis\":%u}",
                (double)strtof(tokens[1],NULL), (double)strtof(tokens[2],NULL),
                (unsigned)strtoul(tokens[3],NULL,10),
                (unsigned)strtoul(tokens[4],NULL,10));
        } else if (na >= 2) {
            snprintf(ab, sizeof(ab), "{\"radius\":%.6g,\"height\":%.6g}",
                (double)strtof(tokens[1],NULL), (double)strtof(tokens[2],NULL));
        } else {
            snprintf(ab, sizeof(ab), "{}");
        }
        int n2 = snprintf(out, out_cap,
            "{\"id\":%u,\"cmd\":\"%s\",\"args\":%s}\n", cmd_id, wire_name, ab);
        if (n2 < 0 || (uint32_t)n2 >= out_cap) return 0;
        return (uint32_t)n2;
    }

    /* mesh_create_plane [w h] [axis] [x y z] */
    if (def && strcmp(wire_name, "mesh_create_plane") == 0) {
        char ab[512];
        uint32_t na = token_count - 1;
        if (na >= 6) {
            snprintf(ab, sizeof(ab),
                "{\"size\":[%.6g,%.6g],\"axis\":%u,"
                "\"pos\":[%.6g,%.6g,%.6g]}",
                (double)strtof(tokens[1],NULL), (double)strtof(tokens[2],NULL),
                (unsigned)strtoul(tokens[3],NULL,10),
                (double)strtof(tokens[4],NULL), (double)strtof(tokens[5],NULL),
                (double)strtof(tokens[6],NULL));
        } else if (na >= 3) {
            snprintf(ab, sizeof(ab), "{\"size\":[%.6g,%.6g],\"axis\":%u}",
                (double)strtof(tokens[1],NULL), (double)strtof(tokens[2],NULL),
                (unsigned)strtoul(tokens[3],NULL,10));
        } else if (na >= 2) {
            snprintf(ab, sizeof(ab), "{\"size\":[%.6g,%.6g]}",
                (double)strtof(tokens[1],NULL), (double)strtof(tokens[2],NULL));
        } else {
            snprintf(ab, sizeof(ab), "{}");
        }
        int n2 = snprintf(out, out_cap,
            "{\"id\":%u,\"cmd\":\"%s\",\"args\":%s}\n", cmd_id, wire_name, ab);
        if (n2 < 0 || (uint32_t)n2 >= out_cap) return 0;
        return (uint32_t)n2;
    }

    /* extrude [distance] [dx dy dz] */
    if (def && strcmp(wire_name, "extrude") == 0) {
        char ab[512];
        uint32_t na = token_count - 1;
        if (na >= 4) {
            snprintf(ab, sizeof(ab),
                "{\"distance\":%.6g,\"direction\":[%.6g,%.6g,%.6g]}",
                (double)strtof(tokens[1],NULL), (double)strtof(tokens[2],NULL),
                (double)strtof(tokens[3],NULL), (double)strtof(tokens[4],NULL));
        } else if (na >= 1) {
            snprintf(ab, sizeof(ab), "{\"distance\":%.6g}",
                (double)strtof(tokens[1],NULL));
        } else {
            snprintf(ab, sizeof(ab), "{}");
        }
        int n2 = snprintf(out, out_cap,
            "{\"id\":%u,\"cmd\":\"%s\",\"args\":%s}\n", cmd_id, wire_name, ab);
        if (n2 < 0 || (uint32_t)n2 >= out_cap) return 0;
        return (uint32_t)n2;
    }

    /* inset [amount] [depth] */
    if (def && strcmp(wire_name, "inset") == 0) {
        char ab[256];
        uint32_t na = token_count - 1;
        if (na >= 2) {
            snprintf(ab, sizeof(ab), "{\"amount\":%.6g,\"depth\":%.6g}",
                (double)strtof(tokens[1],NULL), (double)strtof(tokens[2],NULL));
        } else if (na >= 1) {
            snprintf(ab, sizeof(ab), "{\"amount\":%.6g}",
                (double)strtof(tokens[1],NULL));
        } else {
            snprintf(ab, sizeof(ab), "{}");
        }
        int n2 = snprintf(out, out_cap,
            "{\"id\":%u,\"cmd\":\"%s\",\"args\":%s}\n", cmd_id, wire_name, ab);
        if (n2 < 0 || (uint32_t)n2 >= out_cap) return 0;
        return (uint32_t)n2;
    }

    /* mesh_select / mesh_deselect: variable index list.
     * "mesh_select 0 1 2" → {"indices":[0,1,2]} */
    if (def && (strcmp(wire_name, "mesh_select") == 0 ||
                strcmp(wire_name, "mesh_deselect") == 0)) {
        char ab[2048];
        uint32_t w = 0;
        w += (uint32_t)snprintf(ab + w, sizeof(ab) - w, "{\"indices\":[");
        for (uint32_t i = 1; i < token_count; i++) {
            if (i > 1) w += (uint32_t)snprintf(ab + w, sizeof(ab) - w, ",");
            w += (uint32_t)snprintf(ab + w, sizeof(ab) - w, "%u",
                                     (unsigned)strtoul(tokens[i], NULL, 10));
        }
        w += (uint32_t)snprintf(ab + w, sizeof(ab) - w, "]}");
        int n2 = snprintf(out, out_cap,
            "{\"id\":%u,\"cmd\":\"%s\",\"args\":%s}\n", cmd_id, wire_name, ab);
        if (n2 < 0 || (uint32_t)n2 >= out_cap) return 0;
        return (uint32_t)n2;
    }

    /* Special handling for setattr: entity key value.
     * "setattr turret_0 256 1"       → {"entity":"turret_0","key":256,"value":1}
     * "setattr 5 256 true"           → {"entity":5,"key":256,"value":true}
     * "setattr turret_0 256 hello"   → {"entity":"turret_0","key":256,"value":"hello"}
     */
    if (def && strcmp(wire_name, "setattr") == 0 && token_count >= 4) {
        char args_buf2[512];
        const char *ent_tok = tokens[1];
        const char *key_tok = tokens[2];
        const char *val_tok = tokens[3];

        /* Entity: numeric id or string name. */
        char ent_json[128];
        if (looks_numeric_(ent_tok)) {
            snprintf(ent_json, sizeof(ent_json), "%s", ent_tok);
        } else {
            snprintf(ent_json, sizeof(ent_json), "\"%s\"", ent_tok);
        }

        /* Value: bool, number, or string. */
        char val_json[256];
        if (strcmp(val_tok, "true") == 0 || strcmp(val_tok, "false") == 0) {
            snprintf(val_json, sizeof(val_json), "%s", val_tok);
        } else if (looks_numeric_(val_tok)) {
            snprintf(val_json, sizeof(val_json), "%s", val_tok);
        } else {
            snprintf(val_json, sizeof(val_json), "\"%s\"", val_tok);
        }

        snprintf(args_buf2, sizeof(args_buf2),
                 "{\"entity\":%s,\"key\":%s,\"value\":%s}",
                 ent_json, key_tok, val_json);

        int n2 = snprintf(out, out_cap,
                           "{\"id\":%u,\"cmd\":\"%s\",\"args\":%s}\n",
                           cmd_id, wire_name, args_buf2);
        if (n2 < 0 || (uint32_t)n2 >= out_cap) return 0;
        return (uint32_t)n2;
    }

    /* Special handling for joint: type entity_a entity_b ax ay az [axis_x axis_y axis_z].
     * "joint hinge base_0 barrel_0 0 1.5 0 0 1 0"
     *   → {"joint_type":"hinge","entity_a":"base_0","entity_b":"barrel_0",
     *      "anchor":[0,1.5,0],"axis":[0,1,0]}
     */
    if (def && strcmp(wire_name, "joint") == 0 && token_count >= 7) {
        char jbuf[512];
        const char *jtype = tokens[1];
        const char *ent_a = tokens[2];
        const char *ent_b = tokens[3];

        /* Entity refs: numeric id or string name. */
        char ea_json[128], eb_json[128];
        if (looks_numeric_(ent_a)) {
            snprintf(ea_json, sizeof(ea_json), "%s", ent_a);
        } else {
            snprintf(ea_json, sizeof(ea_json), "\"%s\"", ent_a);
        }
        if (looks_numeric_(ent_b)) {
            snprintf(eb_json, sizeof(eb_json), "%s", ent_b);
        } else {
            snprintf(eb_json, sizeof(eb_json), "\"%s\"", ent_b);
        }

        /* Anchor: tokens 4,5,6. */
        const char *ax = tokens[4];
        const char *ay = tokens[5];
        const char *az = tokens[6];

        /* Optional axis: tokens 7,8,9 (default 0,1,0). */
        const char *axx = (token_count >= 10) ? tokens[7] : "0";
        const char *axy = (token_count >= 10) ? tokens[8] : "1";
        const char *axz = (token_count >= 10) ? tokens[9] : "0";

        snprintf(jbuf, sizeof(jbuf),
                 "{\"joint_type\":\"%s\",\"entity_a\":%s,\"entity_b\":%s,"
                 "\"anchor\":[%s,%s,%s],\"axis\":[%s,%s,%s]}",
                 jtype, ea_json, eb_json, ax, ay, az, axx, axy, axz);

        int n3 = snprintf(out, out_cap,
                           "{\"id\":%u,\"cmd\":\"%s\",\"args\":%s}\n",
                           cmd_id, wire_name, jbuf);
        if (n3 < 0 || (uint32_t)n3 >= out_cap) return 0;
        return (uint32_t)n3;
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

/* ── entity_def block → JSON conversion ──────────────────────────── */

uint32_t ctrl_cmd_build_entity_def_json(const char *header,
                                        const char **lines, uint32_t nlines,
                                        char *out, uint32_t out_cap,
                                        uint32_t cmd_id) {
    if (!header || !out || out_cap < 64) return 0;

    /* Parse name from header: "entity_def <name>" or "edef <name>". */
    const char *p = header;
    while (*p && !isspace((unsigned char)*p)) p++;  /* skip command word */
    while (*p && isspace((unsigned char)*p)) p++;    /* skip whitespace */
    const char *name = p;
    if (!*name) return 0;

    /* Build the args JSON incrementally. */
    char args[4096];
    uint32_t w = 0;
    w += (uint32_t)snprintf(args + w, sizeof(args) - w,
                            "{\"name\":\"%s\"", name);

    /* Collect attrs into a separate buffer to append at the end. */
    char attrs_buf[2048];
    uint32_t aw = 0;
    uint32_t attr_count = 0;
    aw += (uint32_t)snprintf(attrs_buf + aw, sizeof(attrs_buf) - aw, "[");

    for (uint32_t i = 0; i < nlines; i++) {
        /* Tokenize each body line. */
        const char *ln = lines[i];
        while (*ln && isspace((unsigned char)*ln)) ln++;
        if (*ln == '\0' || *ln == '#') continue;

        /* Make a mutable copy for tokenization. */
        char lbuf[512];
        strncpy(lbuf, ln, sizeof(lbuf) - 1);
        lbuf[sizeof(lbuf) - 1] = '\0';

        char *toks[MAX_TOKENS];
        uint32_t ntoks = tokenize_(lbuf, toks, MAX_TOKENS);
        if (ntoks == 0) continue;

        if (strcmp(toks[0], "type") == 0 && ntoks >= 2) {
            w += (uint32_t)snprintf(args + w, sizeof(args) - w,
                                    ",\"type\":\"%s\"", toks[1]);
        } else if (strcmp(toks[0], "pos") == 0 && ntoks >= 4) {
            w += (uint32_t)snprintf(args + w, sizeof(args) - w,
                                    ",\"pos\":[%s,%s,%s]",
                                    toks[1], toks[2], toks[3]);
        } else if (strcmp(toks[0], "rot") == 0 && ntoks >= 4) {
            w += (uint32_t)snprintf(args + w, sizeof(args) - w,
                                    ",\"rot\":[%s,%s,%s]",
                                    toks[1], toks[2], toks[3]);
        } else if (strcmp(toks[0], "scale") == 0 && ntoks >= 4) {
            w += (uint32_t)snprintf(args + w, sizeof(args) - w,
                                    ",\"scale\":[%s,%s,%s]",
                                    toks[1], toks[2], toks[3]);
        } else if (strcmp(toks[0], "setattr") == 0 && ntoks >= 3) {
            const char *key_tok = toks[1];
            const char *val_tok = toks[2];

            char val_json[256];
            if (strcmp(val_tok, "true") == 0 ||
                strcmp(val_tok, "false") == 0) {
                snprintf(val_json, sizeof(val_json), "%s", val_tok);
            } else if (looks_numeric_(val_tok)) {
                snprintf(val_json, sizeof(val_json), "%s", val_tok);
            } else {
                snprintf(val_json, sizeof(val_json), "\"%s\"", val_tok);
            }

            if (attr_count > 0) {
                aw += (uint32_t)snprintf(attrs_buf + aw,
                                         sizeof(attrs_buf) - aw, ",");
            }
            aw += (uint32_t)snprintf(attrs_buf + aw, sizeof(attrs_buf) - aw,
                                     "{\"key\":%s,\"value\":%s}",
                                     key_tok, val_json);
            attr_count++;
        } else if (strcmp(toks[0], "mass") == 0 && ntoks >= 2) {
            /* Sugar: "mass 5.0" → setattr SCRIPT_KEY_MASS(7) <float>. */
            if (attr_count > 0) {
                aw += (uint32_t)snprintf(attrs_buf + aw,
                                         sizeof(attrs_buf) - aw, ",");
            }
            /* Ensure mass is emitted as a float (must have decimal point)
             * so that cmd_setattr stores it as SCRIPT_ATTR_F32. */
            const char *dot = strchr(toks[1], '.');
            aw += (uint32_t)snprintf(attrs_buf + aw, sizeof(attrs_buf) - aw,
                                     "{\"key\":7,\"value\":%s%s}",
                                     toks[1], dot ? "" : ".0");
            attr_count++;
        } else if (strcmp(toks[0], "static") == 0) {
            /* Sugar: "static" → setattr SCRIPT_KEY_STATIC(8) true. */
            if (attr_count > 0) {
                aw += (uint32_t)snprintf(attrs_buf + aw,
                                         sizeof(attrs_buf) - aw, ",");
            }
            aw += (uint32_t)snprintf(attrs_buf + aw, sizeof(attrs_buf) - aw,
                                     "{\"key\":8,\"value\":true}");
            attr_count++;
        } else if (strcmp(toks[0], "kinematic") == 0) {
            /* Sugar: "kinematic" → setattr SCRIPT_KEY_KINEMATIC(9) true. */
            if (attr_count > 0) {
                aw += (uint32_t)snprintf(attrs_buf + aw,
                                         sizeof(attrs_buf) - aw, ",");
            }
            aw += (uint32_t)snprintf(attrs_buf + aw, sizeof(attrs_buf) - aw,
                                     "{\"key\":9,\"value\":true}");
            attr_count++;
        }
    }

    aw += (uint32_t)snprintf(attrs_buf + aw, sizeof(attrs_buf) - aw, "]");

    if (attr_count > 0) {
        w += (uint32_t)snprintf(args + w, sizeof(args) - w,
                                ",\"attrs\":%s", attrs_buf);
    }

    w += (uint32_t)snprintf(args + w, sizeof(args) - w, "}");

    int n = snprintf(out, out_cap,
                     "{\"id\":%u,\"cmd\":\"entity_def\",\"args\":%s}\n",
                     cmd_id, args);
    if (n < 0 || (uint32_t)n >= out_cap) return 0;
    return (uint32_t)n;
}
