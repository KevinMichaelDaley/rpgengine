/**
 * @file editor_tui.c
 * @brief Standalone editor TUI controller.
 *
 * Terminal-based editor UI that connects to the editor server
 * and sends commands. Uses raw terminal mode for key input.
 *
 * Usage: ./editor_tui [host:port]
 * Default: 127.0.0.1:9100
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <regex.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <poll.h>

#include "ferrum/editor/ctrl_conn.h"
#include "ferrum/editor/ctrl_tui.h"
#include "ferrum/editor/ctrl_cmd_defs.h"
#include "ferrum/editor/edit_entity.h"

static atomic_bool g_running = true;
static struct termios g_orig_termios;

/* ── Entity type cache (populated from server on connect) ─────────── */

#define MAX_ENTITY_TYPES 32
static char    g_entity_types[MAX_ENTITY_TYPES][32];
static uint32_t g_entity_type_count = 0;
static uint32_t g_list_types_cmd_id = UINT32_MAX; /* ID of pending list_types. */

/* ── Entity name cache (populated from server on connect) ─────────── */

#define MAX_ENTITY_NAMES 256
static char     g_entity_names[MAX_ENTITY_NAMES][EDIT_ENTITY_NAME_MAX];
static uint32_t g_entity_name_ids[MAX_ENTITY_NAMES];
static uint32_t g_entity_name_count = 0;
static uint32_t g_list_entities_cmd_id = UINT32_MAX;

/* ── Group name cache (populated from server on connect) ──────────── */

#define MAX_GROUP_NAMES 64
static char     g_group_names[MAX_GROUP_NAMES][64];
static uint32_t g_group_name_count = 0;
static uint32_t g_group_list_cmd_id = UINT32_MAX;

/* Forward declarations for response parsers. */
static void parse_group_list_result_(const char *json);

/* Commands that trigger an entity name cache refresh. */
static bool needs_entity_refresh_(const char *cmd_text) {
    /* Extract first word (command name) from the text. */
    char cmd[32];
    size_t i = 0;
    while (cmd_text[i] && cmd_text[i] != ' ' && i < sizeof(cmd) - 1) {
        cmd[i] = cmd_text[i];
        i++;
    }
    cmd[i] = '\0';
    return strcmp(cmd, "spawn") == 0
        || strcmp(cmd, "delete") == 0
        || strcmp(cmd, "delete_id") == 0
        || strcmp(cmd, "load") == 0
        || strcmp(cmd, "alias_create") == 0
        || strcmp(cmd, "alias_delete") == 0;
}

/**
 * @brief Check if a command should trigger a group name cache refresh.
 */
static bool needs_group_refresh_(const char *cmd_text) {
    char cmd[32];
    size_t i = 0;
    while (cmd_text[i] && cmd_text[i] != ' ' && i < sizeof(cmd) - 1) {
        cmd[i] = cmd_text[i];
        i++;
    }
    cmd[i] = '\0';
    return strcmp(cmd, "group_save") == 0
        || strcmp(cmd, "group_delete") == 0;
}

/**
 * @brief Build a comma-separated list of cached entity types.
 */
static void build_type_list_string_(char *buf, size_t cap) {
    size_t pos = 0;
    for (uint32_t i = 0; i < g_entity_type_count && pos < cap - 1; i++) {
        if (i > 0) {
            int n = snprintf(buf + pos, cap - pos, ", ");
            if (n > 0) pos += (size_t)n;
        }
        int n = snprintf(buf + pos, cap - pos, "%s", g_entity_types[i]);
        if (n > 0) pos += (size_t)n;
    }
}

/* ── Minimal JSON response parser ─────────────────────────────────── */

/**
 * @brief Extract a numeric field value from a JSON string.
 *
 * Finds "field":NUMBER and returns the integer value.
 * Returns -1 if field not found.
 */
static int json_get_int_(const char *json, const char *field) {
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\":", field);
    const char *p = strstr(json, needle);
    if (!p) return -1;
    p += strlen(needle);
    while (*p == ' ') p++;
    return atoi(p);
}

/**
 * @brief Check if JSON has "ok":true.
 */
static bool json_get_ok_(const char *json) {
    const char *p = strstr(json, "\"ok\":");
    if (!p) return false;
    p += 5;
    while (*p == ' ') p++;
    return (strncmp(p, "true", 4) == 0);
}

/**
 * @brief Extract string field value from JSON (copies into buf).
 * @return true if found.
 */
static bool json_get_string_(const char *json, const char *field,
                              char *buf, size_t cap) {
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\":\"", field);
    const char *p = strstr(json, needle);
    if (!p) return false;
    p += strlen(needle);
    size_t i = 0;
    while (*p && *p != '"' && i < cap - 1) {
        buf[i++] = *p++;
    }
    buf[i] = '\0';
    return true;
}

/**
 * @brief Extract the "result" field as a display string.
 *
 * For simple values (numbers, strings, bools), returns a readable form.
 * For null, returns empty string.
 */
