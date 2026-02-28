/**
 * @file mesh_edit_mode.c
 * @brief mesh_edit_t mode switching — set_mode.
 */
#include "ferrum/editor/mesh/mesh_edit.h"

/* ------------------------------------------------------------------------ */
/* Public API                                                                */
/* ------------------------------------------------------------------------ */

bool mesh_edit_set_mode(mesh_edit_t *edit, mesh_sel_mode_t mode) {
    if (!edit) { return false; }
    if (mode >= MESH_SEL_MODE_COUNT) { return false; }

    /* Clear all selections on mode switch */
    mesh_sel_bitset_clear_all(&edit->sel_vertices);
    mesh_sel_bitset_clear_all(&edit->sel_edges);
    mesh_sel_bitset_clear_all(&edit->sel_faces);

    edit->mode = mode;
    return true;
}
