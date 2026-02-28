/**
 * @file mesh_collapse.c
 * @brief Edge collapse — replace an edge with its midpoint vertex.
 *
 * Non-static functions (1 of 4): mesh_collapse_edge.
 */
#include "ferrum/editor/mesh/mesh_merge.h"

#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* Static: remove degenerate triangles                                 */
/* ------------------------------------------------------------------ */

static void remove_degenerates_(mesh_slot_t *slot) {
    uint32_t write = 0;
    uint32_t face_count = slot->index_count / 3;

    for (uint32_t f = 0; f < face_count; f++) {
        uint32_t a = slot->indices[f*3+0];
        uint32_t b = slot->indices[f*3+1];
        uint32_t c = slot->indices[f*3+2];

        if (a == b || b == c || a == c) continue;

        if (write != f) {
            slot->indices[write*3+0] = a;
            slot->indices[write*3+1] = b;
            slot->indices[write*3+2] = c;
            if (slot->polygroup_ids) {
                slot->polygroup_ids[write] = slot->polygroup_ids[f];
            }
        }
        write++;
    }
    slot->index_count = write * 3;
}

/* ------------------------------------------------------------------ */
/* Public: mesh_collapse_edge                                          */
/* ------------------------------------------------------------------ */

bool mesh_collapse_edge(mesh_slot_t *slot, uint32_t v0, uint32_t v1) {
    if (!slot) return false;
    if (v0 >= slot->vertex_count || v1 >= slot->vertex_count) return false;
    if (v0 == v1) return false;

    /* Move v0 to midpoint of (v0, v1) */
    for (int k = 0; k < 3; k++) {
        slot->positions[v0*3+k] =
            (slot->positions[v0*3+k] + slot->positions[v1*3+k]) * 0.5f;
        slot->normals[v0*3+k] =
            (slot->normals[v0*3+k] + slot->normals[v1*3+k]) * 0.5f;
    }

    /* Remap all v1 → v0 in indices */
    for (uint32_t i = 0; i < slot->index_count; i++) {
        if (slot->indices[i] == v1) {
            slot->indices[i] = v0;
        }
    }

    remove_degenerates_(slot);
    return true;
}
