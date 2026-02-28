/**
 * @file mesh_uv_fit.c
 * @brief UV fit-to-range and grid snap for selected faces.
 *
 * Non-static functions (2 of 4): mesh_uv_fit, mesh_uv_grid_snap.
 */
#include "ferrum/editor/mesh/mesh_uv_transform.h"

#include <math.h>
#include <string.h>
#include <float.h>

/* ------------------------------------------------------------------ */
/* Static: mark touched vertices from selected faces                   */
/* ------------------------------------------------------------------ */

static void mark_touched_(const mesh_slot_t *slot,
                           const mesh_sel_bitset_t *sel,
                           uint8_t *touched, uint32_t max_v) {
    uint32_t fc = slot->index_count / 3;
    for (uint32_t f = 0; f < fc; f++) {
        if (!mesh_sel_bitset_test(sel, f)) continue;
        for (int j = 0; j < 3; j++) {
            uint32_t vi = slot->indices[f*3+j];
            if (vi < max_v) touched[vi] = 1;
        }
    }
}

/* ------------------------------------------------------------------ */
/* mesh_uv_fit                                                         */
/* ------------------------------------------------------------------ */

bool mesh_uv_fit(mesh_slot_t *slot, const mesh_sel_bitset_t *sel,
                 int channel) {
    if (!slot || !sel || channel < 0 || channel >= MESH_SLOT_UV_CHANNELS) return false;
    if (!slot->uvs[channel]) return false;

    uint32_t vc = slot->vertex_count;
    uint8_t touched[MESH_SLOT_MAX_VERTICES];
    memset(touched, 0, vc);
    mark_touched_(slot, sel, touched, vc);

    /* Find UV bounds */
    float u_min = FLT_MAX, u_max = -FLT_MAX;
    float v_min = FLT_MAX, v_max = -FLT_MAX;
    bool any = false;

    for (uint32_t v = 0; v < vc; v++) {
        if (!touched[v]) continue;
        float u = slot->uvs[channel][v*2+0];
        float vv = slot->uvs[channel][v*2+1];
        if (u < u_min) u_min = u;
        if (u > u_max) u_max = u;
        if (vv < v_min) v_min = vv;
        if (vv > v_max) v_max = vv;
        any = true;
    }
    if (!any) return false;

    float u_range = u_max - u_min;
    float v_range = v_max - v_min;
    if (u_range < 1e-12f) u_range = 1.0f;
    if (v_range < 1e-12f) v_range = 1.0f;

    /* Normalize to [0,1] */
    for (uint32_t v = 0; v < vc; v++) {
        if (!touched[v]) continue;
        slot->uvs[channel][v*2+0] = (slot->uvs[channel][v*2+0] - u_min) / u_range;
        slot->uvs[channel][v*2+1] = (slot->uvs[channel][v*2+1] - v_min) / v_range;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* mesh_uv_grid_snap                                                   */
/* ------------------------------------------------------------------ */

bool mesh_uv_grid_snap(mesh_slot_t *slot, const mesh_sel_bitset_t *sel,
                       float grid_size, int channel) {
    if (!slot || !sel || channel < 0 || channel >= MESH_SLOT_UV_CHANNELS) return false;
    if (!slot->uvs[channel] || grid_size <= 0.0f) return false;

    uint32_t vc = slot->vertex_count;
    uint8_t touched[MESH_SLOT_MAX_VERTICES];
    memset(touched, 0, vc);
    mark_touched_(slot, sel, touched, vc);

    for (uint32_t v = 0; v < vc; v++) {
        if (!touched[v]) continue;
        slot->uvs[channel][v*2+0] = roundf(slot->uvs[channel][v*2+0] / grid_size) * grid_size;
        slot->uvs[channel][v*2+1] = roundf(slot->uvs[channel][v*2+1] / grid_size) * grid_size;
    }
    return true;
}
