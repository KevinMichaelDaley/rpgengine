/**
 * @file edit_commands_register.c
 * @brief Bulk registration of all entity commands.
 */

#include "ferrum/editor/edit_commands.h"

void edit_commands_register_all(edit_dispatch_t *dispatch) {
    if (!dispatch) return;
    edit_dispatch_register(dispatch, "spawn",  cmd_spawn);
    edit_dispatch_register(dispatch, "delete", cmd_delete);
    edit_dispatch_register(dispatch, "move",   cmd_move);
    edit_dispatch_register(dispatch, "rotate", cmd_rotate);
    edit_dispatch_register(dispatch, "scale",  cmd_scale);
    edit_dispatch_register(dispatch, "save",   cmd_save);
    edit_dispatch_register(dispatch, "load",   cmd_load);
    edit_dispatch_register(dispatch, "select",       cmd_select);
    edit_dispatch_register(dispatch, "deselect",     cmd_deselect);
    edit_dispatch_register(dispatch, "select_all",   cmd_select_all);
    edit_dispatch_register(dispatch, "deselect_all", cmd_deselect_all);
    edit_dispatch_register(dispatch, "delete_id",    cmd_delete_id);
    edit_dispatch_register(dispatch, "move_id",      cmd_move_id);
    edit_dispatch_register(dispatch, "physics_pause",  cmd_physics_pause);
    edit_dispatch_register(dispatch, "physics_resume", cmd_physics_resume);
    edit_dispatch_register(dispatch, "physics_step",   cmd_physics_step);
    edit_dispatch_register(dispatch, "physics_reset",  cmd_physics_reset);
    edit_dispatch_register(dispatch, "list_types",     cmd_list_types);
    edit_dispatch_register(dispatch, "list_entities",  cmd_list_entities);
    edit_dispatch_register(dispatch, "select_regex",   cmd_select_regex);
    edit_dispatch_register(dispatch, "rotate_id",      cmd_rotate_id);
    edit_dispatch_register(dispatch, "scale_id",       cmd_scale_id);
    edit_dispatch_register(dispatch, "select_near",    cmd_select_near);
    edit_dispatch_register(dispatch, "deselect_near",  cmd_deselect_near);
    edit_dispatch_register(dispatch, "deselect_regex", cmd_deselect_regex);
    edit_dispatch_register(dispatch, "cursor_push",    cmd_cursor_push);
    edit_dispatch_register(dispatch, "cursor_pop",     cmd_cursor_pop);
    edit_dispatch_register(dispatch, "cursor_snap",    cmd_cursor_snap);
    edit_dispatch_register(dispatch, "alias_create",   cmd_alias_create);
    edit_dispatch_register(dispatch, "alias_delete",   cmd_alias_delete);
    edit_dispatch_register(dispatch, "alias_list",     cmd_alias_list);
    edit_dispatch_register(dispatch, "select_touching", cmd_select_touching);
    edit_dispatch_register(dispatch, "select_fill",     cmd_select_fill);
    edit_dispatch_register(dispatch, "group_save",      cmd_group_save);
    edit_dispatch_register(dispatch, "group_delete",    cmd_group_delete);
    edit_dispatch_register(dispatch, "group_list",      cmd_group_list);
    edit_dispatch_register(dispatch, "group",           cmd_group);
    edit_dispatch_register(dispatch, "ungroup",         cmd_ungroup);
    edit_dispatch_register(dispatch, "select_group",    cmd_select_group);
    edit_dispatch_register(dispatch, "group_info",      cmd_group_info);
    edit_dispatch_register(dispatch, "asset_list",      cmd_asset_list);
    edit_dispatch_register(dispatch, "asset_search",    cmd_asset_search);
    edit_dispatch_register(dispatch, "asset_complete",  cmd_asset_complete);
    edit_dispatch_register(dispatch, "complete",        cmd_complete);
    edit_dispatch_register(dispatch, "browse",          cmd_browse);
    edit_dispatch_register(dispatch, "material",        cmd_material);
    edit_dispatch_register(dispatch, "clone",           cmd_clone);
    edit_dispatch_register(dispatch, "script",          cmd_script);
    edit_dispatch_register(dispatch, "source",          cmd_source);
    edit_dispatch_register(dispatch, "setattr",         cmd_setattr);
    edit_dispatch_register(dispatch, "entity_def",      cmd_entity_def);
    edit_dispatch_register(dispatch, "joint",           cmd_joint);

    /* Mesh modeling commands */
    edit_dispatch_register(dispatch, "mesh_create_box",      cmd_mesh_create_box);
    edit_dispatch_register(dispatch, "mesh_create_sphere",   cmd_mesh_create_sphere);
    edit_dispatch_register(dispatch, "mesh_create_cylinder", cmd_mesh_create_cylinder);
    edit_dispatch_register(dispatch, "mesh_create_plane",    cmd_mesh_create_plane);
    edit_dispatch_register(dispatch, "mesh_mode",            cmd_mesh_mode);
    edit_dispatch_register(dispatch, "extrude",              cmd_extrude);
    edit_dispatch_register(dispatch, "inset",                cmd_inset);
    edit_dispatch_register(dispatch, "bevel",                cmd_bevel);
    edit_dispatch_register(dispatch, "mesh_select",          cmd_mesh_select);
    edit_dispatch_register(dispatch, "mesh_deselect",        cmd_mesh_deselect);
    edit_dispatch_register(dispatch, "mesh_select_all",      cmd_mesh_select_all);
    edit_dispatch_register(dispatch, "mesh_deselect_all",    cmd_mesh_deselect_all);
    edit_dispatch_register(dispatch, "mesh_commit",          cmd_mesh_commit);
    edit_dispatch_register(dispatch, "mesh_info",            cmd_mesh_info);
}
