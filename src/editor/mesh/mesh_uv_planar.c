/**
 * @file mesh_uv_planar.c
 * @brief Planar and box UV projection.
 *
 * Non-static functions (2 of 4): mesh_uv_planar, mesh_uv_box.
 */
#include "ferrum/editor/mesh/mesh_uv.h"

#include <math.h>
#include <float.h>

/* ------------------------------------------------------------------ */
/* Static helpers                                                      */
/* ------------------------------------------------------------------ */

/** Get the two axes perpendicular to the projection axis. */
static void perp_axes_(mesh_axis_t axis, int *u_axis, int *v_axis) {
    switch (axis) {
    case MESH_AXIS_X: *u_axis = 1; *v_axis = 2; break;
    case MESH_AXIS_Y: *u_axis = 0; *v_axis = 2; break;
    default:          *u_axis = 0; *v_axis = 1; break; /* MESH_AXIS_Z */
    }
}

/** Compute face normal (unnormalized). */
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
}

/* ------------------------------------------------------------------ */
/* Public: mesh_uv_planar                                              */
/* ------------------------------------------------------------------ */

bool mesh_uv_planar(mesh_slot_t *slot, const mesh_sel_bitset_t *sel,
                    mesh_axis_t axis, int channel) {
    if (!slot || !sel || channel < 0 || channel >= MESH_SLOT_UV_CHANNELS) return false;
    if (!slot->uvs[channel]) return false;

    int u_ax, v_ax;
    perp_axes_(axis, &u_ax, &v_ax);

    uint32_t fc = slot->index_count / 3;

    /* Find bounding box of selected vertices */
    float u_min = FLT_MAX, u_max = -FLT_MAX;
    float v_min = FLT_MAX, v_max = -FLT_MAX;
    bool any = false;

    for (uint32_t f = 0; f < fc; f++) {
        if (!mesh_sel_bitset_test(sel, f)) continue;
        for (int j = 0; j < 3; j++) {
            uint32_t vi = slot->indices[f*3+j];
            float u = slot->positions[vi*3 + u_ax];
            float v = slot->positions[vi*3 + v_ax];
            if (u < u_min) u_min = u;
            if (u > u_max) u_max = u;
            if (v < v_min) v_min = v;
            if (v > v_max) v_max = v;
            any = true;
        }
    }
    if (!any) return false;

    float u_range = u_max - u_min;
    float v_range = v_max - v_min;
    if (u_range < 1e-12f) u_range = 1.0f;
    if (v_range < 1e-12f) v_range = 1.0f;

    /* Project */
    for (uint32_t f = 0; f < fc; f++) {
        if (!mesh_sel_bitset_test(sel, f)) continue;
        for (int j = 0; j < 3; j++) {
            uint32_t vi = slot->indices[f*3+j];
            slot->uvs[channel][vi*2+0] = (slot->positions[vi*3+u_ax] - u_min) / u_range;
            slot->uvs[channel][vi*2+1] = (slot->positions[vi*3+v_ax] - v_min) / v_range;
        }
    }

    return true;
}

/* ------------------------------------------------------------------ */
/* Public: mesh_uv_box                                                 */
/* ------------------------------------------------------------------ */

bool mesh_uv_box(mesh_slot_t *slot, const mesh_sel_bitset_t *sel,
                 int channel) {
    if (!slot || !sel || channel < 0 || channel >= MESH_SLOT_UV_CHANNELS) return false;
    if (!slot->uvs[channel]) return false;

    uint32_t fc = slot->index_count / 3;

    /* Find global bounding box of selected vertices */
    float bbox_min[3] = { FLT_MAX, FLT_MAX, FLT_MAX };
    float bbox_max[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

    for (uint32_t f = 0; f < fc; f++) {
        if (!mesh_sel_bitset_test(sel, f)) continue;
        for (int j = 0; j < 3; j++) {
            uint32_t vi = slot->indices[f*3+j];
            for (int k = 0; k < 3; k++) {
                float v = slot->positions[vi*3+k];
                if (v < bbox_min[k]) bbox_min[k] = v;
                if (v > bbox_max[k]) bbox_max[k] = v;
            }
        }
    }

    float range[3];
    for (int k = 0; k < 3; k++) {
        range[k] = bbox_max[k] - bbox_min[k];
        if (range[k] < 1e-12f) range[k] = 1.0f;
    }

    /* For each selected face, project onto the dominant axis plane */
    for (uint32_t f = 0; f < fc; f++) {
        if (!mesh_sel_bitset_test(sel, f)) continue;

        float n[3];
        face_normal_(slot, f, n);

        /* Find dominant axis (largest absolute normal component) */
        float ax = fabsf(n[0]), ay = fabsf(n[1]), az = fabsf(n[2]);
        mesh_axis_t axis;
        if (ax >= ay && ax >= az) axis = MESH_AXIS_X;
        else if (ay >= az) axis = MESH_AXIS_Y;
        else axis = MESH_AXIS_Z;

        int u_ax, v_ax;
        perp_axes_(axis, &u_ax, &v_ax);

        for (int j = 0; j < 3; j++) {
            uint32_t vi = slot->indices[f*3+j];
            slot->uvs[channel][vi*2+0] = (slot->positions[vi*3+u_ax] - bbox_min[u_ax]) / range[u_ax];
            slot->uvs[channel][vi*2+1] = (slot->positions[vi*3+v_ax] - bbox_min[v_ax]) / range[v_ax];
        }
    }

    return true;
}
