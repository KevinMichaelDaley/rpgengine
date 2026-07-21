/**
 * @file cull_internal.h
 * @brief Shared AABB geometry primitives for the cull module (rpg-0rs4/rpg-9u96).
 *
 * static inline so frustum_cull.c and sphere_cull.c share one implementation
 * without an extra translation unit (keeps each .c within the 4-function rule).
 * Not a public API -- consumers use frustum_cull.h / sphere_cull.h.
 */
#ifndef FERRUM_RENDERER_CULL_INTERNAL_H
#define FERRUM_RENDERER_CULL_INTERNAL_H

/* Transform the 8 corners of local AABB [lmin,lmax] by column-major @p model and
 * reduce to a world-space AABB [wmin,wmax]. */
static inline void cull_world_aabb(const float model[16], const float lmin[3],
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

/* Squared distance from point @p p to the nearest point of world AABB [wmin,wmax]
 * (0 when p is inside). */
static inline float cull_aabb_point_dist2(const float wmin[3], const float wmax[3],
                                          const float p[3])
{
    float d2 = 0.0f;
    for (int r = 0; r < 3; ++r) {
        float e = p[r];
        float v = e < wmin[r] ? (wmin[r] - e) : (e > wmax[r] ? (e - wmax[r]) : 0.0f);
        d2 += v * v;
    }
    return d2;
}

#endif /* FERRUM_RENDERER_CULL_INTERNAL_H */
