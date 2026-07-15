/**
 * @file lm_gpu_gather_chunked.c
 * @brief Chunked GPU GI gather (rpg-fzht): partition the scene into cubic chunks,
 *        and gather each chunk's luxels against a per-chunk coarse SDF built over
 *        the chunk's OUTER (chunk+margin) box, sharing one full-scene SVO. Lets a
 *        large scene bake with a bounded per-pass SDF instead of one field over
 *        the whole world. See lm_gpu_gather.h.
 */
#include "ferrum/lightmap/gpu/lm_gpu_gather.h"

#include <stdint.h>
#include <stdlib.h>

#include "ferrum/renderer/chunk/chunk_grid.h"

bool lm_gpu_gather_chunked(const lm_lightmap_t *lm, lm_sh9_t *accum,
                           const npc_svo_grid_t *svo, const lm_mesh_scene_t *scene,
                           float chunk_size, float margin, const lm_light_t *lights,
                           uint32_t n_lights, const lm_sky_t *sky, float transition,
                           float maxdist, uint32_t samples, uint32_t bounces,
                           uint32_t seed)
{
    if (lm == NULL || accum == NULL || svo == NULL)
        return false;
    uint32_t nlux = lm->res_u * lm->res_v;
    if (nlux == 0)
        return true;

    chunk_grid_t grid;
    if (!chunk_grid_init(&grid, svo->world_bounds, chunk_size, margin))
        return false;
    uint32_t nchunks = chunk_grid_count(&grid);

    /* Per-luxel chunk id + reusable scratch sized for the worst-case single chunk. */
    uint32_t   *chunk_of = malloc((size_t)nlux * sizeof(uint32_t));
    uint32_t   *idx      = malloc((size_t)nlux * sizeof(uint32_t));
    lm_luxel_t *tlux     = malloc((size_t)nlux * sizeof(lm_luxel_t));
    lm_sh9_t   *sacc     = malloc((size_t)nlux * 3 * sizeof(lm_sh9_t));
    if (!chunk_of || !idx || !tlux || !sacc) {
        free(chunk_of); free(idx); free(tlux); free(sacc);
        return false;
    }

    /* Assign each luxel to the chunk its position falls in (its inner cell). */
    for (uint32_t i = 0; i < nlux; ++i) {
        vec3_t p = lm->luxels[i].pos;
        uint32_t c = chunk_grid_of_point(&grid, p.x, p.y, p.z);
        chunk_of[i] = (c == UINT32_MAX) ? 0u : c;   /* clamp strays to chunk 0 */
    }

    bool ok = true;
    for (uint32_t c = 0; c < nchunks && ok; ++c) {
        /* Compact this chunk's luxels into a contiguous sub-lightmap. */
        uint32_t m = 0;
        for (uint32_t i = 0; i < nlux; ++i)
            if (chunk_of[i] == c) { idx[m] = i; tlux[m] = lm->luxels[i]; ++m; }
        if (m == 0)
            continue;

        lm_lightmap_t sub = { tlux, m, 1 };
        phys_aabb_t outer;
        chunk_grid_bounds(&grid, c, NULL, &outer);
        ok = lm_gpu_gather_run(&sub, sacc, svo, scene, &outer, lights, n_lights, sky,
                               transition, maxdist, samples, bounces, seed);
        if (!ok)
            break;

        /* Scatter the sub-result back to the global luxels. */
        for (uint32_t i = 0; i < m; ++i)
            for (int ch = 0; ch < 3; ++ch)
                accum[idx[i]*3 + ch] = sacc[i*3 + ch];
    }

    free(chunk_of); free(idx); free(tlux); free(sacc);
    return ok;
}
