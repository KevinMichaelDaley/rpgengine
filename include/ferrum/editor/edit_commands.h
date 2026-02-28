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

/** @brief Select entity by ID. Args: {"entity_id":N, "toggle":bool}. */
bool cmd_select(edit_dispatch_t *d, const json_value_t *args,
                json_value_t *result, json_arena_t *arena);

/** @brief Deselect entity by ID. Args: {"entity_id":N}. */
bool cmd_deselect(edit_dispatch_t *d, const json_value_t *args,
                  json_value_t *result, json_arena_t *arena);

/** @brief Select all active entities. Args: {} (none). */
bool cmd_select_all(edit_dispatch_t *d, const json_value_t *args,
                    json_value_t *result, json_arena_t *arena);

/** @brief Deselect all entities. Args: {} (none). */
bool cmd_deselect_all(edit_dispatch_t *d, const json_value_t *args,
                      json_value_t *result, json_arena_t *arena);

/** @brief Delete entity by ID. Args: {"entity_id":N}. */
bool cmd_delete_id(edit_dispatch_t *d, const json_value_t *args,
                   json_value_t *result, json_arena_t *arena);

/** @brief Move entity by ID. Args: {"entity_id":N, "delta":[dx,dy,dz]}. */
bool cmd_move_id(edit_dispatch_t *d, const json_value_t *args,
                 json_value_t *result, json_arena_t *arena);

/** @brief Pause physics simulation. Args: {} (none). */
bool cmd_physics_pause(edit_dispatch_t *d, const json_value_t *args,
                       json_value_t *result, json_arena_t *arena);

/** @brief Resume physics simulation. Args: {} (none). */
bool cmd_physics_resume(edit_dispatch_t *d, const json_value_t *args,
                        json_value_t *result, json_arena_t *arena);

/** @brief Advance one physics tick (only while paused). Args: {} (none). */
bool cmd_physics_step(edit_dispatch_t *d, const json_value_t *args,
                      json_value_t *result, json_arena_t *arena);

/** @brief Reset physics (zero velocities, pause). Args: {} (none). */
bool cmd_physics_reset(edit_dispatch_t *d, const json_value_t *args,
                       json_value_t *result, json_arena_t *arena);

/** @brief List available entity types. Args: {} (none). */
bool cmd_list_types(edit_dispatch_t *d, const json_value_t *args,
                    json_value_t *result, json_arena_t *arena);

/** @brief List active entities. Args: {"pattern":"regex"} (optional). */
bool cmd_list_entities(edit_dispatch_t *d, const json_value_t *args,
                       json_value_t *result, json_arena_t *arena);

/** @brief Select entities by regex pattern. Args: {"pattern":"regex"}. */
bool cmd_select_regex(edit_dispatch_t *d, const json_value_t *args,
                      json_value_t *result, json_arena_t *arena);

/** @brief Rotate entity by ID. Args: {"entity_id":N, "delta":[rx,ry,rz]}. */
bool cmd_rotate_id(edit_dispatch_t *d, const json_value_t *args,
                   json_value_t *result, json_arena_t *arena);

/** @brief Scale entity by ID. Args: {"entity_id":N, "factor":[sx,sy,sz]}. */
bool cmd_scale_id(edit_dispatch_t *d, const json_value_t *args,
                  json_value_t *result, json_arena_t *arena);

/** @brief Select entities within distance. Args: {"pos":[x,y,z],"dist":r}. */
bool cmd_select_near(edit_dispatch_t *d, const json_value_t *args,
                     json_value_t *result, json_arena_t *arena);

/** @brief Deselect entities within distance. Args: {"pos":[x,y,z],"dist":r}. */
bool cmd_deselect_near(edit_dispatch_t *d, const json_value_t *args,
                       json_value_t *result, json_arena_t *arena);

/** @brief Deselect entities by regex pattern. Args: {"pattern":"regex"}. */
bool cmd_deselect_regex(edit_dispatch_t *d, const json_value_t *args,
                        json_value_t *result, json_arena_t *arena);

/** @brief Push @cursor position onto stack. Args: {} (none). */
bool cmd_cursor_push(edit_dispatch_t *d, const json_value_t *args,
                     json_value_t *result, json_arena_t *arena);

/** @brief Pop @cursor position from stack. Args: {} (none). */
bool cmd_cursor_pop(edit_dispatch_t *d, const json_value_t *args,
                    json_value_t *result, json_arena_t *arena);

/** @brief Snap @cursor to entity or selection center. Args: {"entity_id":N}. */
bool cmd_cursor_snap(edit_dispatch_t *d, const json_value_t *args,
                     json_value_t *result, json_arena_t *arena);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_EDIT_COMMANDS_H */
