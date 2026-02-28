/**
 * @file mesh_uv_transform.c
 * @brief UV shift, rotate, scale for selected faces.
 *
 * Non-static functions (3 of 4): mesh_uv_shift, mesh_uv_rotate, mesh_uv_scale.
 */
#include "ferrum/editor/mesh/mesh_uv_transform.h"

#include <math.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Static: collect unique vertices from selected faces                 */
/* ------------------------------------------------------------------ */

/** Mark which vertices are touched by selected faces. */
static uint32_t collect_verts_(const mesh_slot_t *slot,
                               const mesh_sel_bitset_t *sel,
                               uint8_t *touched, uint32_t max_v) {
    uint32_t fc = slot->index_count / 3;
    uint32_t count = 0;
    for (uint32_t f = 0; f < fc; f++) {
        if (!mesh_sel_bitset_test(sel, f)) continue;
        for (int j = 0; j < 3; j++) {
            uint32_t vi = slot->indices[f*3+j];
            if (vi < max_v && !touched[vi]) {
                touched[vi] = 1;
                count++;
            }
        }
    }
    return count;
}

/** Compute UV centroid of touched vertices. */
static void uv_centroid_(const mesh_slot_t *slot, const uint8_t *touched,
                         int channel, float *cu, float *cv) {
    float su = 0, sv = 0;
    uint32_t n = 0;
    for (uint32_t v = 0; v < slot->vertex_count; v++) {
        if (!touched[v]) continue;
        su += slot->uvs[channel][v*2+0];
        sv += slot->uvs[channel][v*2+1];
        n++;
    }
    if (n > 0) { *cu = su / (float)n; *cv = sv / (float)n; }
    else       { *cu = 0.5f; *cv = 0.5f; }
}

/* ------------------------------------------------------------------ */
/* mesh_uv_shift                                                       */
/* ------------------------------------------------------------------ */

bool mesh_uv_shift(mesh_slot_t *slot, const mesh_sel_bitset_t *sel,
                   float du, float dv, int channel) {
    if (!slot || !sel || channel < 0 || channel >= MESH_SLOT_UV_CHANNELS) return false;
    if (!slot->uvs[channel]) return false;

    uint32_t vc = slot->vertex_count;
    uint8_t touched[MESH_SLOT_MAX_VERTICES];
    memset(touched, 0, vc);
    collect_verts_(slot, sel, touched, vc);

    for (uint32_t v = 0; v < vc; v++) {
        if (!touched[v]) continue;
        slot->uvs[channel][v*2+0] += du;
        slot->uvs[channel][v*2+1] += dv;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* mesh_uv_rotate                                                      */
/* ------------------------------------------------------------------ */

bool mesh_uv_rotate(mesh_slot_t *slot, const mesh_sel_bitset_t *sel,
                    float angle, float pivot_u, float pivot_v, int channel) {
    if (!slot || !sel || channel < 0 || channel >= MESH_SLOT_UV_CHANNELS) return false;
    if (!slot->uvs[channel]) return false;

    uint32_t vc = slot->vertex_count;
    uint8_t touched[MESH_SLOT_MAX_VERTICES];
    memset(touched, 0, vc);
    collect_verts_(slot, sel, touched, vc);

    /* Auto-pivot */
    float cu, cv;
    if (pivot_u < 0 || pivot_v < 0) {
        uv_centroid_(slot, touched, channel, &cu, &cv);
    } else {
        cu = pivot_u; cv = pivot_v;
    }

    float cos_a = cosf(angle);
    float sin_a = sinf(angle);

    for (uint32_t v = 0; v < vc; v++) {
        if (!touched[v]) continue;
        float du = slot->uvs[channel][v*2+0] - cu;
        float dv = slot->uvs[channel][v*2+1] - cv;
        slot->uvs[channel][v*2+0] = cu + du * cos_a - dv * sin_a;
        slot->uvs[channel][v*2+1] = cv + du * sin_a + dv * cos_a;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* mesh_uv_scale                                                       */
/* ------------------------------------------------------------------ */

bool mesh_uv_scale(mesh_slot_t *slot, const mesh_sel_bitset_t *sel,
                   float su, float sv, float pivot_u, float pivot_v,
                   int channel) {
    if (!slot || !sel || channel < 0 || channel >= MESH_SLOT_UV_CHANNELS) return false;
    if (!slot->uvs[channel]) return false;

    uint32_t vc = slot->vertex_count;
    uint8_t touched[MESH_SLOT_MAX_VERTICES];
    memset(touched, 0, vc);
    collect_verts_(slot, sel, touched, vc);

    float cu, cv;
    if (pivot_u < 0 || pivot_v < 0) {
        uv_centroid_(slot, touched, channel, &cu, &cv);
    } else {
        cu = pivot_u; cv = pivot_v;
    }

    for (uint32_t v = 0; v < vc; v++) {
        if (!touched[v]) continue;
        slot->uvs[channel][v*2+0] = cu + (slot->uvs[channel][v*2+0] - cu) * su;
        slot->uvs[channel][v*2+1] = cv + (slot->uvs[channel][v*2+1] - cv) * sv;
    }
    return true;
}
