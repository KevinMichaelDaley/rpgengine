/**
 * @file ctrl_cmd_defs.c
 * @brief TUI command definitions table and lookup.
 *
 * Static table of all known editor commands with usage, help text,
 * and argument format descriptors for text-to-JSON conversion.
 *
 * Non-static functions: ctrl_cmd_defs_table, ctrl_cmd_defs_find (2).
 */

#include "ferrum/editor/ctrl_cmd_defs.h"
#include <string.h>

/* ── Command definition table ─────────────────────────────────────── */

static const ctrl_cmd_def_t s_defs[] = {
    {
        .name    = "spawn",
        .alias   = "sp",
        .usage   = "spawn <type> [name] <x> <y> <z>",
        .help    = "Spawn entity. Optional name before position.",
        .arg_fmt = "s:type f3:pos",
    },
    {
        .name    = "delete",
        .alias   = "del",
        .usage   = "delete",
        .help    = "Delete all selected entities.",
        .arg_fmt = NULL,
    },
    {
        .name    = "delete_id",
        .alias   = "di",
        .usage   = "delete_id <id_or_name>",
        .help    = "Delete entity by ID or name.",
        .arg_fmt = "s:entity_id",
    },
    {
        .name    = "move",
        .alias   = "mv",
        .usage   = "move <dx> <dy> <dz>",
        .help    = "Move selected entities by delta.",
        .arg_fmt = "f3:delta",
    },
    {
        .name    = "move_id",
        .alias   = "mi",
        .usage   = "move_id <id_or_name> <dx> <dy> <dz>",
        .help    = "Move entity by ID or name with delta.",
        .arg_fmt = "s:entity_id f3:delta",
    },
    {
        .name    = "rotate",
        .alias   = "rot",
        .usage   = "rotate <rx> <ry> <rz>",
        .help    = "Rotate selected entities by euler angles (degrees).",
        .arg_fmt = "f3:delta",
    },
    {
        .name    = "scale",
        .alias   = "sc",
        .usage   = "scale <sx> <sy> <sz>",
        .help    = "Scale selected entities by factor.",
        .arg_fmt = "f3:factor",
    },
    {
        .name    = "select",
        .alias   = "s",
        .usage   = "select <id_or_name> [toggle]",
        .help    = "Select entity by ID or name.",
        .arg_fmt = "s:entity_id",
    },
    {
        .name    = "deselect",
        .alias   = "ds",
        .usage   = "deselect <id_or_name>",
        .help    = "Deselect entity by ID or name.",
        .arg_fmt = "s:entity_id",
    },
    {
        .name    = "select_all",
        .alias   = "sa",
        .usage   = "select_all [&group]",
        .help    = "Select all active entities. Optional group mask.",
        .arg_fmt = NULL,
    },
    {
        .name    = "deselect_all",
        .alias   = "da",
        .usage   = "deselect_all",
        .help    = "Deselect all entities.",
        .arg_fmt = NULL,
    },
    {
        .name    = "save",
        .alias   = "w",
        .usage   = "save <path>",
        .help    = "Save level to JSON file.",
        .arg_fmt = "s:path",
    },
    {
        .name    = "load",
        .alias   = "ld",
        .usage   = "load <path>",
        .help    = "Load level from JSON file.",
        .arg_fmt = "s:path",
    },
    {
        .name    = "physics_pause",
        .alias   = "pp",
        .usage   = "physics_pause",
        .help    = "Pause physics simulation (bodies freeze).",
        .arg_fmt = NULL,
    },
    {
        .name    = "physics_resume",
        .alias   = "pr",
        .usage   = "physics_resume",
        .help    = "Resume physics simulation.",
        .arg_fmt = NULL,
    },
    {
        .name    = "physics_step",
        .alias   = "ps",
        .usage   = "physics_step",
        .help    = "Advance one physics tick (only while paused).",
        .arg_fmt = NULL,
    },
    {
        .name    = "physics_reset",
        .alias   = "px",
        .usage   = "physics_reset",
        .help    = "Zero all velocities and pause.",
        .arg_fmt = NULL,
    },
    {
        .name    = "list_types",
        .alias   = "lt",
        .usage   = "list_types",
        .help    = "List available entity types for spawn.",
        .arg_fmt = NULL,
    },
    {
        .name    = "list_entities",
        .alias   = "le",
        .usage   = "list_entities [pattern]",
        .help    = "List active entities. Optional regex filter on name.",
        .arg_fmt = "s:pattern",
    },
    {
        .name    = "find",
        .alias   = "f",
        .usage   = "find <entities|types> [pattern]",
        .help    = "Search entities or types by regex pattern.",
        .arg_fmt = NULL,  /* Handled locally in TUI. */
    },
    {
        .name    = "select_regex",
        .alias   = "sr",
        .usage   = "select_regex <pattern> [&group]",
        .help    = "Select entities whose name matches regex. Optional group mask.",
        .arg_fmt = NULL,
    },
    {
        .name    = "rotate_id",
        .alias   = "ri",
        .usage   = "rotate_id <id_or_name> <rx> <ry> <rz>",
        .help    = "Rotate entity by ID or name with euler delta (degrees).",
        .arg_fmt = "s:entity_id f3:delta",
    },
    {
        .name    = "scale_id",
        .alias   = "si",
        .usage   = "scale_id <id_or_name> <sx> <sy> <sz>",
        .help    = "Scale entity by ID or name with factor.",
        .arg_fmt = "s:entity_id f3:factor",
    },
    {
        .name    = "select_near",
        .alias   = "sn",
        .usage   = "select_near [x y z] <dist> [&group]",
        .help    = "Select entities within distance. Optional group mask.",
        .arg_fmt = NULL,  /* Custom parsing in TUI. */
    },
    {
        .name    = "deselect_near",
        .alias   = "dn",
        .usage   = "deselect_near [x y z] <dist>",
        .help    = "Deselect entities within distance. Defaults to @cursor.",
        .arg_fmt = NULL,  /* Custom parsing — same as select_near. */
    },
    {
        .name    = "deselect_regex",
        .alias   = "dr",
        .usage   = "deselect_regex <pattern>",
        .help    = "Deselect entities whose name matches regex pattern.",
        .arg_fmt = "s:pattern",
    },
    {
        .name    = "cursor_push",
        .alias   = "cp",
        .usage   = "cursor_push",
        .help    = "Push @cursor position onto stack.",
        .arg_fmt = NULL,
    },
    {
        .name    = "cursor_pop",
        .alias   = "co",
        .usage   = "cursor_pop",
        .help    = "Pop and restore @cursor position from stack.",
        .arg_fmt = NULL,
    },
    {
        .name    = "cursor_snap",
        .alias   = "cs",
        .usage   = "cursor_snap [id_or_name]",
        .help    = "Snap @cursor to entity, or selection center if none.",
        .arg_fmt = "s:entity_id",
    },
    {
        .name    = "alias_create",
        .alias   = "ac",
        .usage   = "alias_create @name [x y z] [rx ry rz]",
        .help    = "Create named reference point (pos defaults to @cursor).",
        .arg_fmt = NULL,  /* Custom parsing — variable args. */
    },
    {
        .name    = "alias_delete",
        .alias   = "ad",
        .usage   = "alias_delete @name",
        .help    = "Delete a named alias.",
        .arg_fmt = "s:name",
    },
    {
        .name    = "alias_list",
        .alias   = "al",
        .usage   = "alias_list [pattern]",
        .help    = "List all @ aliases. Optional regex filter.",
        .arg_fmt = "s:pattern",
    },
    {
        .name    = "select_touching",
        .alias   = "st",
        .usage   = "select_touching [&group]",
        .help    = "Select colliding entities. Optional group mask.",
        .arg_fmt = NULL,
    },
    {
        .name    = "select_fill",
        .alias   = "sf",
        .usage   = "select_fill [&group]",
        .help    = "Flood-fill select through touching chains. Optional mask.",
        .arg_fmt = NULL,
    },
    {
        .name    = "group_save",
        .alias   = "gs",
        .usage   = "group_save &name",
        .help    = "Save current selection as named group.",
        .arg_fmt = "s:name",
    },
    {
        .name    = "group_delete",
        .alias   = "gd",
        .usage   = "group_delete &name",
        .help    = "Delete a named selection group.",
        .arg_fmt = "s:name",
    },
    {
        .name    = "group_list",
        .alias   = "gl",
        .usage   = "group_list",
        .help    = "List all selection groups.",
        .arg_fmt = NULL,
    },
};

static const uint32_t s_def_count =
    (uint32_t)(sizeof(s_defs) / sizeof(s_defs[0]));

/* ── Public API ───────────────────────────────────────────────────── */

const ctrl_cmd_def_t *ctrl_cmd_defs_table(uint32_t *count) {
    if (count) *count = s_def_count;
    return s_defs;
}

const ctrl_cmd_def_t *ctrl_cmd_defs_find(const char *name) {
    if (!name) return NULL;
    for (uint32_t i = 0; i < s_def_count; i++) {
        if (strcmp(s_defs[i].name, name) == 0) {
            return &s_defs[i];
        }
        if (s_defs[i].alias && strcmp(s_defs[i].alias, name) == 0) {
            return &s_defs[i];
        }
    }
    return NULL;
}
