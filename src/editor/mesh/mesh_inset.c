/**
 * @file mesh_inset.c
 * @brief Face inset — shrink toward centroid, create border ring.
 *
 * Non-static functions (1 of 4): mesh_inset.
 */
#include "ferrum/editor/mesh/mesh_inset.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Static helpers                                                      */
/* ------------------------------------------------------------------ */

/** Compute face normal for triangle at face_idx. */
static void face_normal_(const mesh_slot_t *slot, uint32_t face_idx,
                          float out[3]) {
    const uint32_t *tri = &slot->indices[face_idx * 3];
    const float *a = &slot->positions[tri[0] * 3];
    const float *b = &slot->positions[tri[1] * 3];
    const float *c = &slot->positions[tri[2] * 3];

    float ab[3] = { b[0]-a[0], b[1]-a[1], b[2]-a[2] };
    float ac[3] = { c[0]-a[0], c[1]-a[1], c[2]-a[2] };

    out[0] = ab[1]*ac[2] - ab[2]*ac[1];
    out[1] = ab[2]*ac[0] - ab[0]*ac[2];
    out[2] = ab[0]*ac[1] - ab[1]*ac[0];

    float len = sqrtf(out[0]*out[0] + out[1]*out[1] + out[2]*out[2]);
    if (len > 1e-12f) {
        out[0] /= len; out[1] /= len; out[2] /= len;
    }
}

/* ------------------------------------------------------------------ */
/* Public: mesh_inset                                                  */
/* ------------------------------------------------------------------ */

bool mesh_inset(mesh_slot_t *slot, mesh_sel_bitset_t *sel,
                float amount, float depth) {
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

    /* Reserve space: each tri → 3 new inset verts + 3 border quads (6 tris) */
    mesh_slot_reserve_vertices(slot, slot->vertex_count + sel_count * 3);
    mesh_slot_reserve_indices(slot, slot->index_count + sel_count * 6 * 3);

    /* Process each selected face */
    for (uint32_t i = 0; i < sel_count; i++) {
        uint32_t fi = sel_faces[i];
        uint32_t *tri = &slot->indices[fi * 3];
        uint32_t orig[3] = { tri[0], tri[1], tri[2] };

        /* Compute face centroid */
        float cx = 0, cy = 0, cz = 0;
        for (int j = 0; j < 3; j++) {
            cx += slot->positions[orig[j]*3+0];
            cy += slot->positions[orig[j]*3+1];
            cz += slot->positions[orig[j]*3+2];
        }
        cx /= 3; cy /= 3; cz /= 3;

        /* Compute face normal for depth offset */
        float n[3] = {0, 0, 0};
        if (depth != 0.0f) {
            face_normal_(slot, fi, n);
        }

        /* Create 3 inset vertices (lerp toward centroid + depth) */
        uint32_t inset[3];
        for (int j = 0; j < 3; j++) {
            float ox = slot->positions[orig[j]*3+0];
            float oy = slot->positions[orig[j]*3+1];
            float oz = slot->positions[orig[j]*3+2];

            float pos[3] = {
                ox + (cx - ox) * amount + n[0] * depth,
                oy + (cy - oy) * amount + n[1] * depth,
                oz + (cz - oz) * amount + n[2] * depth
            };
            float nrm[3] = {
                slot->normals[orig[j]*3+0],
                slot->normals[orig[j]*3+1],
                slot->normals[orig[j]*3+2]
            };

            inset[j] = mesh_slot_add_vertex(slot, pos, nrm);
            if (inset[j] == UINT32_MAX) { free(sel_faces); return false; }

            /* Copy UVs, colors */
            for (int ch = 0; ch < MESH_SLOT_UV_CHANNELS; ch++) {
                if (slot->uvs[ch]) {
                    slot->uvs[ch][inset[j]*2+0] = slot->uvs[ch][orig[j]*2+0];
                    slot->uvs[ch][inset[j]*2+1] = slot->uvs[ch][orig[j]*2+1];
                }
            }
        }

        /* Replace original face with inset face */
        tri[0] = inset[0]; tri[1] = inset[1]; tri[2] = inset[2];

        /* Create 3 border quads (2 tris each) */
        for (int j = 0; j < 3; j++) {
            uint32_t v0 = orig[j];
            uint32_t v1 = orig[(j+1) % 3];
            uint32_t i0 = inset[j];
            uint32_t i1 = inset[(j+1) % 3];

            mesh_slot_add_triangle(slot, v0, v1, i1, 0);
            mesh_slot_add_triangle(slot, v0, i1, i0, 0);
        }
    }

    free(sel_faces);
    return true;
}