static bool json_get_result_display_(const char *json, char *buf, size_t cap) {
    const char *p = strstr(json, "\"result\":");
    if (!p) return false;
    p += 9;
    while (*p == ' ') p++;

    if (strncmp(p, "null", 4) == 0) {
        buf[0] = '\0';
        return true;
    }

    /* Copy the value until we hit } or end of string. */
    size_t i = 0;
    if (*p == '"') {
        /* String value. */
        p++;
        while (*p && *p != '"' && i < cap - 1) buf[i++] = *p++;
    } else {
        /* Number, bool, array, etc. */
        while (*p && *p != '}' && *p != ',' && i < cap - 1) buf[i++] = *p++;
    }
    buf[i] = '\0';
    return true;
}

/**
 * @brief Parse a JSON array of strings from "result":[...].
 *
 * Populates the entity type cache from list_types response.
 */
static void parse_type_list_result_(const char *json) {
    const char *p = strstr(json, "\"result\":");
    if (!p) return;
    p += 9;
    while (*p == ' ') p++;
    if (*p != '[') return;
    p++; /* Skip '[' */

    g_entity_type_count = 0;
    while (*p && *p != ']' && g_entity_type_count < MAX_ENTITY_TYPES) {
        while (*p == ' ' || *p == ',') p++;
        if (*p == '"') {
            p++; /* Skip opening quote. */
            size_t i = 0;
            while (*p && *p != '"' && i < 31) {
                g_entity_types[g_entity_type_count][i++] = *p++;
            }
            g_entity_types[g_entity_type_count][i] = '\0';
            if (*p == '"') p++;
            g_entity_type_count++;
        } else {
            break;
        }
    }
}

/**
 * @brief Parse list_entities response into the entity name cache.
 *
 * Response result is [{"id":N,"name":"...","type":"..."}, ...].
 * Only entities with non-empty names are cached (for tab completion).
 */
static void parse_entity_list_result_(const char *json) {
    const char *p = strstr(json, "\"result\":");
    if (!p) return;
    p += 9;
    while (*p == ' ') p++;
    if (*p != '[') return;
    p++;

    g_entity_name_count = 0;

    while (*p && *p != ']' && g_entity_name_count < MAX_ENTITY_NAMES) {
        while (*p == ' ' || *p == ',') p++;
        if (*p != '{') break;
        p++;

        /* Parse object fields: id, name, type. */
        uint32_t eid = UINT32_MAX;
        char name[EDIT_ENTITY_NAME_MAX];
        name[0] = '\0';

        while (*p && *p != '}') {
            while (*p == ' ' || *p == ',') p++;
            if (*p != '"') { p++; continue; }
            p++; /* Skip opening quote. */

            /* Read key. */
            char key[16];
            size_t ki = 0;
            while (*p && *p != '"' && ki < sizeof(key) - 1) key[ki++] = *p++;
            key[ki] = '\0';
            if (*p == '"') p++;
            while (*p == ' ' || *p == ':') p++;

            if (strcmp(key, "id") == 0) {
                eid = (uint32_t)strtoul(p, NULL, 10);
                while (*p && *p != ',' && *p != '}') p++;
            } else if (strcmp(key, "name") == 0 && *p == '"') {
                p++;
                size_t ni = 0;
                while (*p && *p != '"' && ni < sizeof(name) - 1) {
                    name[ni++] = *p++;
                }
                name[ni] = '\0';
                if (*p == '"') p++;
            } else {
                /* Skip value. */
                if (*p == '"') {
                    p++;
                    while (*p && *p != '"') p++;
                    if (*p == '"') p++;
                } else {
                    while (*p && *p != ',' && *p != '}') p++;
                }
            }
        }
        if (*p == '}') p++;

        /* Cache named entities for tab completion. */
        if (name[0] != '\0' && eid != UINT32_MAX) {
            strncpy(g_entity_names[g_entity_name_count], name,
                    EDIT_ENTITY_NAME_MAX - 1);
            g_entity_names[g_entity_name_count][EDIT_ENTITY_NAME_MAX - 1] = '\0';
            g_entity_name_ids[g_entity_name_count] = eid;
            g_entity_name_count++;
        }
    }
}

/**
 * @brief Parse a server response JSON and update the TUI log.
 *
 * Success with null result: mark command line with green ✓.
 * Success with result: mark ✓ and show result on next line.
 * Failure: mark command line with red ✗, show error on next line.
 */
