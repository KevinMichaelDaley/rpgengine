/**
 * @file frustum_cull.c
 * @brief Shared AABB-vs-frustum culling (rpg-0rs4). See frustum_cull.h.
 *
 * The plane extraction and positive-vertex AABB test are the exact routines
 * that were private to shadow_csm_render.c (csm_extract_planes / csm_cull),
 * hoisted verbatim so every draw loop shares one implementation.
 */
#include "ferrum/renderer/cull/frustum_cull.h"

#include <math.h>

/* Transform the 8 corners of local AABB [lmin,lmax] by the column-major @p model
 * and reduce to a world-space AABB [wmin,wmax]. */
static void world_aabb(const float model[16], const float lmin[3],
                       const float lmax[3], float wmin[3], float wmax[3])
{
    wmin[0] = wmin[1] = wmin[2] = 1e30f;
    wmax[0] = wmax[1] = wmax[2] = -1e30f;
    for (int c = 0; c < 8; ++c) {
        float lc[3] = { (c & 1) ? lmax[0] : lmin[0],
                        (c & 2) ? lmax[1] : lmin[1],
                        (c & 4) ? lmax[2] : lmin[2] };
        for (int r = 0; r < 3; ++r) {
            float w = model[0*4 + r] * lc[0] + model[1*4 + r] * lc[1]
                    + model[2*4 + r] * lc[2] + model[3*4 + r];
            if (w < wmin[r]) wmin[r] = w;
            if (w > wmax[r]) wmax[r] = w;
        }
    }
}

/* True if the world AABB is fully outside any of the 6 planes (positive-vertex
 * test: if the AABB corner farthest along a plane normal is still behind it, the
 * whole box is behind that plane). Tight -- no sphere approximation. */
static int aabb_outside_planes(const float pl[6][4], const float wmin[3],
                               const float wmax[3])
{
    for (int i = 0; i < 6; ++i) {
        float px = pl[i][0] >= 0.0f ? wmax[0] : wmin[0];
        float py = pl[i][1] >= 0.0f ? wmax[1] : wmin[1];
        float pz = pl[i][2] >= 0.0f ? wmax[2] : wmin[2];
        if (pl[i][0]*px + pl[i][1]*py + pl[i][2]*pz + pl[i][3] < 0.0f)
            return 1;
    }
    return 0;
}

/* Squared distance from @p eye to the nearest point of world AABB [wmin,wmax]. */
static float aabb_dist2_to_point(const float wmin[3], const float wmax[3],
                                 const float eye[3])
{
    float d2 = 0.0f;
    for (int r = 0; r < 3; ++r) {
        float e = eye[r];
        float v = e < wmin[r] ? (wmin[r] - e) : (e > wmax[r] ? (e - wmax[r]) : 0.0f);
        d2 += v * v;
    }
    return d2;
}

void frustum_extract_planes(const float m[16], float pl[6][4])
{
    for (int i = 0; i < 6; ++i) {
        int r = i >> 1;                       /* 0=x, 1=y, 2=z plane pair */
        float s = (i & 1) ? -1.0f : 1.0f;     /* + = left/bottom/near, - = opposite */
        for (int k = 0; k < 4; ++k)
            pl[i][k] = m[k*4 + 3] + s * m[k*4 + r];
        float n = sqrtf(pl[i][0]*pl[i][0] + pl[i][1]*pl[i][1] + pl[i][2]*pl[i][2]);
        if (n > 1e-8f) for (int k = 0; k < 4; ++k) pl[i][k] /= n;
    }
}

void frustum_extract_planes_vp(const float proj[16], const float view[16],
                               float pl[6][4])
{
    /* mvp = proj * view, column-major: mvp[col*4+row] = sum_k proj[k*4+row]*view[col*4+k]. */
    float mvp[16];
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k)
                s += proj[k*4 + row] * view[col*4 + k];
            mvp[col*4 + row] = s;
        }
    frustum_extract_planes(mvp, pl);
}

int frustum_cull_aabb(const float pl[6][4], const float model[16],
                      const float lmin[3], const float lmax[3])
{
    float wmin[3], wmax[3];
    world_aabb(model, lmin, lmax, wmin, wmax);
    return aabb_outside_planes(pl, wmin, wmax);
}

int frustum_cull_aabb_ex(const float pl[6][4], const float model[16],
                         const float lmin[3], const float lmax[3],
                         const float eye[3], float max_dist)
{
    float wmin[3], wmax[3];
    world_aabb(model, lmin, lmax, wmin, wmax);
    if (aabb_outside_planes(pl, wmin, wmax))
        return 1;
    if (max_dist > 0.0f &&
        aabb_dist2_to_point(wmin, wmax, eye) > max_dist * max_dist)
        return 1;
    return 0;
}
