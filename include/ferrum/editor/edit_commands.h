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

/** @brief Spawn an entity. Args: {"type":"box"|"sphere", "pos":[x,y,z], "rot":[rx,ry,rz], "scale":[sx,sy,sz]}. */
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

/** @brief Create named alias (@ marker). Args: {"name":"@x","pos":[],"rot":[]}. */
bool cmd_alias_create(edit_dispatch_t *d, const json_value_t *args,
                      json_value_t *result, json_arena_t *arena);

/** @brief Delete named alias. Args: {"name":"@x"}. */
bool cmd_alias_delete(edit_dispatch_t *d, const json_value_t *args,
                      json_value_t *result, json_arena_t *arena);

/** @brief List all aliases. Args: {"pattern":"regex"} (optional). */
bool cmd_alias_list(edit_dispatch_t *d, const json_value_t *args,
                    json_value_t *result, json_arena_t *arena);

/** @brief Select entities touching current selection. Args: {} (none). */
bool cmd_select_touching(edit_dispatch_t *d, const json_value_t *args,
                         json_value_t *result, json_arena_t *arena);

/** @brief Flood-fill select via touching until stable. Args: {} (none). */
bool cmd_select_fill(edit_dispatch_t *d, const json_value_t *args,
                     json_value_t *result, json_arena_t *arena);

/** @brief Save current selection as named group. Args: {"name":"&x"}. */
bool cmd_group_save(edit_dispatch_t *d, const json_value_t *args,
                    json_value_t *result, json_arena_t *arena);

/** @brief Delete a named group. Args: {"name":"&x"}. */
bool cmd_group_delete(edit_dispatch_t *d, const json_value_t *args,
                      json_value_t *result, json_arena_t *arena);

/** @brief List all groups. Args: {} (none). */
bool cmd_group_list(edit_dispatch_t *d, const json_value_t *args,
                    json_value_t *result, json_arena_t *arena);

/** @brief Create named group from selection with pivot. Args: {"name":"&x","pivot":[x,y,z],"parent":"&p"}. */
bool cmd_group(edit_dispatch_t *d, const json_value_t *args,
               json_value_t *result, json_arena_t *arena);

/** @brief Dissolve a named group (with undo). Args: {"name":"&x"}. */
bool cmd_ungroup(edit_dispatch_t *d, const json_value_t *args,
                 json_value_t *result, json_arena_t *arena);

/** @brief Select all entities in a named group. Args: {"name":"&x"}. */
bool cmd_select_group(edit_dispatch_t *d, const json_value_t *args,
                      json_value_t *result, json_arena_t *arena);

/** @brief Get info about a named group. Args: {"name":"&x"}. */
bool cmd_group_info(edit_dispatch_t *d, const json_value_t *args,
                    json_value_t *result, json_arena_t *arena);

/** @brief List assets. Args: {"prefix":"...", "type":"mesh"} (all optional). */
bool cmd_asset_list(edit_dispatch_t *d, const json_value_t *args,
                    json_value_t *result, json_arena_t *arena);

/** @brief Search assets by regex. Args: {"pattern":"regex"}. */
bool cmd_asset_search(edit_dispatch_t *d, const json_value_t *args,
                      json_value_t *result, json_arena_t *arena);

/** @brief Tab-complete asset paths. Args: {"prefix":"mesh"}. */
bool cmd_asset_complete(edit_dispatch_t *d, const json_value_t *args,
                        json_value_t *result, json_arena_t *arena);

/** @brief General tab-completion. Args: {"context":"spawn mesh p"}. */
bool cmd_complete(edit_dispatch_t *d, const json_value_t *args,
                  json_value_t *result, json_arena_t *arena);

/** @brief Browse assets. Args: {"prefix":"meshes/","filter":"wall"}. */
bool cmd_browse(edit_dispatch_t *d, const json_value_t *args,
                json_value_t *result, json_arena_t *arena);

/** @brief Material assign/query. Args: {"sub":"set","entity":0,"slot":"albedo","path":"..."}. */
bool cmd_material(edit_dispatch_t *d, const json_value_t *args,
                  json_value_t *result, json_arena_t *arena);

/** @brief Clone selected entities. Args: {"offset":[1,0,0]} (optional). */
bool cmd_clone(edit_dispatch_t *d, const json_value_t *args,
               json_value_t *result, json_arena_t *arena);

/** @brief Clone by ID. Args: {"entity_id":N, "offset":[dx,dy,dz]}.
 *  Returns new entity ID. */
bool cmd_clone_id(edit_dispatch_t *d, const json_value_t *args,
                  json_value_t *result, json_arena_t *arena);

/** @brief Delta-compressed entity sync.
 *  Args: {"since_version":V, "offset":N, "limit":N}.
 *  Returns changed entities + tombstones (delta) or paginated full list. */
bool cmd_sync_entities(edit_dispatch_t *d, const json_value_t *args,
                        json_value_t *result, json_arena_t *arena);