static void parse_server_response_(ctrl_tui_t *tui, const char *json) {
    int id = json_get_int_(json, "id");
    bool ok = json_get_ok_(json);

    if (id >= 0) {
        /* Check if this is a bootstrap response. */
        bool is_type_bootstrap = ((uint32_t)id == g_list_types_cmd_id);
        bool is_entity_bootstrap = ((uint32_t)id == g_list_entities_cmd_id);
        bool is_group_bootstrap = ((uint32_t)id == g_group_list_cmd_id);

        if (ok) {
            if (is_type_bootstrap) {
                parse_type_list_result_(json);
                g_list_types_cmd_id = UINT32_MAX;
                return;
            }
            if (is_entity_bootstrap) {
                parse_entity_list_result_(json);
                g_list_entities_cmd_id = UINT32_MAX;
                return;
            }
            if (is_group_bootstrap) {
                parse_group_list_result_(json);
                g_group_list_cmd_id = UINT32_MAX;
                return;
            }

            ctrl_log_set_cmd_status(&tui->log, (uint32_t)id,
                                    CTRL_LOG_STATUS_OK);

            /* Show result value only when informative (not null/true/false). */
            char result[128];
            if (json_get_result_display_(json, result, sizeof(result))
                && result[0] != '\0'
                && strcmp(result, "null") != 0
                && strcmp(result, "true") != 0
                && strcmp(result, "false") != 0) {
                char msg[192];
                snprintf(msg, sizeof(msg), "  → %s", result);
                ctrl_log_add(&tui->log, 0, msg);
            }
        } else {
            if (is_type_bootstrap) {
                g_list_types_cmd_id = UINT32_MAX;
                return;
            }
            if (is_entity_bootstrap) {
                g_list_entities_cmd_id = UINT32_MAX;
                return;
            }
            if (is_group_bootstrap) {
                g_group_list_cmd_id = UINT32_MAX;
                return;
            }

            ctrl_log_set_cmd_status(&tui->log, (uint32_t)id,
                                    CTRL_LOG_STATUS_FAIL);

            /* Show error message (not raw JSON). */
            char error[128];
            if (json_get_string_(json, "error", error, sizeof(error))) {
                /* Convert underscored error codes to readable text. */
                for (char *c = error; *c; c++) {
                    if (*c == '_') *c = ' ';
                }
                ctrl_log_add(&tui->log, 2, error);
            } else {
                ctrl_log_add(&tui->log, 2, "command failed");
            }
        }
    } else {
        /* Not a command response — show as-is (e.g., server broadcast). */
        ctrl_log_add(&tui->log, 0, json);
    }
}

/**
 * @brief Send a list_entities query to refresh the entity name cache.
 */
static void send_entity_refresh_(ctrl_conn_t *conn) {
    g_list_entities_cmd_id = conn->next_id;
    char json[128];
    int n = snprintf(json, sizeof(json),
                     "{\"id\":%u,\"cmd\":\"list_entities\",\"args\":{}}\n",
                     conn->next_id);
    if (n > 0) {
        ctrl_conn_send_raw(conn, json, (uint32_t)n);
        conn->next_id++;
    }
}

/**
 * @brief Send a group_list query to refresh the group name cache.
 */
static void send_group_refresh_(ctrl_conn_t *conn) {
    g_group_list_cmd_id = conn->next_id;
    char json[128];
    int n = snprintf(json, sizeof(json),
                     "{\"id\":%u,\"cmd\":\"group_list\",\"args\":{}}\n",
                     conn->next_id);
    if (n > 0) {
        ctrl_conn_send_raw(conn, json, (uint32_t)n);
        conn->next_id++;
    }
}

/**
 * @brief Parse group_list result into the group name cache.
 *
 * The result is an array of strings like "&walls(3)" — we extract
 * just the group name (up to the '(' character).
 */
static void parse_group_list_result_(const char *json) {
    g_group_name_count = 0;
    /* Quick scan: find "result":[ ... ] and extract strings. */
    const char *arr = strstr(json, "\"result\":[");
    if (!arr) return;
    arr += 10; /* skip "result":[ */

    while (*arr && g_group_name_count < MAX_GROUP_NAMES) {
        /* Skip whitespace and commas. */
        while (*arr == ' ' || *arr == ',' || *arr == '\n') arr++;
        if (*arr == ']') break;
        if (*arr != '"') { arr++; continue; }
        arr++; /* skip opening quote */

        /* Extract string content up to '(' or '"'. */
        char *dst = g_group_names[g_group_name_count];
        size_t di = 0;
        while (*arr && *arr != '"' && *arr != '(' && di < 63) {
            dst[di++] = *arr++;
        }
        dst[di] = '\0';
        /* Skip to closing quote. */
        while (*arr && *arr != '"') arr++;
        if (*arr == '"') arr++;

        if (di > 0 && dst[0] == '&') {
            g_group_name_count++;
        }
    }
}

/**
 * @brief Handle the local "find" command.
 *
 * Syntax: find entities [pattern]
 *         find types [pattern]
 *
 * Searches cached entity names or type names by POSIX regex.
 * Results shown in the TUI log.
 *
 * @return true if the command was handled (even if no results).
 */
