/**
 * @file mesh_edit_slot.c
 * @brief mesh_edit_t slot access — set_active_slot, get_active_slot.
 */
#include "ferrum/editor/mesh/mesh_edit.h"

#include <stddef.h>

/* ------------------------------------------------------------------------ */
/* Public API                                                                */
/* ------------------------------------------------------------------------ */

bool mesh_edit_set_active_slot(mesh_edit_t *edit, uint32_t idx) {
    if (!edit) { return false; }
    if (idx >= MESH_MAX_EDITABLE) { return false; }
    edit->active_slot = idx;
    return true;
}

mesh_slot_t *mesh_edit_get_active_slot(mesh_edit_t *edit) {
    if (!edit) { return NULL; }
    return &edit->slots[edit->active_slot];
}
