/**
 * @file mesh_select_grow.c
 * @brief Grow/shrink face selection by adjacency rings.
 *
 * Non-static functions: mesh_select_grow, mesh_select_shrink (2 of 4).
 */
#include "ferrum/editor/mesh/mesh_select.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Internal: check if two faces share an edge                          */
/* ------------------------------------------------------------------ */

static bool faces_adjacent_(const mesh_slot_t *slot, uint32_t fa, uint32_t fb) {
    const uint32_t *ia = &slot->indices[fa * 3];
    const uint32_t *ib = &slot->indices[fb * 3];
    int shared = 0;
    for (int a = 0; a < 3; a++) {
        for (int b = 0; b < 3; b++) {
            if (ia[a] == ib[b]) { shared++; }
        }
    }
    return shared >= 2;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void mesh_select_grow(const mesh_slot_t *slot, mesh_sel_bitset_t *sel,
                      uint32_t steps) {
    if (!slot || !sel || steps == 0) { return; }

    uint32_t fc = mesh_slot_face_count(slot);
    if (fc == 0) { return; }

    for (uint32_t step = 0; step < steps; step++) {
        /* Collect currently selected faces */
        uint32_t *selected = malloc((size_t)fc * sizeof(uint32_t));
        if (!selected) { return; }
        uint32_t sel_count = 0;
        for (uint32_t f = 0; f < fc; f++) {
            if (mesh_sel_bitset_test(sel, f)) {
                selected[sel_count++] = f;
            }
        }

        /* Add neighbors of selected faces */
        for (uint32_t i = 0; i < sel_count; i++) {
            for (uint32_t other = 0; other < fc; other++) {
                if (mesh_sel_bitset_test(sel, other)) { continue; }
                if (faces_adjacent_(slot, selected[i], other)) {
                    mesh_sel_bitset_set(sel, other);
                }
            }
        }

        free(selected);
    }
}

void mesh_select_shrink(const mesh_slot_t *slot, mesh_sel_bitset_t *sel,
                        uint32_t steps) {
    if (!slot || !sel || steps == 0) { return; }

    uint32_t fc = mesh_slot_face_count(slot);
    if (fc == 0) { return; }

    for (uint32_t step = 0; step < steps; step++) {
        /* Find faces to deselect: any selected face with a non-selected neighbor */
        uint32_t *to_deselect = malloc((size_t)fc * sizeof(uint32_t));
        if (!to_deselect) { return; }
        uint32_t desel_count = 0;

        for (uint32_t f = 0; f < fc; f++) {
            if (!mesh_sel_bitset_test(sel, f)) { continue; }

            /* Check if any adjacent face is NOT selected */
            bool has_unselected_neighbor = false;
            for (uint32_t other = 0; other < fc; other++) {
                if (other == f) { continue; }
                if (faces_adjacent_(slot, f, other) &&
                    !mesh_sel_bitset_test(sel, other)) {
                    has_unselected_neighbor = true;
                    break;
                }
            }

            /* Also deselect if face is on mesh boundary (fewer than 3 adjacents) */
            if (!has_unselected_neighbor) {
                int adj_count = 0;
                for (uint32_t other = 0; other < fc; other++) {
                    if (other != f && faces_adjacent_(slot, f, other)) {
                        adj_count++;
                    }
                }
                if (adj_count < 3) {
                    has_unselected_neighbor = true;
                }
            }

            if (has_unselected_neighbor) {
                to_deselect[desel_count++] = f;
            }
        }

        /* Apply deselection */
        for (uint32_t i = 0; i < desel_count; i++) {
            mesh_sel_bitset_unset(sel, to_deselect[i]);
        }

        free(to_deselect);
    }
}
