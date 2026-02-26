/**
 * @file edit_commands.h
 * @brief Entity command handler declarations and bulk registration.
 *
 * Each command is implemented in its own .c file under src/editor/commands/.
 * Call edit_commands_register_all() to register all entity commands with
 * the dispatch table at startup.
 */
#ifndef FERRUM_EDITOR_EDIT_COMMANDS_H
#define FERRUM_EDITOR_EDIT_COMMANDS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ferrum/editor/edit_dispatch.h"

/* ------------------------------------------------------------------------ */
/* Bulk registration                                                         */
/* ------------------------------------------------------------------------ */

/**
 * @brief Register all entity commands (spawn, delete, move, rotate, scale)
 *        with the dispatch table.
 * @param dispatch  Dispatch to register handlers in.
 */
void edit_commands_register_all(edit_dispatch_t *dispatch);

/* ------------------------------------------------------------------------ */
/* Individual handlers (for direct testing or external registration)          */
/* ------------------------------------------------------------------------ */

/** @brief Spawn an entity. Args: {"type":"box"|"sphere", "pos":[x,y,z]}. */
bool cmd_spawn(edit_dispatch_t *d, const json_value_t *args,
               json_value_t *result, json_arena_t *arena);

/** @brief Delete selected entities. Args: {} (operates on selection). */
bool cmd_delete(edit_dispatch_t *d, const json_value_t *args,
                json_value_t *result, json_arena_t *arena);

/** @brief Move selected entities. Args: {"delta":[dx,dy,dz]}. */
bool cmd_move(edit_dispatch_t *d, const json_value_t *args,
              json_value_t *result, json_arena_t *arena);

/** @brief Rotate selected entities. Args: {"delta":[rx,ry,rz]}. */
bool cmd_rotate(edit_dispatch_t *d, const json_value_t *args,
                json_value_t *result, json_arena_t *arena);

/** @brief Scale selected entities. Args: {"factor":[sx,sy,sz]}. */
bool cmd_scale(edit_dispatch_t *d, const json_value_t *args,
               json_value_t *result, json_arena_t *arena);

/** @brief Save level to file. Args: {"path":"foo.json"}. */
bool cmd_save(edit_dispatch_t *d, const json_value_t *args,
              json_value_t *result, json_arena_t *arena);

/** @brief Load level from file. Args: {"path":"foo.json"}. */
bool cmd_load(edit_dispatch_t *d, const json_value_t *args,
              json_value_t *result, json_arena_t *arena);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_EDIT_COMMANDS_H */
