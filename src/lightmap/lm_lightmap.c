/**
 * @file lm_lightmap.c
 * @brief Per-surface luxel grid extraction and SH read-back (see lm_lightmap.h).
 */
#include "ferrum/lightmap/lm_lightmap.h"

#include <stddef.h>

bool lm_lightmap_from_surface(lm_lightmap_t *lm, const lm_surface_t *surface,
                              arena_t *arena)
{
    lm->res_u = surface->res_u;
    lm->res_v = surface->res_v;
    lm->luxels = NULL;
    size_t n = (size_t)surface->res_u * surface->res_v;
    if (n == 0)
        return true;

    lm->luxels = arena_alloc(arena, _Alignof(lm_luxel_t), n * sizeof(lm_luxel_t));
    if (!lm->luxels)
        return false;

    for (uint32_t iv = 0; iv < surface->res_v; ++iv) {
        for (uint32_t iu = 0; iu < surface->res_u; ++iu) {
            lm_luxel_t *lx = lm_lightmap_at(lm, iu, iv);
            lx->pos = lm_surface_point(surface, iu, iv);
            lx->normal = surface->normal;
            lx->albedo = surface->albedo;
            lx->emissive = surface->emissive;
            lm_sh9_zero(&lx->sh[0]);
            lm_sh9_zero(&lx->sh[1]);
            lm_sh9_zero(&lx->sh[2]);
        }
    }
    return true;
}

void lm_lightmap_readback(const lm_lightmap_t *lm, float *out_rgb)
{
    size_t n = (size_t)lm->res_u * lm->res_v;
    for (size_t i = 0; i < n; ++i) {
        const lm_luxel_t *lx = &lm->luxels[i];
        for (int c = 0; c < 3; ++c) {
            float e = lm_sh9_irradiance(&lx->sh[c], lx->normal);
            out_rgb[i * 3 + c] = e > 0.0f ? e : 0.0f;
        }
    }
}
