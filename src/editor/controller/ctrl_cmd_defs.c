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
        .usage   = "spawn <type> [name] <x> <y> <z>",
        .help    = "Spawn entity. Optional name before position.",
        .arg_fmt = "s:type f3:pos",
    },
    {
        .name    = "delete",
        .usage   = "delete",
        .help    = "Delete all selected entities.",
        .arg_fmt = NULL,
    },
    {
        .name    = "delete_id",
        .usage   = "delete_id <id_or_name>",
        .help    = "Delete entity by ID or name.",
        .arg_fmt = "s:entity_id",
    },
    {
        .name    = "move",
        .usage   = "move <dx> <dy> <dz>",
        .help    = "Move selected entities by delta.",
        .arg_fmt = "f3:delta",
    },
    {
        .name    = "move_id",
        .usage   = "move_id <id_or_name> <dx> <dy> <dz>",
        .help    = "Move entity by ID or name with delta.",
        .arg_fmt = "s:entity_id f3:delta",
    },
    {
        .name    = "rotate",
        .usage   = "rotate <rx> <ry> <rz>",
        .help    = "Rotate selected entities by euler angles (degrees).",
        .arg_fmt = "f3:delta",
    },
    {
        .name    = "scale",
        .usage   = "scale <sx> <sy> <sz>",
        .help    = "Scale selected entities by factor.",
        .arg_fmt = "f3:factor",
    },
    {
        .name    = "select",
        .usage   = "select <id_or_name> [toggle]",
        .help    = "Select entity by ID or name.",
        .arg_fmt = "s:entity_id",
    },
    {
        .name    = "deselect",
        .usage   = "deselect <id_or_name>",
        .help    = "Deselect entity by ID or name.",
        .arg_fmt = "s:entity_id",
    },
    {
        .name    = "select_all",
        .usage   = "select_all",
        .help    = "Select all active entities.",
        .arg_fmt = NULL,
    },
    {
        .name    = "deselect_all",
        .usage   = "deselect_all",
        .help    = "Deselect all entities.",
        .arg_fmt = NULL,
    },
    {
        .name    = "save",
        .usage   = "save <path>",
        .help    = "Save level to JSON file.",
        .arg_fmt = "s:path",
    },
    {
        .name    = "load",
        .usage   = "load <path>",
        .help    = "Load level from JSON file.",
        .arg_fmt = "s:path",
    },
    {
        .name    = "physics_pause",
        .usage   = "physics_pause",
        .help    = "Pause physics simulation (bodies freeze).",
        .arg_fmt = NULL,
    },
    {
        .name    = "physics_resume",
        .usage   = "physics_resume",
        .help    = "Resume physics simulation.",
        .arg_fmt = NULL,
    },
    {
        .name    = "physics_step",
        .usage   = "physics_step",
        .help    = "Advance one physics tick (only while paused).",
        .arg_fmt = NULL,
    },
    {
        .name    = "physics_reset",
        .usage   = "physics_reset",
        .help    = "Zero all velocities and pause.",
        .arg_fmt = NULL,
    },
    {
        .name    = "list_types",
        .usage   = "list_types",
        .help    = "List available entity types for spawn.",
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
    }
    return NULL;
}
