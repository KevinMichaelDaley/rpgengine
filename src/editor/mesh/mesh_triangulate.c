/**
 * @file mesh_triangulate.c
 * @brief Triangulate and tris-to-quads (polygroup tagging).
 *
 * Non-static functions (2 of 4): mesh_triangulate, mesh_tris_to_quads.
 */
#include "ferrum/editor/mesh/mesh_triangulate.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Public: mesh_triangulate                                            */
/* ------------------------------------------------------------------ */

bool mesh_triangulate(mesh_slot_t *slot, const mesh_sel_bitset_t *sel) {
    /* All faces are already triangles. No-op. */
    (void)slot;
    (void)sel;
    return true;
}

/* ------------------------------------------------------------------ */
/* Static helper: face normal                                          */
/* ------------------------------------------------------------------ */

static void face_normal_(const mesh_slot_t *slot, uint32_t fi, float out[3]) {
    const uint32_t *tri = &slot->indices[fi * 3];
    const float *a = &slot->positions[tri[0]*3];
    const float *b = &slot->positions[tri[1]*3];
    const float *c = &slot->positions[tri[2]*3];

    float ab[3] = { b[0]-a[0], b[1]-a[1], b[2]-a[2] };
    float ac[3] = { c[0]-a[0], c[1]-a[1], c[2]-a[2] };

    out[0] = ab[1]*ac[2] - ab[2]*ac[1];
    out[1] = ab[2]*ac[0] - ab[0]*ac[2];
    out[2] = ab[0]*ac[1] - ab[1]*ac[0];

    float len = sqrtf(out[0]*out[0]+out[1]*out[1]+out[2]*out[2]);
    if (len > 1e-12f) { out[0]/=len; out[1]/=len; out[2]/=len; }
}

/** Check if two faces share an edge. */
static bool share_edge_(const uint32_t *t0, const uint32_t *t1) {
    int shared = 0;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            if (t0[i] == t1[j]) shared++;
        }
    }
    return shared >= 2;
}

/* ------------------------------------------------------------------ */
/* Public: mesh_tris_to_quads                                          */
/* ------------------------------------------------------------------ */

uint32_t mesh_tris_to_quads(mesh_slot_t *slot, float threshold) {
    if (!slot || !slot->polygroup_ids) return 0;

    uint32_t fc = slot->index_count / 3;
    if (fc < 2) return 0;

    /* Track which faces are already paired */
    bool *paired = calloc(fc, sizeof(bool));
    if (!paired) return 0;

    uint32_t pair_count = 0;
    uint16_t next_pg = 1;

    /* Find the max polygroup currently in use */
    for (uint32_t f = 0; f < fc; f++) {
        if (slot->polygroup_ids[f] >= next_pg) {
            next_pg = slot->polygroup_ids[f] + 1;
        }
    }

    /* O(F²) brute force pairing — ok for editor meshes */
    for (uint32_t i = 0; i < fc && i + 1 < fc; i++) {
        if (paired[i]) continue;

        float ni[3];
        face_normal_(slot, i, ni);

        for (uint32_t j = i + 1; j < fc; j++) {
            if (paired[j]) continue;
            if (!share_edge_(&slot->indices[i*3], &slot->indices[j*3])) continue;

            float nj[3];
            face_normal_(slot, j, nj);

            float dot = ni[0]*nj[0] + ni[1]*nj[1] + ni[2]*nj[2];
            if (dot >= threshold) {
                /* Pair them under same polygroup */
                slot->polygroup_ids[i] = next_pg;
                slot->polygroup_ids[j] = next_pg;
                next_pg++;
                paired[i] = true;
                paired[j] = true;
                pair_count++;
                break;
            }
        }
    }

    free(paired);
    return pair_count;
}