static bool handle_find_(ctrl_tui_t *tui, const char *text) {
    /* Extract first word to check against 'find' or its alias 'f'. */
    char first[16];
    size_t fi = 0;
    const char *tp = text;
    while (*tp && *tp != ' ' && fi < sizeof(first) - 1) first[fi++] = *tp++;
    first[fi] = '\0';

    const ctrl_cmd_def_t *def = ctrl_cmd_defs_find(first);
    if (!def || strcmp(def->name, "find") != 0) return false;

    const char *p = tp;
    while (*p == ' ') p++;
    if (*p == '\0') {
        ctrl_log_add(&tui->log, 0, "Usage: find <entities|types> [pattern]");
        return true;
    }

    /* Extract category. */
    char category[32];
    size_t ci = 0;
    while (*p && *p != ' ' && ci < sizeof(category) - 1) category[ci++] = *p++;
    category[ci] = '\0';
    while (*p == ' ') p++;

    /* Compile optional regex pattern. */
    regex_t regex;
    bool has_pattern = (*p != '\0');
    if (has_pattern) {
        int rc = regcomp(&regex, p, REG_EXTENDED | REG_NOSUB | REG_ICASE);
        if (rc != 0) {
            ctrl_log_add(&tui->log, 2, "Invalid regex pattern");
            return true;
        }
    }

    if (strcmp(category, "entities") == 0 || strcmp(category, "e") == 0) {
        uint32_t shown = 0;
        for (uint32_t i = 0; i < g_entity_name_count; i++) {
            if (has_pattern &&
                regexec(&regex, g_entity_names[i], 0, NULL, 0) != 0) {
                continue;
            }
            char line[300];
            snprintf(line, sizeof(line), "  [%u] %s",
                     g_entity_name_ids[i], g_entity_names[i]);
            ctrl_log_add(&tui->log, 0, line);
            shown++;
        }
        if (shown == 0) {
            ctrl_log_add(&tui->log, 0, "  (no matching entities)");
        } else {
            char summary[64];
            snprintf(summary, sizeof(summary), "%u entit%s found",
                     shown, shown == 1 ? "y" : "ies");
            ctrl_log_add(&tui->log, 0, summary);
        }
    } else if (strcmp(category, "types") == 0 || strcmp(category, "t") == 0) {
        uint32_t shown = 0;
        for (uint32_t i = 0; i < g_entity_type_count; i++) {
            if (has_pattern &&
                regexec(&regex, g_entity_types[i], 0, NULL, 0) != 0) {
                continue;
            }
            char line[64];
            snprintf(line, sizeof(line), "  %s", g_entity_types[i]);
            ctrl_log_add(&tui->log, 0, line);
            shown++;
        }
        if (shown == 0) {
            ctrl_log_add(&tui->log, 0, "  (no matching types)");
        } else {
            char summary[64];
            snprintf(summary, sizeof(summary), "%u type%s found",
                     shown, shown == 1 ? "" : "s");
            ctrl_log_add(&tui->log, 0, summary);
        }
    } else {
        ctrl_log_add(&tui->log, 1,
                     "Unknown category. Use: find entities|types [pattern]");
    }

    if (has_pattern) regfree(&regex);
    return true;
}

static void sigint_handler_(int sig) {
    (void)sig;
    g_running = false;
}

static void restore_terminal_(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
    /* Show cursor and reset. */
    printf("\033[?25h\033[0m\n");
}

