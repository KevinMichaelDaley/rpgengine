/**
 * @file mesh_extrude_individual.c
 * @brief Per-face independent extrusion — each face gets its own pillar.
 *
 * Non-static functions (1 of 4): mesh_extrude_individual.
 */
#include "ferrum/editor/mesh/mesh_extrude.h"

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

/** Duplicate a vertex with offset, copying all attributes. */
static uint32_t dup_vertex_offset_(mesh_slot_t *slot, uint32_t v,
                                    const float offset[3]) {
    float pos[3] = {
        slot->positions[v*3+0] + offset[0],
        slot->positions[v*3+1] + offset[1],
        slot->positions[v*3+2] + offset[2]
    };
    float nrm[3] = {
        slot->normals[v*3+0],
        slot->normals[v*3+1],
        slot->normals[v*3+2]
    };
    uint32_t nv = mesh_slot_add_vertex(slot, pos, nrm);
    if (nv == UINT32_MAX) return UINT32_MAX;

    /* Copy UV channels */
    for (int ch = 0; ch < MESH_SLOT_UV_CHANNELS; ch++) {
        if (slot->uvs[ch]) {
            slot->uvs[ch][nv*2+0] = slot->uvs[ch][v*2+0];
            slot->uvs[ch][nv*2+1] = slot->uvs[ch][v*2+1];
        }
    }
    if (slot->colors) {
        memcpy(&slot->colors[nv*4], &slot->colors[v*4], 4*sizeof(float));
    }
    if (slot->tangents) {
        memcpy(&slot->tangents[nv*4], &slot->tangents[v*4], 4*sizeof(float));
    }
    return nv;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

bool mesh_extrude_individual(mesh_slot_t *slot, mesh_sel_bitset_t *sel,
                             float distance) {
    if (!slot || !sel) return false;

    uint32_t face_count = slot->index_count / 3;

    /* Collect selected face indices */
    uint32_t sel_count = 0;
    uint32_t *sel_faces = malloc(face_count * sizeof(uint32_t));
    if (!sel_faces) return false;

    for (uint32_t f = 0; f < face_count; f++) {
        if (mesh_sel_bitset_test(sel, f)) {
            sel_faces[sel_count++] = f;
        }
    }
    if (sel_count == 0) { free(sel_faces); return false; }

    /* Reserve space: each face → 3 new verts + 3 side quads (6 tris) */
    mesh_slot_reserve_vertices(slot, slot->vertex_count + sel_count * 3);
    mesh_slot_reserve_indices(slot, slot->index_count + sel_count * 6 * 3);

    /* Extrude each face independently */
    for (uint32_t i = 0; i < sel_count; i++) {
        uint32_t fi = sel_faces[i];
        uint32_t *tri = &slot->indices[fi * 3];

        /* Get original vertices before modification */
        uint32_t orig[3] = { tri[0], tri[1], tri[2] };

        /* Compute face normal */
        float n[3];
        face_normal_(slot, fi, n);
        float offset[3] = { n[0]*distance, n[1]*distance, n[2]*distance };

        /* Duplicate each vertex with offset */
        uint32_t dup[3];
        bool ok = true;
        for (int j = 0; j < 3; j++) {
            dup[j] = dup_vertex_offset_(slot, orig[j], offset);
            if (dup[j] == UINT32_MAX) { ok = false; break; }
        }
        if (!ok) { free(sel_faces); return false; }

        /* Replace face with offset vertices */
        tri[0] = dup[0]; tri[1] = dup[1]; tri[2] = dup[2];

        /* Create 3 side wall quads (2 tris each) */
        for (int j = 0; j < 3; j++) {
            uint32_t v0 = orig[j];
            uint32_t v1 = orig[(j+1) % 3];
            uint32_t d0 = dup[j];
            uint32_t d1 = dup[(j+1) % 3];
            mesh_slot_add_triangle(slot, v0, v1, d1, 0);
            mesh_slot_add_triangle(slot, v0, d1, d0, 0);
        }
    }

    /* Update selection: original face indices still point to extruded faces */
    /* (we modified them in-place, so they're already correct) */

    free(sel_faces);
    return true;
}
