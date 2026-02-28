/**
 * @file mesh_edit.c
 * @brief mesh_edit_t lifecycle — init, destroy.
 */
#include "ferrum/editor/mesh/mesh_edit.h"

#include <string.h>

/* ------------------------------------------------------------------------ */
/* Public API                                                                */
/* ------------------------------------------------------------------------ */

bool mesh_edit_init(mesh_edit_t *edit) {
    if (!edit) { return false; }
    memset(edit, 0, sizeof(*edit));

    /* Initialize all mesh slots with zero capacity (empty but valid) */
    for (int i = 0; i < MESH_MAX_EDITABLE; i++) {
        if (!mesh_slot_init(&edit->slots[i], 0, 0)) {
            /* Roll back already-initialized slots */
            for (int j = 0; j < i; j++) {
                mesh_slot_destroy(&edit->slots[j]);
            }
            return false;
        }
    }

    edit->active_slot = 0;
    edit->mode        = MESH_SEL_MODE_FACE;

    mesh_sel_bitset_init(&edit->sel_vertices);
    mesh_sel_bitset_init(&edit->sel_edges);
    mesh_sel_bitset_init(&edit->sel_faces);

    return true;
}

void mesh_edit_destroy(mesh_edit_t *edit) {
    if (!edit) { return; }

    for (int i = 0; i < MESH_MAX_EDITABLE; i++) {
        mesh_slot_destroy(&edit->slots[i]);
    }

    mesh_sel_bitset_destroy(&edit->sel_vertices);
    mesh_sel_bitset_destroy(&edit->sel_edges);
    mesh_sel_bitset_destroy(&edit->sel_faces);

    memset(edit, 0, sizeof(*edit));
}
