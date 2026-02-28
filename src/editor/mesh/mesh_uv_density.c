/**
 * @file mesh_uv_density.c
 * @brief Texel density calculation for UV-mapped meshes.
 */
#include "ferrum/editor/mesh/mesh_uv_pack.h"

#include <math.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/** Triangle area from 3 2D points. */
static float tri_area_2d_(float x0, float y0, float x1, float y1,
                           float x2, float y2) {
    return fabsf((x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)) * 0.5f;
}

/** Triangle area from 3 3D points. */
static float tri_area_3d_(const float *p0, const float *p1, const float *p2) {
    float ax = p1[0] - p0[0], ay = p1[1] - p0[1], az = p1[2] - p0[2];
    float bx = p2[0] - p0[0], by = p2[1] - p0[1], bz = p2[2] - p0[2];
    float cx = ay*bz - az*by;
    float cy = az*bx - ax*bz;
    float cz = ax*by - ay*bx;
    return sqrtf(cx*cx + cy*cy + cz*cz) * 0.5f;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

float mesh_uv_texel_density(const mesh_slot_t *slot, uint32_t resolution) {
    if (!slot || resolution == 0) return 0.0f;
    if (!slot->uvs[0]) return 0.0f;

    uint32_t face_count = slot->index_count / 3;
    if (face_count == 0) return 0.0f;

    float total_uv_area = 0.0f;
    float total_world_area = 0.0f;

    for (uint32_t f = 0; f < face_count; f++) {
        uint32_t i0 = slot->indices[f * 3 + 0];
        uint32_t i1 = slot->indices[f * 3 + 1];
        uint32_t i2 = slot->indices[f * 3 + 2];

        float uv_area = tri_area_2d_(
            slot->uvs[0][i0*2+0], slot->uvs[0][i0*2+1],
            slot->uvs[0][i1*2+0], slot->uvs[0][i1*2+1],
            slot->uvs[0][i2*2+0], slot->uvs[0][i2*2+1]);

        float world_area = tri_area_3d_(
            &slot->positions[i0*3],
            &slot->positions[i1*3],
            &slot->positions[i2*3]);

        total_uv_area += uv_area;
        total_world_area += world_area;
    }

    if (total_world_area < 1e-12f) return 0.0f;

    /* texel density = sqrt(uv_area / world_area) * resolution */
    float ratio = total_uv_area / total_world_area;
    return sqrtf(ratio) * (float)resolution;
}
