/**
 * @file mesh_outset.c
 * @brief Face outset — scale vertices outward from centroid.
 *
 * Non-static functions (1 of 4): mesh_outset.
 */
#include "ferrum/editor/mesh/mesh_inset.h"

#include <math.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* Public: mesh_outset                                                 */
/* ------------------------------------------------------------------ */

bool mesh_outset(mesh_slot_t *slot, mesh_sel_bitset_t *sel,
                 float amount) {
    if (!slot || !sel) return false;

    uint32_t face_count = slot->index_count / 3;

    /* Collect selected faces */
    uint32_t sel_count = 0;
    uint32_t *sel_faces = malloc(face_count * sizeof(uint32_t));
    if (!sel_faces) return false;

    for (uint32_t f = 0; f < face_count; f++) {
        if (mesh_sel_bitset_test(sel, f)) {
            sel_faces[sel_count++] = f;
        }
    }
    if (sel_count == 0) { free(sel_faces); return false; }

    /* For each selected face, scale vertices outward from centroid */
    for (uint32_t i = 0; i < sel_count; i++) {
        uint32_t fi = sel_faces[i];
        const uint32_t *tri = &slot->indices[fi * 3];

        /* Compute centroid */
        float cx = 0, cy = 0, cz = 0;
        for (int j = 0; j < 3; j++) {
            cx += slot->positions[tri[j]*3+0];
            cy += slot->positions[tri[j]*3+1];
            cz += slot->positions[tri[j]*3+2];
        }
        cx /= 3; cy /= 3; cz /= 3;

        /* Move each vertex outward from centroid */
        for (int j = 0; j < 3; j++) {
            uint32_t vi = tri[j];
            float dx = slot->positions[vi*3+0] - cx;
            float dy = slot->positions[vi*3+1] - cy;
            float dz = slot->positions[vi*3+2] - cz;

            float len = sqrtf(dx*dx + dy*dy + dz*dz);
            if (len > 1e-12f) {
                slot->positions[vi*3+0] += (dx / len) * amount;
                slot->positions[vi*3+1] += (dy / len) * amount;
                slot->positions[vi*3+2] += (dz / len) * amount;
            }
        }
    }

    free(sel_faces);
    return true;
}
