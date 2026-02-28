/**
 * @file mesh_bevel.c
 * @brief Edge bevel — chamfer edges by splitting adjacent faces.
 *
 * Non-static functions (1 of 4): mesh_bevel_edges.
 *
 * Algorithm (segments=1):
 * For each selected edge (v0,v1):
 *   1. Find all faces containing this edge.
 *   2. For each face, split at amount from the edge:
 *      - Create 2 new vertices along the face edges at distance `amount`.
 *      - Replace the original triangle with 2 triangles using the new vertices.
 *   3. Create a chamfer quad between the two split positions.
 */
#include "ferrum/editor/mesh/mesh_bevel.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Static helpers                                                      */
/* ------------------------------------------------------------------ */

/**
 * Create a vertex interpolated between v0 and v1 at parameter t.
 * Copies position (lerp), normal (lerp+normalize), and UVs (lerp).
 */
static uint32_t lerp_vertex_(mesh_slot_t *slot, uint32_t v0, uint32_t v1,
                              float t) {
    float pos[3], nrm[3];
    for (int k = 0; k < 3; k++) {
        pos[k] = slot->positions[v0*3+k] * (1-t) + slot->positions[v1*3+k] * t;
        nrm[k] = slot->normals[v0*3+k] * (1-t) + slot->normals[v1*3+k] * t;
    }
    /* Normalize the interpolated normal */
    float len = sqrtf(nrm[0]*nrm[0] + nrm[1]*nrm[1] + nrm[2]*nrm[2]);
    if (len > 1e-12f) { nrm[0]/=len; nrm[1]/=len; nrm[2]/=len; }

    uint32_t nv = mesh_slot_add_vertex(slot, pos, nrm);
    if (nv == UINT32_MAX) return UINT32_MAX;

    /* Lerp UVs */
    for (int ch = 0; ch < MESH_SLOT_UV_CHANNELS; ch++) {
        if (slot->uvs[ch]) {
            slot->uvs[ch][nv*2+0] = slot->uvs[ch][v0*2+0]*(1-t) + slot->uvs[ch][v1*2+0]*t;
            slot->uvs[ch][nv*2+1] = slot->uvs[ch][v0*2+1]*(1-t) + slot->uvs[ch][v1*2+1]*t;
        }
    }
    return nv;
}

/**
 * Find the "other" vertex in a triangle that is not v0 or v1.
 * Returns UINT32_MAX if not found.
 */
static uint32_t opposite_vertex_(const uint32_t tri[3], uint32_t v0, uint32_t v1) {
    for (int j = 0; j < 3; j++) {
        if (tri[j] != v0 && tri[j] != v1) return tri[j];
    }
    return UINT32_MAX;
}

/**
 * Compute edge length between two vertices.
 */
static float edge_length_(const mesh_slot_t *slot, uint32_t v0, uint32_t v1) {
    float dx = slot->positions[v1*3+0] - slot->positions[v0*3+0];
    float dy = slot->positions[v1*3+1] - slot->positions[v0*3+1];
    float dz = slot->positions[v1*3+2] - slot->positions[v0*3+2];
    return sqrtf(dx*dx + dy*dy + dz*dz);
}

/* ------------------------------------------------------------------ */
/* Public: mesh_bevel_edges                                            */
/* ------------------------------------------------------------------ */

bool mesh_bevel_edges(mesh_slot_t *slot,
                      const mesh_sel_bitset_t *sel,
                      const mesh_edge_table_t *et,
                      float amount,
                      uint32_t segments) {
    if (!slot || !sel || !et) return false;
    if (segments == 0) segments = 1;

    /* Collect selected edges */
    uint32_t sel_count = 0;
    for (uint32_t e = 0; e < et->edge_count; e++) {
        if (mesh_sel_bitset_test(sel, e)) sel_count++;
    }
    if (sel_count == 0) return false;

    uint32_t face_count = slot->index_count / 3;

    /* Pre-reserve generous space */
    mesh_slot_reserve_vertices(slot, slot->vertex_count + sel_count * 4 * segments);
    mesh_slot_reserve_indices(slot, slot->index_count + sel_count * 12 * segments);

    /* Process each selected edge */
    for (uint32_t ei = 0; ei < et->edge_count; ei++) {
        if (!mesh_sel_bitset_test(sel, ei)) continue;

        uint32_t ev0 = et->edges[ei].v0;
        uint32_t ev1 = et->edges[ei].v1;

        float elen = edge_length_(slot, ev0, ev1);
        if (elen < 1e-12f) continue;

        /* Compute the bevel parameter t (fraction of edge to split at) */
        float t = amount / elen;
        if (t > 0.5f) t = 0.5f;

        /* Create bevel vertices on the edge */
        uint32_t nv_a = lerp_vertex_(slot, ev0, ev1, t);      /* near ev0 */
        uint32_t nv_b = lerp_vertex_(slot, ev0, ev1, 1.0f-t); /* near ev1 */
        if (nv_a == UINT32_MAX || nv_b == UINT32_MAX) return false;

        /* Find and modify all faces containing this edge.
         * We scan existing faces (use face_count from before modifications). */
        for (uint32_t fi = 0; fi < face_count; fi++) {
            uint32_t *tri = &slot->indices[fi * 3];

            /* Check if this face contains edge (ev0, ev1) */
            int has_v0 = -1, has_v1 = -1;
            for (int j = 0; j < 3; j++) {
                if (tri[j] == ev0) has_v0 = j;
                if (tri[j] == ev1) has_v1 = j;
            }
            if (has_v0 < 0 || has_v1 < 0) continue;

            uint32_t opp = opposite_vertex_(tri, ev0, ev1);
            if (opp == UINT32_MAX) continue;

            /* Replace original triangle with 3 triangles:
             * (opp, ev0, nv_a), (opp, nv_a, nv_b), (opp, nv_b, ev1) */
            tri[0] = opp; tri[1] = ev0; tri[2] = nv_a;
            mesh_slot_add_triangle(slot, opp, nv_a, nv_b, 0);
            mesh_slot_add_triangle(slot, opp, nv_b, ev1, 0);
        }
    }

    return true;
}