/* ------------------------------------------------------------------------ */
/* Script commands                                                            */
/* ------------------------------------------------------------------------ */

/** @brief Manage Aegis scripts. Args: {"action":"load|unload|list",...}. */
bool cmd_script(edit_dispatch_t *d, const json_value_t *args,
                json_value_t *result, json_arena_t *arena);

/* ------------------------------------------------------------------------ */
/* Mesh modeling commands                                                     */
/* ------------------------------------------------------------------------ */

/** @brief Create box mesh. Args: {"size":[w,h,d],"segments":[x,y,z],"pos":[x,y,z]}. */
bool cmd_mesh_create_box(edit_dispatch_t *d, const json_value_t *args,
                         json_value_t *result, json_arena_t *arena);

/** @brief Create sphere mesh. Args: {"radius":r,"segments":n,"pos":[x,y,z]}. */
bool cmd_mesh_create_sphere(edit_dispatch_t *d, const json_value_t *args,
                            json_value_t *result, json_arena_t *arena);

/** @brief Create cylinder mesh. Args: {"radius":r,"height":h,"segments":n,"axis":0|1|2,"pos":[]}. */
bool cmd_mesh_create_cylinder(edit_dispatch_t *d, const json_value_t *args,
                              json_value_t *result, json_arena_t *arena);

/** @brief Create plane mesh. Args: {"size":[w,h],"segments":[x,y],"axis":0|1|2,"pos":[]}. */
bool cmd_mesh_create_plane(edit_dispatch_t *d, const json_value_t *args,
                           json_value_t *result, json_arena_t *arena);

/** @brief Switch mesh selection mode. Args: {"mode":"vertex"|"edge"|"face"}. */
bool cmd_mesh_mode(edit_dispatch_t *d, const json_value_t *args,
                   json_value_t *result, json_arena_t *arena);

/** @brief Extrude selected faces. Args: {"distance":1.0}. */
bool cmd_extrude(edit_dispatch_t *d, const json_value_t *args,
                 json_value_t *result, json_arena_t *arena);

/** @brief Inset selected faces. Args: {"amount":0.5}. */
bool cmd_inset(edit_dispatch_t *d, const json_value_t *args,
               json_value_t *result, json_arena_t *arena);

/** @brief Bevel selected edges. Args: {"amount":0.5,"segments":1}. */
bool cmd_bevel(edit_dispatch_t *d, const json_value_t *args,
               json_value_t *result, json_arena_t *arena);

/** @brief Select mesh elements. Args: {"indices":[0,1,2]}. */
bool cmd_mesh_select(edit_dispatch_t *d, const json_value_t *args,
                     json_value_t *result, json_arena_t *arena);

/** @brief Deselect mesh elements. Args: {"indices":[0,1,2]}. */
bool cmd_mesh_deselect(edit_dispatch_t *d, const json_value_t *args,
                       json_value_t *result, json_arena_t *arena);

/** @brief Select all mesh elements. Args: {} (none). */
bool cmd_mesh_select_all(edit_dispatch_t *d, const json_value_t *args,
                         json_value_t *result, json_arena_t *arena);

/** @brief Deselect all mesh elements. Args: {} (none). */
bool cmd_mesh_deselect_all(edit_dispatch_t *d, const json_value_t *args,
                           json_value_t *result, json_arena_t *arena);

/** @brief Commit mesh to world entity. Args: {"entity_name":"...", "material_override":"..."}. */
bool cmd_mesh_commit(edit_dispatch_t *d, const json_value_t *args,
                     json_value_t *result, json_arena_t *arena);

/** @brief Query mesh stats. Args: {} (none). */
bool cmd_mesh_info(edit_dispatch_t *d, const json_value_t *args,
                   json_value_t *result, json_arena_t *arena);

/** @brief Execute a file of text commands. Args: {"file":"path"}. */
bool cmd_source(edit_dispatch_t *d, const json_value_t *args,
                json_value_t *result, json_arena_t *arena);

/** @brief Set an attribute on an entity. Args: {"entity":<id|name>, "key":<num>, "value":<val>}. */
bool cmd_setattr(edit_dispatch_t *d, const json_value_t *args,
                 json_value_t *result, json_arena_t *arena);

/** @brief Spawn entity with pre-applied attrs. Args: {"name":..., "type":..., "attrs":[...]}. */
bool cmd_entity_def(edit_dispatch_t *d, const json_value_t *args,
                    json_value_t *result, json_arena_t *arena);

/** @brief Create a physics joint between two entities. Args: {"joint_type":"hinge","entity_a":...,"entity_b":...,...}. */
bool cmd_joint(edit_dispatch_t *d, const json_value_t *args,
               json_value_t *result, json_arena_t *arena);

/** @brief Set physics material on an entity. Args: {"entity_id":N,"friction":f,"restitution":r}. */
bool cmd_physics_material(edit_dispatch_t *d, const json_value_t *args,
                          json_value_t *result, json_arena_t *arena);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_EDIT_COMMANDS_H */
