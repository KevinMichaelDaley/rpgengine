/**
 * @file gi_zone_sdf_compose.c
 * @brief Compose the global low-res zone SDF from fine chunk SDFs (see
 *        gi_zone_sdf.h). Headless: pure CPU, no GL, unit-tested.
 */
#include <math.h>
#include <stddef.h>

#include "ferrum/renderer/gi/gi_zone_sdf.h"

bool gi_zone_sdf_plan(const gi_zone_sdf_src_t *srcs, uint32_t n_srcs,
                      int32_t max_dim, int32_t out_dims[3], float *out_voxel,
                      float out_origin[3])
{
    if (srcs == NULL || n_srcs == 0 || max_dim < 1 ||
        out_dims == NULL || out_voxel == NULL || out_origin == NULL)
        return false;

    /* Union of the source chunk world boxes. */
    float mn[3] = { 1e30f, 1e30f, 1e30f }, mx[3] = { -1e30f, -1e30f, -1e30f };
    for (uint32_t s = 0; s < n_srcs; ++s) {
        if (srcs[s].dist == NULL || srcs[s].voxel <= 0.0f) return false;
        for (int a = 0; a < 3; ++a) {
            if (srcs[s].dims[a] < 1) return false;
            float lo = srcs[s].origin[a];
            float hi = lo + (float)srcs[s].dims[a] * srcs[s].voxel;
            if (lo < mn[a]) mn[a] = lo;
            if (hi > mx[a]) mx[a] = hi;
        }
    }

    /* Uniform voxel: the LONGEST union extent spans max_dim cells. */
    float ext_max = 0.0f;
    for (int a = 0; a < 3; ++a) {
        float e = mx[a] - mn[a];
        if (e > ext_max) ext_max = e;
    }
    if (ext_max <= 0.0f) return false;
    float vox = ext_max / (float)max_dim;

    for (int a = 0; a < 3; ++a) {
        float e = mx[a] - mn[a];
        int32_t d = (int32_t)ceilf(e / vox - 1e-4f);
        if (d < 1) d = 1;
        if (d > max_dim) d = max_dim;
        out_dims[a] = d;
        out_origin[a] = mn[a];
    }
    *out_voxel = vox;
    return true;
}

bool gi_zone_sdf_compose(const gi_zone_sdf_src_t *srcs, uint32_t n_srcs,
                         const int32_t dims[3], float voxel, const float origin[3],
                         float *out_dist, float *out_albedo, uint32_t cap)
{
    if (srcs == NULL || n_srcs == 0 || dims == NULL || voxel <= 0.0f ||
        origin == NULL || out_dist == NULL || out_albedo == NULL)
        return false;
    if (dims[0] < 1 || dims[1] < 1 || dims[2] < 1) return false;
    uint32_t ncells = (uint32_t)dims[0] * (uint32_t)dims[1] * (uint32_t)dims[2];
    if (ncells > cap) return false;

    /* Init: everything is FAR empty space (the zone diagonal -- metrically "very
     * far" for a marcher) with a neutral albedo; sources pull cells down. */
    float far_d = voxel * sqrtf((float)(dims[0]*dims[0] + dims[1]*dims[1] +
                                        dims[2]*dims[2]));
    if (far_d < 1.0f) far_d = 1.0f;
    for (uint32_t i = 0; i < ncells; ++i) {
        out_dist[i] = far_d;
        out_albedo[i*3+0] = out_albedo[i*3+1] = out_albedo[i*3+2] = 0.5f;
    }

    /* Min-downsample: every fine voxel votes into the coarse cell containing its
     * CENTRE; the minimum distance wins and brings its albedo (nearest surface).
     * Minimum = conservative -- a thin wall's negative/small samples always
     * survive, so the coarse field can only stop a march EARLIER, never leak. */
    for (uint32_t s = 0; s < n_srcs; ++s) {
        const gi_zone_sdf_src_t *c = &srcs[s];
        if (c->dist == NULL || c->voxel <= 0.0f) return false;
        for (int32_t z = 0; z < c->dims[2]; ++z) {
            float wz = c->origin[2] + ((float)z + 0.5f) * c->voxel;
            int32_t cz = (int32_t)floorf((wz - origin[2]) / voxel);
            if (cz < 0 || cz >= dims[2]) continue;
            for (int32_t y = 0; y < c->dims[1]; ++y) {
                float wy = c->origin[1] + ((float)y + 0.5f) * c->voxel;
                int32_t cy = (int32_t)floorf((wy - origin[1]) / voxel);
                if (cy < 0 || cy >= dims[1]) continue;
                for (int32_t x = 0; x < c->dims[0]; ++x) {
                    float wx = c->origin[0] + ((float)x + 0.5f) * c->voxel;
                    int32_t cx = (int32_t)floorf((wx - origin[0]) / voxel);
                    if (cx < 0 || cx >= dims[0]) continue;
                    size_t fi = ((size_t)z * (size_t)c->dims[1] + (size_t)y)
                              * (size_t)c->dims[0] + (size_t)x;
                    size_t ci = ((size_t)cz * (size_t)dims[1] + (size_t)cy)
                              * (size_t)dims[0] + (size_t)cx;
                    float d = c->dist[fi];
                    if (d < out_dist[ci]) {
                        out_dist[ci] = d;
                        if (c->albedo != NULL) {
                            out_albedo[ci*3+0] = c->albedo[fi*3+0];
                            out_albedo[ci*3+1] = c->albedo[fi*3+1];
                            out_albedo[ci*3+2] = c->albedo[fi*3+2];
                        } else {
                            out_albedo[ci*3+0] = out_albedo[ci*3+1] =
                            out_albedo[ci*3+2] = 0.5f;
                        }
                    }
                }
            }
        }
    }
    return true;
}
