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
}
