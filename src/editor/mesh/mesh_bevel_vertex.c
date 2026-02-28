/**
 * @file mesh_bevel_vertex.c
 * @brief Vertex bevel — replace vertex with polygon at incident edges.
 *
 * Non-static functions (1 of 4): mesh_bevel_vertices.
 *
 * Algorithm:
 * For each selected vertex V:
 *   1. Find all faces containing V.
 *   2. For each incident edge (V, Vn), create a new vertex at lerp(V, Vn, t).
 *   3. Replace V in each face with the new vertex corresponding to that face.
 *   4. Create a polygon (fan of triangles) connecting the new vertices.
 */
#include "ferrum/editor/mesh/mesh_bevel.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Static helpers                                                      */
/* ------------------------------------------------------------------ */

/** Create a lerped vertex between v0 and v1 at parameter t. */
static uint32_t lerp_vert_(mesh_slot_t *slot, uint32_t v0, uint32_t v1,
                            float t) {
    float pos[3], nrm[3];
    for (int k = 0; k < 3; k++) {
        pos[k] = slot->positions[v0*3+k]*(1-t) + slot->positions[v1*3+k]*t;
        nrm[k] = slot->normals[v0*3+k]*(1-t) + slot->normals[v1*3+k]*t;
    }
    float len = sqrtf(nrm[0]*nrm[0] + nrm[1]*nrm[1] + nrm[2]*nrm[2]);
    if (len > 1e-12f) { nrm[0]/=len; nrm[1]/=len; nrm[2]/=len; }

    uint32_t nv = mesh_slot_add_vertex(slot, pos, nrm);
    if (nv == UINT32_MAX) return UINT32_MAX;

    for (int ch = 0; ch < MESH_SLOT_UV_CHANNELS; ch++) {
        if (slot->uvs[ch]) {
            slot->uvs[ch][nv*2+0] = slot->uvs[ch][v0*2+0]*(1-t) + slot->uvs[ch][v1*2+0]*t;
            slot->uvs[ch][nv*2+1] = slot->uvs[ch][v0*2+1]*(1-t) + slot->uvs[ch][v1*2+1]*t;
        }
    }
    return nv;
}

/* ------------------------------------------------------------------ */
/* Public: mesh_bevel_vertices                                         */
/* ------------------------------------------------------------------ */

bool mesh_bevel_vertices(mesh_slot_t *slot,
                         const mesh_sel_bitset_t *sel,
                         float amount) {
    if (!slot || !sel) return false;

    /* Collect selected vertices */
    uint32_t sel_count = 0;
    for (uint32_t v = 0; v < slot->vertex_count; v++) {
        if (mesh_sel_bitset_test(sel, v)) sel_count++;
    }
    if (sel_count == 0) return false;

    uint32_t face_count = slot->index_count / 3;

    /* Reserve generous space */
    mesh_slot_reserve_vertices(slot, slot->vertex_count + sel_count * 8);
    mesh_slot_reserve_indices(slot, slot->index_count + sel_count * 8 * 3);

    /* Process each selected vertex */
    for (uint32_t vi = 0; vi < slot->vertex_count; vi++) {
        if (!mesh_sel_bitset_test(sel, vi)) continue;

        /* Find all faces containing this vertex and collect neighbors */
        /* Max incident faces we track (reasonable for editor meshes) */
        uint32_t new_verts[64];
        uint32_t face_indices[64];
        uint32_t incident = 0;

        for (uint32_t fi = 0; fi < face_count && incident < 64; fi++) {
            uint32_t *tri = &slot->indices[fi * 3];
            int vi_pos = -1;
            for (int j = 0; j < 3; j++) {
                if (tri[j] == vi) { vi_pos = j; break; }
            }
            if (vi_pos < 0) continue;

            /* Get the two other vertices of this face */
            uint32_t vn1 = tri[(vi_pos + 1) % 3];
            uint32_t vn2 = tri[(vi_pos + 2) % 3];

            /* We need one new vertex per incident face.
             * Position it along the edge toward vn1 from vi. */
            float elen = 0;
            for (int k = 0; k < 3; k++) {
                float d = slot->positions[vn1*3+k] - slot->positions[vi*3+k];
                elen += d*d;
            }
            elen = sqrtf(elen);
            float t = (elen > 1e-12f) ? (amount / elen) : 0;
            if (t > 0.5f) t = 0.5f;

            uint32_t nv = lerp_vert_(slot, vi, vn1, t);
            if (nv == UINT32_MAX) return false;

            new_verts[incident] = nv;
            face_indices[incident] = fi;
            incident++;

            (void)vn2; /* used implicitly through tri replacement below */
        }
        if (incident < 2) continue;

        /* Replace vi in each incident face with the new vertex */
        for (uint32_t k = 0; k < incident; k++) {
            uint32_t fi = face_indices[k];
            uint32_t *tri = &slot->indices[fi * 3];
            for (int j = 0; j < 3; j++) {
                if (tri[j] == vi) { tri[j] = new_verts[k]; break; }
            }
        }

        /* Create bevel polygon: fan from new_verts[0] */
        for (uint32_t k = 1; k + 1 < incident; k++) {
            mesh_slot_add_triangle(slot, new_verts[0], new_verts[k],
                                   new_verts[k+1], 0);
        }
    }

    return true;
}
