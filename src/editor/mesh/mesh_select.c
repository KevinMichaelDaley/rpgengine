/**
 * @file mesh_select.c
 * @brief Basic mesh selection: by indices, all, invert, deselect.
 *
 * Non-static functions: select_by_indices, deselect_by_indices,
 * select_all, select_invert (4 of 4).
 */
#include "ferrum/editor/mesh/mesh_select.h"

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void mesh_select_by_indices(mesh_sel_bitset_t *sel,
                            const uint32_t *indices, uint32_t count) {
    if (!sel || !indices) { return; }
    for (uint32_t i = 0; i < count; i++) {
        mesh_sel_bitset_set(sel, indices[i]);
    }
}

void mesh_deselect_by_indices(mesh_sel_bitset_t *sel,
                              const uint32_t *indices, uint32_t count) {
    if (!sel || !indices) { return; }
    for (uint32_t i = 0; i < count; i++) {
        mesh_sel_bitset_unset(sel, indices[i]);
    }
}

void mesh_select_all(mesh_sel_bitset_t *sel, uint32_t total) {
    if (!sel) { return; }
    for (uint32_t i = 0; i < total; i++) {
        mesh_sel_bitset_set(sel, i);
    }
}

void mesh_select_invert(mesh_sel_bitset_t *sel, uint32_t total) {
    if (!sel) { return; }
    for (uint32_t i = 0; i < total; i++) {
        mesh_sel_bitset_toggle(sel, i);
    }
}
