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
}
