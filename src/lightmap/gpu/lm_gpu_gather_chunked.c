/**
 * @file lm_gpu_gather_chunked.c
 * @brief Chunked GPU GI gather (rpg-fzht): partition the scene into cubic NEAR
 *        chunks, and into a coarser FAR grid whose cells each cover a whole
 *        neighbourhood of near chunks. Each near chunk builds its OWN fine SVO
 *        over its outer box (so a massive scene never needs one whole-scene
 *        octree) and gathers against three fields: its chunk's fine near SDF, the
 *        per-chunk medium field (built inside @ref lm_gpu_gather_run), and the
 *        coarse FAR SDF SHARED by every near chunk in the same far cell. See
 *        lm_gpu_gather.h.
 */
#include "ferrum/lightmap/gpu/lm_gpu_gather.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "ferrum/lightmap/lm_chunk_svo.h"
#include "ferrum/renderer/chunk/chunk_grid.h"

bool lm_gpu_gather_chunked(const lm_lightmap_t *lm, lm_sh9_t *accum,
                           phys_aabb_t scene_bounds, float fine_voxel,
                           const lm_mesh_scene_t *scene,
                           float chunk_size, float margin, const lm_light_t *lights,
                           uint32_t n_lights, const lm_sky_t *sky, float transition,
                           float maxdist, uint32_t samples, uint32_t bounces,
                           uint32_t seed, const char *sdf_prefix)
{
    if (lm == NULL || accum == NULL || scene == NULL)
        return false;
    uint32_t nlux = lm->res_u * lm->res_v;
    if (nlux == 0)
        return true;

    /* NEAR grid (fine chunks) + FAR grid (coarse, one cell spans a neighbourhood
     * of near chunks). The far cell overlaps its neighbours generously so a chunk
     * near the far-cell edge still sees geometry a good way into adjacent cells. */
    float far_mult = getenv("LM_FAR_MULT") ? (float)atof(getenv("LM_FAR_MULT")) : 4.0f;
    int   far_dim  = getenv("LM_FAR_DIM")  ? atoi(getenv("LM_FAR_DIM"))         : 64;
    if (far_mult < 1.0f) far_mult = 1.0f;
    if (far_dim  < 8)    far_dim  = 8;

    chunk_grid_t ngrid, fgrid;
    if (!chunk_grid_init(&ngrid, scene_bounds, chunk_size, margin))
        return false;
    if (!chunk_grid_init(&fgrid, scene_bounds, chunk_size * far_mult, chunk_size * far_mult))
        return false;
    uint32_t nchunks = chunk_grid_count(&ngrid);
    uint32_t nfar    = chunk_grid_count(&fgrid);

    /* Per-luxel near-chunk id, per-near-chunk far-cell id, and reusable scratch
     * sized for the worst-case single chunk. */
    uint32_t   *chunk_of = malloc((size_t)nlux * sizeof(uint32_t));
    uint32_t   *farcell  = malloc((size_t)nchunks * sizeof(uint32_t));
    uint32_t   *idx      = malloc((size_t)nlux * sizeof(uint32_t));
    lm_luxel_t *tlux     = malloc((size_t)nlux * sizeof(lm_luxel_t));
    lm_sh9_t   *sacc     = malloc((size_t)nlux * 3 * sizeof(lm_sh9_t));
    if (!chunk_of || !farcell || !idx || !tlux || !sacc) {
        free(chunk_of); free(farcell); free(idx); free(tlux); free(sacc);
        return false;
    }

    /* Assign each luxel to the near chunk its position falls in. */
    for (uint32_t i = 0; i < nlux; ++i) {
        vec3_t p = lm->luxels[i].pos;
        uint32_t c = chunk_grid_of_point(&ngrid, p.x, p.y, p.z);
        chunk_of[i] = (c == UINT32_MAX) ? 0u : c;   /* clamp strays to chunk 0 */
    }
    /* Map each near chunk to the far cell containing its centre. */
    for (uint32_t c = 0; c < nchunks; ++c) {
        phys_aabb_t inner;
        chunk_grid_bounds(&ngrid, c, &inner, NULL);
        float cx = (inner.min.x + inner.max.x) * 0.5f;
        float cy = (inner.min.y + inner.max.y) * 0.5f;
        float cz = (inner.min.z + inner.max.z) * 0.5f;
        uint32_t f = chunk_grid_of_point(&fgrid, cx, cy, cz);
        farcell[c] = (f == UINT32_MAX) ? 0u : f;
    }

    /* Outer loop over FAR cells: build each far field ONCE (coarse, shared) and
     * gather every near chunk that maps to it before releasing it. */
    bool ok = true;
    for (uint32_t f = 0; f < nfar && ok; ++f) {
        /* Skip far cells that no populated near chunk maps to. */
        bool any = false;
        for (uint32_t c = 0; c < nchunks && !any; ++c) if (farcell[c] == f) any = true;
        if (!any)
            continue;

        phys_aabb_t fouter;
        chunk_grid_bounds(&fgrid, f, NULL, &fouter);
        lm_gpu_field_t farfield = {0};
        bool has_far = lm_gpu_field_build(scene, fine_voxel, &fouter, far_dim, &farfield);

        for (uint32_t c = 0; c < nchunks && ok; ++c) {
            if (farcell[c] != f)
                continue;
            /* Compact this chunk's luxels into a contiguous sub-lightmap. */
            uint32_t m = 0;
            for (uint32_t i = 0; i < nlux; ++i)
                if (chunk_of[i] == c) { idx[m] = i; tlux[m] = lm->luxels[i]; ++m; }
            if (m == 0)
                continue;

            /* Build this chunk's OWN fine SVO over its outer box, gather, free. */
            phys_aabb_t nouter;
            chunk_grid_bounds(&ngrid, c, NULL, &nouter);
            npc_svo_grid_t csvo;
            if (!lm_chunk_svo_build(scene, nouter, fine_voxel, &csvo)) { ok = false; break; }

            /* Per near-chunk SDF sidecar path (rpg-iudw): <prefix>_cNNN.sdf. */
            char sdf_path[512];
            const char *chunk_sdf = NULL;
            if (sdf_prefix != NULL &&
                snprintf(sdf_path, sizeof sdf_path, "%s_c%03u.sdf", sdf_prefix, c) <
                    (int)sizeof sdf_path)
                chunk_sdf = sdf_path;

            lm_lightmap_t sub = { tlux, m, 1 };
            ok = lm_gpu_gather_run(&sub, sacc, &csvo, scene, &nouter,
                                   has_far ? &farfield : NULL, lights, n_lights, sky,
                                   transition, maxdist, samples, bounces, seed,
                                   chunk_sdf);
            npc_svo_grid_destroy(&csvo);
            if (!ok)
                break;

            /* Scatter the sub-result back to the global luxels. */
            for (uint32_t i = 0; i < m; ++i)
                for (int ch = 0; ch < 3; ++ch)
                    accum[idx[i]*3 + ch] = sacc[i*3 + ch];
        }

        lm_gpu_field_free(&farfield);
    }

    free(chunk_of); free(farcell); free(idx); free(tlux); free(sacc);
    return ok;
}
