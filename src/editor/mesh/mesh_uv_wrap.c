/**
 * @file mesh_uv_wrap.c
 * @brief Cylindrical and spherical UV projection.
 *
 * Non-static functions (2 of 4): mesh_uv_cylindrical, mesh_uv_spherical.
 */
#include "ferrum/editor/mesh/mesh_uv.h"

#include <math.h>
#include <float.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------ */
/* Public: mesh_uv_cylindrical                                         */
/* ------------------------------------------------------------------ */

bool mesh_uv_cylindrical(mesh_slot_t *slot, const mesh_sel_bitset_t *sel,
                         mesh_axis_t axis, int channel) {
    if (!slot || !sel || channel < 0 || channel >= MESH_SLOT_UV_CHANNELS) return false;
    if (!slot->uvs[channel]) return false;

    /* Determine which axes map to angular (around) and height */
    int height_ax = (int)axis;
    int ax0, ax1; /* the two perpendicular axes */
    switch (axis) {
    case MESH_AXIS_X: ax0 = 1; ax1 = 2; break;
    case MESH_AXIS_Y: ax0 = 0; ax1 = 2; break;
    default:          ax0 = 0; ax1 = 1; break; /* MESH_AXIS_Z */
    }

    uint32_t fc = slot->index_count / 3;

    /* Find height bounds */
    float h_min = FLT_MAX, h_max = -FLT_MAX;
    for (uint32_t f = 0; f < fc; f++) {
        if (!mesh_sel_bitset_test(sel, f)) continue;
        for (int j = 0; j < 3; j++) {
            uint32_t vi = slot->indices[f*3+j];
            float h = slot->positions[vi*3 + height_ax];
            if (h < h_min) h_min = h;
            if (h > h_max) h_max = h;
        }
    }
    float h_range = h_max - h_min;
    if (h_range < 1e-12f) h_range = 1.0f;

    /* Find centroid on the perpendicular plane */
    float cx = 0, cy = 0;
    uint32_t count = 0;
    for (uint32_t f = 0; f < fc; f++) {
        if (!mesh_sel_bitset_test(sel, f)) continue;
        for (int j = 0; j < 3; j++) {
            uint32_t vi = slot->indices[f*3+j];
            cx += slot->positions[vi*3 + ax0];
            cy += slot->positions[vi*3 + ax1];
            count++;
        }
    }
    if (count == 0) return false;
    cx /= (float)count;
    cy /= (float)count;

    /* Project: U = atan2 / (2π) + 0.5, V = normalized height */
    for (uint32_t f = 0; f < fc; f++) {
        if (!mesh_sel_bitset_test(sel, f)) continue;
        for (int j = 0; j < 3; j++) {
            uint32_t vi = slot->indices[f*3+j];
            float dx = slot->positions[vi*3 + ax0] - cx;
            float dy = slot->positions[vi*3 + ax1] - cy;
            float angle = atan2f(dy, dx);

            slot->uvs[channel][vi*2+0] = angle / (2.0f * (float)M_PI) + 0.5f;
            slot->uvs[channel][vi*2+1] = (slot->positions[vi*3 + height_ax] - h_min) / h_range;
        }
    }

    return true;
}

/* ------------------------------------------------------------------ */
/* Public: mesh_uv_spherical                                           */
/* ------------------------------------------------------------------ */

bool mesh_uv_spherical(mesh_slot_t *slot, const mesh_sel_bitset_t *sel,
                       int channel) {
    if (!slot || !sel || channel < 0 || channel >= MESH_SLOT_UV_CHANNELS) return false;
    if (!slot->uvs[channel]) return false;

    uint32_t fc = slot->index_count / 3;

    /* Find centroid of selected vertices */
    float cx = 0, cy = 0, cz = 0;
    uint32_t count = 0;
    for (uint32_t f = 0; f < fc; f++) {
        if (!mesh_sel_bitset_test(sel, f)) continue;
        for (int j = 0; j < 3; j++) {
            uint32_t vi = slot->indices[f*3+j];
            cx += slot->positions[vi*3+0];
            cy += slot->positions[vi*3+1];
            cz += slot->positions[vi*3+2];
            count++;
        }
    }
    if (count == 0) return false;
    cx /= (float)count;
    cy /= (float)count;
    cz /= (float)count;

    /* Project: U = longitude / (2π), V = latitude / π */
    for (uint32_t f = 0; f < fc; f++) {
        if (!mesh_sel_bitset_test(sel, f)) continue;
        for (int j = 0; j < 3; j++) {
            uint32_t vi = slot->indices[f*3+j];
            float dx = slot->positions[vi*3+0] - cx;
            float dy = slot->positions[vi*3+1] - cy;
            float dz = slot->positions[vi*3+2] - cz;

            float r = sqrtf(dx*dx + dy*dy + dz*dz);
            if (r < 1e-12f) { r = 1e-12f; }

            /* Longitude: atan2(z, x) → U */
            float lon = atan2f(dz, dx);
            /* Latitude: asin(y/r) → V */
            float lat = asinf(dy / r);

            slot->uvs[channel][vi*2+0] = lon / (2.0f * (float)M_PI) + 0.5f;
            slot->uvs[channel][vi*2+1] = lat / (float)M_PI + 0.5f;
        }
    }

    return true;
}