static bool raw_mode_(void) {
    if (tcgetattr(STDIN_FILENO, &g_orig_termios) < 0) return false;
    atexit(restore_terminal_);

    struct termios raw = g_orig_termios;
    raw.c_iflag &= ~(unsigned)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(unsigned)(OPOST);
    raw.c_cflag |= (unsigned)(CS8);
    raw.c_lflag &= ~(unsigned)(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;

    return tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0;
}

/** @brief Check if user text is a help query and display help if so. */
static bool handle_help_query_(ctrl_tui_t *tui, const char *text) {
    /* "?" alone → show all commands. */
    if (strcmp(text, "?") == 0) {
        ctrl_log_add(&tui->log, 0, "Available commands:");
        uint32_t count = 0;
        const ctrl_cmd_def_t *defs = ctrl_cmd_defs_table(&count);
        for (uint32_t i = 0; i < count; i++) {
            char line[256];
            if (defs[i].alias) {
                snprintf(line, sizeof(line), "  %-14s %-3s %s",
                         defs[i].name, defs[i].alias, defs[i].help);
            } else {
                snprintf(line, sizeof(line), "  %-14s     %s",
                         defs[i].name, defs[i].help);
            }
            ctrl_log_add(&tui->log, 0, line);
        }
        ctrl_log_add(&tui->log, 0, "  q/quit                Quit editor.");
        return true;
    }

    /* "?command" or "command ?" → show usage for that command. */
    const char *cmd_name = text;
    bool is_help = false;

    /* Check "?command" form. */
    if (text[0] == '?') {
        cmd_name = text + 1;
        is_help = true;
    }

    /* Check "command ?" form. */
    if (!is_help) {
        size_t len = strlen(text);
        if (len >= 2 && text[len - 1] == '?') {
            /* Copy and strip trailing ? and whitespace. */
            static char name_buf[128];
            strncpy(name_buf, text, sizeof(name_buf) - 1);
            name_buf[sizeof(name_buf) - 1] = '\0';
            /* Strip trailing "?" and spaces. */
            char *end = name_buf + strlen(name_buf) - 1;
            while (end > name_buf && (*end == '?' || *end == ' ')) {
                *end-- = '\0';
            }
            cmd_name = name_buf;
            is_help = true;
        }
    }

    if (!is_help) return false;

    const ctrl_cmd_def_t *def = ctrl_cmd_defs_find(cmd_name);
    if (def) {
        char line[256];
        snprintf(line, sizeof(line), "Usage: %s", def->usage);
        ctrl_log_add(&tui->log, 0, line);
        ctrl_log_add(&tui->log, 0, def->help);

        /* For spawn, also list available entity types. */
        if (strcmp(cmd_name, "spawn") == 0 && g_entity_type_count > 0) {
            char types_str[256];
            build_type_list_string_(types_str, sizeof(types_str));
            char types_line[256];
            snprintf(types_line, sizeof(types_line),
                     "  Types: %s", types_str);
            ctrl_log_add(&tui->log, 0, types_line);
        }
    } else {
        char line[256];
        snprintf(line, sizeof(line), "Unknown command: %s (type ? for list)",
                 cmd_name);
        ctrl_log_add(&tui->log, 1, line);
    }
    return true;
}

/** @brief Handle Tab completion in command mode. */
static void handle_tab_(ctrl_tui_t *tui) {
    if (tui->cmd_len == 0) return;

    char prefix[CTRL_CMD_MAX_LEN];
    memcpy(prefix, tui->cmd_text, tui->cmd_len);
    prefix[tui->cmd_len] = '\0';

    /* Find first space — determines if we're completing cmd or arg. */
    char *space = strchr(prefix, ' ');

    if (!space) {
        /* Completing command name. */
        const char *matches[32];
        uint32_t match_count = ctrl_cmd_complete(prefix, matches, 32);

        if (match_count == 1) {
            size_t len = strlen(matches[0]);
            if (len < CTRL_CMD_MAX_LEN - 2) {
                memcpy(tui->cmd_text, matches[0], len);
                tui->cmd_text[len] = ' ';
                tui->cmd_len = (uint32_t)(len + 1);
                tui->cmd_text[tui->cmd_len] = '\0';
                tui->cmd_cursor = tui->cmd_len;
            }
        } else if (match_count > 1) {
            ctrl_log_add(&tui->log, 0, "Completions:");
            char line[512];
            line[0] = '\0';
            size_t pos = 0;
            for (uint32_t i = 0; i < match_count; i++) {
                int n = snprintf(line + pos, sizeof(line) - pos, "  %s",
                                 matches[i]);
                if (n > 0) pos += (size_t)n;
            }
            ctrl_log_add(&tui->log, 0, line);

            /* Complete common prefix. */
            size_t common = strlen(matches[0]);
            for (uint32_t i = 1; i < match_count; i++) {
                size_t j = 0;
                while (j < common && matches[0][j] == matches[i][j]) j++;
                common = j;
            }
            if (common > tui->cmd_len) {
                memcpy(tui->cmd_text, matches[0], common);
                tui->cmd_len = (uint32_t)common;
                tui->cmd_text[tui->cmd_len] = '\0';
                tui->cmd_cursor = tui->cmd_len;
            }
        }
        return;
    }

    /* Past command name — determine arg completion type. */
    *space = '\0';
    const char *cmd = prefix;
    const char *arg_start = space + 1;
    while (*arg_start == ' ') arg_start++;

    /* Don't complete if we already have more args after the first arg. */
    bool has_more_args = (strchr(arg_start, ' ') != NULL);

    if (strcmp(cmd, "spawn") == 0 && !has_more_args &&
        g_entity_type_count > 0) {
        /* Complete spawn's type argument. */
        size_t arg_len = strlen(arg_start);
        const char *matches[MAX_ENTITY_TYPES];
        uint32_t match_count = 0;

        for (uint32_t i = 0; i < g_entity_type_count; i++) {
            if (strncmp(g_entity_types[i], arg_start, arg_len) == 0) {
                matches[match_count++] = g_entity_types[i];
            }
        }

        if (match_count == 1) {
            char rebuilt[CTRL_CMD_MAX_LEN];
            int n = snprintf(rebuilt, sizeof(rebuilt), "%s %s ",
                             cmd, matches[0]);
            if (n > 0 && (uint32_t)n < CTRL_CMD_MAX_LEN) {
                memcpy(tui->cmd_text, rebuilt, (size_t)n);
                tui->cmd_len = (uint32_t)n;
                tui->cmd_text[tui->cmd_len] = '\0';
                tui->cmd_cursor = tui->cmd_len;
            }
        } else if (match_count > 1) {
            ctrl_log_add(&tui->log, 0, "Types:");
            char line[512];
            line[0] = '\0';
            size_t pos = 0;
            for (uint32_t i = 0; i < match_count; i++) {
                int n = snprintf(line + pos, sizeof(line) - pos, "  %s",
                                 matches[i]);
                if (n > 0) pos += (size_t)n;
            }
            ctrl_log_add(&tui->log, 0, line);
        }
    } else if (!has_more_args &&
               (strcmp(cmd, "select") == 0 ||
                strcmp(cmd, "deselect") == 0 ||
                strcmp(cmd, "delete_id") == 0 ||
                strcmp(cmd, "move_id") == 0 ||
                strcmp(cmd, "rotate_id") == 0 ||
                strcmp(cmd, "scale_id") == 0 ||
                strcmp(cmd, "cursor_snap") == 0 ||
                strcmp(cmd, "alias_delete") == 0 ||
                strcmp(cmd, "group_delete") == 0)) {
        /* Complete entity or group name for commands that take entity_id. */
        size_t arg_len = strlen(arg_start);
        const char *matches[MAX_ENTITY_NAMES + MAX_GROUP_NAMES];
        uint32_t match_count = 0;

        /* For select/deselect/group_delete, include group names. */
        bool accepts_groups = (strcmp(cmd, "select") == 0 ||
                               strcmp(cmd, "deselect") == 0 ||
                               strcmp(cmd, "group_delete") == 0);
        if (accepts_groups) {
            for (uint32_t i = 0; i < g_group_name_count &&
                                  match_count < MAX_GROUP_NAMES; i++) {
                if (strncmp(g_group_names[i], arg_start, arg_len) == 0) {
                    matches[match_count++] = g_group_names[i];
                }
            }
        }

        /* Include entity names (skip if arg starts with & for group_delete). */
        bool skip_entities = (strcmp(cmd, "group_delete") == 0);
        if (!skip_entities && g_entity_name_count > 0) {
            for (uint32_t i = 0; i < g_entity_name_count &&
                                  match_count < MAX_ENTITY_NAMES + MAX_GROUP_NAMES; i++) {
                if (strncmp(g_entity_names[i], arg_start, arg_len) == 0) {
                    matches[match_count++] = g_entity_names[i];
                }
            }
        }

        if (match_count == 1) {
            char rebuilt[CTRL_CMD_MAX_LEN];
            int n = snprintf(rebuilt, sizeof(rebuilt), "%s %s ",
                             cmd, matches[0]);
            if (n > 0 && (uint32_t)n < CTRL_CMD_MAX_LEN) {
                memcpy(tui->cmd_text, rebuilt, (size_t)n);
                tui->cmd_len = (uint32_t)n;
                tui->cmd_text[tui->cmd_len] = '\0';
                tui->cmd_cursor = tui->cmd_len;
            }
        } else if (match_count > 1) {
            ctrl_log_add(&tui->log, 0, "Entities:");
            char line[512];
            line[0] = '\0';
            size_t pos = 0;
            for (uint32_t i = 0; i < match_count; i++) {
                int n = snprintf(line + pos, sizeof(line) - pos, "  %s",
                                 matches[i]);
                if (n > 0) pos += (size_t)n;
            }
            ctrl_log_add(&tui->log, 0, line);

            /* Complete common prefix. */
            size_t common = strlen(matches[0]);
            for (uint32_t i = 1; i < match_count; i++) {
                size_t j = 0;
                while (j < common && matches[0][j] == matches[i][j]) j++;
                common = j;
            }
            if (common > arg_len) {
                char rebuilt[CTRL_CMD_MAX_LEN];
                char partial[256];
                memcpy(partial, matches[0], common);
                partial[common] = '\0';
                int n = snprintf(rebuilt, sizeof(rebuilt), "%s %s",
                                 cmd, partial);
                if (n > 0 && (uint32_t)n < CTRL_CMD_MAX_LEN) {
                    memcpy(tui->cmd_text, rebuilt, (size_t)n);
                    tui->cmd_len = (uint32_t)n;
                    tui->cmd_text[tui->cmd_len] = '\0';
                    tui->cmd_cursor = tui->cmd_len;
                }
            }
        }
    } else if (!has_more_args && g_group_name_count > 0 &&
               (strcmp(cmd, "select_all") == 0 ||
                strcmp(cmd, "select_touching") == 0 ||
                strcmp(cmd, "select_fill") == 0)) {
        /* Complete group_mask for commands where it's the only arg. */
        size_t arg_len = strlen(arg_start);
        const char *matches[MAX_GROUP_NAMES];
        uint32_t match_count = 0;
        for (uint32_t i = 0; i < g_group_name_count &&
                              match_count < MAX_GROUP_NAMES; i++) {
            if (strncmp(g_group_names[i], arg_start, arg_len) == 0) {
                matches[match_count++] = g_group_names[i];
            }
        }
        if (match_count == 1) {
            char rebuilt[CTRL_CMD_MAX_LEN];
            int n = snprintf(rebuilt, sizeof(rebuilt), "%s %s ",
                             cmd, matches[0]);
            if (n > 0 && (uint32_t)n < CTRL_CMD_MAX_LEN) {
                memcpy(tui->cmd_text, rebuilt, (size_t)n);
                tui->cmd_len = (uint32_t)n;
                tui->cmd_text[tui->cmd_len] = '\0';
                tui->cmd_cursor = tui->cmd_len;
            }
        } else if (match_count > 1) {
            ctrl_log_add(&tui->log, 0, "Groups:");
            char line[512];
            line[0] = '\0';
            size_t pos = 0;
            for (uint32_t i = 0; i < match_count; i++) {
                int n = snprintf(line + pos, sizeof(line) - pos, "  %s",
                                 matches[i]);
                if (n > 0) pos += (size_t)n;
            }
            ctrl_log_add(&tui->log, 0, line);
        }
    } else if (has_more_args && g_group_name_count > 0 &&
               (strcmp(cmd, "select_regex") == 0 ||
                strcmp(cmd, "select_near") == 0)) {
        /* Complete trailing &group for select_regex/select_near. */
        const char *last_space = strrchr(arg_start, ' ');
        const char *last_arg = last_space ? last_space + 1 : arg_start;
        if (last_arg[0] == '&' || last_arg[0] == '\0') {
            size_t la_len = strlen(last_arg);
            const char *matches[MAX_GROUP_NAMES];
            uint32_t match_count = 0;
            for (uint32_t i = 0; i < g_group_name_count &&
                                  match_count < MAX_GROUP_NAMES; i++) {
                if (strncmp(g_group_names[i], last_arg, la_len) == 0) {
                    matches[match_count++] = g_group_names[i];
                }
            }
            if (match_count == 1) {
                /* Rebuild: cmd + original args up to last_arg + match. */
                char rebuilt[CTRL_CMD_MAX_LEN];
                size_t prefix_len = (size_t)(last_arg - arg_start);
                char prior_args[256];
                if (prefix_len >= sizeof(prior_args))
                    prefix_len = sizeof(prior_args) - 1;
                memcpy(prior_args, arg_start, prefix_len);
                prior_args[prefix_len] = '\0';
                int n = snprintf(rebuilt, sizeof(rebuilt), "%s %s%s ",
                                 cmd, prior_args, matches[0]);
                if (n > 0 && (uint32_t)n < CTRL_CMD_MAX_LEN) {
                    memcpy(tui->cmd_text, rebuilt, (size_t)n);
                    tui->cmd_len = (uint32_t)n;
                    tui->cmd_text[tui->cmd_len] = '\0';
                    tui->cmd_cursor = tui->cmd_len;
                }
            } else if (match_count > 1) {
                ctrl_log_add(&tui->log, 0, "Groups:");
                char line[512];
                line[0] = '\0';
                size_t pos = 0;
                for (uint32_t i = 0; i < match_count; i++) {
                    int n = snprintf(line + pos, sizeof(line) - pos, "  %s",
                                     matches[i]);
                    if (n > 0) pos += (size_t)n;
                }
                ctrl_log_add(&tui->log, 0, line);
            }
        }
    } else if (strcmp(cmd, "find") == 0 && !has_more_args) {
        /* Complete find's category argument. */
        static const char *categories[] = {"entities", "types"};
        size_t arg_len = strlen(arg_start);
        const char *matches[2];
        uint32_t match_count = 0;
        for (int i = 0; i < 2; i++) {
            if (strncmp(categories[i], arg_start, arg_len) == 0) {
                matches[match_count++] = categories[i];
            }
        }
        if (match_count == 1) {
            char rebuilt[CTRL_CMD_MAX_LEN];
            int n = snprintf(rebuilt, sizeof(rebuilt), "%s %s ",
                             cmd, matches[0]);
            if (n > 0 && (uint32_t)n < CTRL_CMD_MAX_LEN) {
                memcpy(tui->cmd_text, rebuilt, (size_t)n);
                tui->cmd_len = (uint32_t)n;
                tui->cmd_text[tui->cmd_len] = '\0';
                tui->cmd_cursor = tui->cmd_len;
            }
        } else if (match_count > 1) {
            ctrl_log_add(&tui->log, 0, "  entities  types");
        }
    }
}

int main(int argc, char **argv) {
    const char *host = "127.0.0.1";
    uint16_t port = 9100;

    if (argc > 1) {
        /* Parse host:port or host port. */
        char *colon = strchr(argv[1], ':');
        if (colon) {
            static char host_buf[256];
            size_t hlen = (size_t)(colon - argv[1]);
            if (hlen >= sizeof(host_buf)) hlen = sizeof(host_buf) - 1;
            memcpy(host_buf, argv[1], hlen);
            host_buf[hlen] = '\0';
            host = host_buf;
            port = (uint16_t)atoi(colon + 1);
        } else if (argc > 2) {
            host = argv[1];
            port = (uint16_t)atoi(argv[2]);
        } else {
            port = (uint16_t)atoi(argv[1]);
        }
    }

    signal(SIGINT, sigint_handler_);
    signal(SIGTERM, sigint_handler_);

    /* Connect to editor server. */
    ctrl_conn_t conn;
    ctrl_conn_init(&conn);

    printf("Connecting to %s:%u...\n", host, port);
    if (!ctrl_conn_connect(&conn, host, port)) {
        fprintf(stderr, "Failed to connect to %s:%u\n", host, port);
        return 1;
    }
    printf("Connected!\n");
    usleep(200000);

    /* Init TUI. */
    ctrl_tui_t tui;
    ctrl_tui_init(&tui);

    {
        char msg[256];
        snprintf(msg, sizeof(msg), "Connected to %s:%u", host, port);
        ctrl_log_add(&tui.log, 0, msg);
    }
    ctrl_log_add(&tui.log, 0, "Type :? for command list, :cmd ? for help");
    ctrl_log_add(&tui.log, 0, "Tab to autocomplete, :q to quit");

    /* Query entity types from server (bootstrap). */
    {
        g_list_types_cmd_id = conn.next_id;
        char json[128];
        int n = snprintf(json, sizeof(json),
                         "{\"id\":%u,\"cmd\":\"list_types\",\"args\":{}}\n",
                         conn.next_id);
        if (n > 0) {
            ctrl_conn_send_raw(&conn, json, (uint32_t)n);
            conn.next_id++;
        }
    }

    /* Query entity names from server (bootstrap). */
    {
        g_list_entities_cmd_id = conn.next_id;
        char json[128];
        int n = snprintf(json, sizeof(json),
                         "{\"id\":%u,\"cmd\":\"list_entities\",\"args\":{}}\n",
                         conn.next_id);
        if (n > 0) {
            ctrl_conn_send_raw(&conn, json, (uint32_t)n);
            conn.next_id++;
        }
    }

    /* Query group names from server (bootstrap). */
    send_group_refresh_(&conn);

    /* Enter raw terminal mode. */
    if (!raw_mode_()) {
        fprintf(stderr, "Failed to set raw terminal mode\n");
        ctrl_conn_disconnect(&conn);
        return 1;
    }

    /* Get terminal size. */
    {
        struct winsize ws;
        if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0) {
            tui.cols = ws.ws_col;
            tui.rows = ws.ws_row;
        }
    }

    /* Main loop. */
    while (g_running) {
        /* Poll stdin + server socket. */
        struct pollfd fds[2];
        fds[0].fd = STDIN_FILENO;
        fds[0].events = POLLIN;
        fds[1].fd = conn.fd;
        fds[1].events = POLLIN;

        int ready = poll(fds, 2, 50);  /* 50ms timeout → ~20 Hz refresh */

        /* Handle keyboard input. */
        if (ready > 0 && (fds[0].revents & POLLIN)) {
            char ch;
            while (read(STDIN_FILENO, &ch, 1) == 1) {
                /* Tab completion in command mode. */
                if (tui.mode == CTRL_MODE_COMMAND && ch == '\t') {
                    handle_tab_(&tui);
                    continue;
                }

                const char *cmd = ctrl_tui_feed_key(&tui, ch);
                if (cmd && cmd[0] != '\0') {
                    /* Check for help query first. */
                    if (handle_help_query_(&tui, cmd)) {
                        continue;
                    }

                    /* Handle local 'find' command (no server roundtrip). */
                    if (handle_find_(&tui, cmd)) {
                        continue;
                    }

                    /* Quit command. */
                    if (strcmp(cmd, "q") == 0 || strcmp(cmd, "quit") == 0) {
                        g_running = false;
                        break;
                    }

                    /* Log the user command with pending status. */
                    char msg[512];
                    snprintf(msg, sizeof(msg), "%s", cmd);
                    uint32_t this_cmd_id = conn.next_id;

                    /* Build proper JSON and send. */
                    char json[4096];
                    uint32_t json_len = ctrl_cmd_build_json(
                        cmd, json, sizeof(json), conn.next_id);

                    if (json_len > 0) {
                        ctrl_log_add_cmd(&tui.log, msg, this_cmd_id);
                        ctrl_conn_send_raw(&conn, json, json_len);
                        conn.next_id++;

                        /* Refresh entity cache after mutating commands. */
                        if (needs_entity_refresh_(msg)) {
                            send_entity_refresh_(&conn);
                        }
                        /* Refresh group cache after group mutations. */
                        if (needs_group_refresh_(msg)) {
                            send_group_refresh_(&conn);
                        }
                    } else {
                        /* Build failed — check if command has required args. */
                        const ctrl_cmd_def_t *def = ctrl_cmd_defs_find(cmd);
                        if (def && def->arg_fmt) {
                            char hint[256];
                            snprintf(hint, sizeof(hint),
                                     "Usage: %s", def->usage);
                            ctrl_log_add(&tui.log, 1, hint);
                        } else {
                            /* Unknown cmd or no args needed — send raw. */
                            ctrl_log_add_cmd(&tui.log, msg, conn.next_id);
                            ctrl_conn_send_cmd(&conn, cmd);
                        }
                    }
                }
            }
        }

        /* Handle server responses. */
        if (ready > 0 && (fds[1].revents & POLLIN)) {
            if (ctrl_conn_recv(&conn)) {
                char line[4096];
                uint32_t len;
                while ((len = ctrl_conn_pop_line(&conn, line,
                                                 sizeof(line))) > 0) {
                    parse_server_response_(&tui, line);
                }
            } else if (conn.state == CTRL_CONN_ERROR) {
                ctrl_log_add(&tui.log, 2, "Server disconnected");
                g_running = false;
            }
        }

        /* Render TUI. */
        ctrl_tui_render(&tui);
    }

    ctrl_tui_destroy(&tui);
    ctrl_conn_disconnect(&conn);
    return 0;
}
