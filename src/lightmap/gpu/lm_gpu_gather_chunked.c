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

#include "ferrum/lightmap/gpu/lm_gpu_chunk_build.h"
#include "ferrum/lightmap/lm_chunk_svo.h"
#include "ferrum/memory/arena.h"
#include "ferrum/renderer/chunk/chunk_grid.h"
#include "ferrum/renderer/chunk/chunk_tree.h"

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

    chunk_grid_t fgrid;
    if (!chunk_grid_init(&fgrid, scene_bounds, chunk_size * far_mult, chunk_size * far_mult))
        return false;
    uint32_t nfar = chunk_grid_count(&fgrid);

    /* ADAPTIVE near partition (rpg-zw99): DETAIL-dense regions (buildings)
     * subdivide to small leaves -> a fine voxel at the fixed near grid dim, while
     * flat/sparse regions (terrain, road, distant hills) stay large -> a coarse
     * voxel. Detail = meshes whose triangle density over their XZ footprint clears
     * a threshold (LM_DETAIL_DENSITY tris/m^2); terrain/road are big + sparse and
     * fall below it. chunk_size is the COARSE (max) leaf; LM_CHUNK_SPLIT sets how
     * many halvings down to the finest building leaf. Replaces the old uniform
     * grid + the glass-only fine heuristic. */
    float det_dens = getenv("LM_DETAIL_DENSITY") ? (float)atof(getenv("LM_DETAIL_DENSITY")) : 1.0f;
    int   split    = getenv("LM_CHUNK_SPLIT") ? atoi(getenv("LM_CHUNK_SPLIT")) : 4;
    if (split < 1) split = 1;
    float min_chunk = chunk_size / (float)split;
    float *det_min = malloc((size_t)scene->n_meshes * 3u * sizeof(float));
    float *det_max = malloc((size_t)scene->n_meshes * 3u * sizeof(float));
    uint32_t n_det = 0;
    if (det_min != NULL && det_max != NULL) {
        for (uint32_t mi = 0; mi < scene->n_meshes; ++mi) {
            const lm_mesh_t *me = &scene->meshes[mi];
            if (me->positions == NULL || me->vert_count == 0 || me->index_count < 3) continue;
            float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
            for (uint32_t v = 0; v < me->vert_count; ++v)
                for (int a = 0; a < 3; ++a) {
                    float p = me->positions[v * 3u + (uint32_t)a];
                    if (p < lo[a]) lo[a] = p;
                    if (p > hi[a]) hi[a] = p;
                }
            /* ferrum_important FORCES detail regardless of density (low-poly
             * buildings whose interiors still need fine chunks). Otherwise the
             * triangle-density heuristic gates it (flat/sparse -> coarse). */
            if (!me->important) {
                float foot = (hi[0] - lo[0]) * (hi[2] - lo[2]);   /* XZ footprint (m^2). */
                float dens = (float)(me->index_count / 3u) / (foot > 1.0f ? foot : 1.0f);
                if (dens < det_dens) continue;
            }
            for (int a = 0; a < 3; ++a) { det_min[n_det * 3 + a] = lo[a]; det_max[n_det * 3 + a] = hi[a]; }
            ++n_det;
        }
    }
    /* Tree node pool: bounded, offline -> a heap arena is fine. */
    static uint8_t tree_buf[64u * 1024u * 1024u];
    arena_t tree_arena; arena_init(&tree_arena, tree_buf, sizeof tree_buf);
    chunk_tree_t ntree;
    bool tree_ok = chunk_tree_build(&ntree, scene_bounds, min_chunk, chunk_size, margin,
                                    det_min, det_max, n_det, margin, &tree_arena);
    free(det_min); free(det_max);
    if (!tree_ok) return false;
    uint32_t nchunks = chunk_tree_count(&ntree);
    fprintf(stderr, "lm_gpu_gather_chunked: adaptive partition -> %u chunks "
            "(%u detail meshes, %.0f..%.0f m leaves)\n", nchunks, n_det, (double)min_chunk,
            (double)chunk_size);

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

    /* Fixed near grid resolution: the ADAPTIVE part is the leaf SIZE (above), so a
     * building's small leaf yields a fine voxel and a flat leaf a coarse one at the
     * SAME near_dim -> every chunk texture is near_dim^3 (uniform GPU slot). */
    int near_dim = getenv("LM_NEAR_DIM") ? atoi(getenv("LM_NEAR_DIM")) : 128;
    if (near_dim < 16) near_dim = 16;

    /* Assign each luxel to the near LEAF its position falls in. */
    for (uint32_t i = 0; i < nlux; ++i) {
        vec3_t p = lm->luxels[i].pos;
        uint32_t c = chunk_tree_of_point(&ntree, p.x, p.y, p.z);
        chunk_of[i] = (c == UINT32_MAX) ? 0u : c;   /* clamp strays to chunk 0 */
    }
    /* Map each near leaf to the far cell containing its centre. */
    for (uint32_t c = 0; c < nchunks; ++c) {
        phys_aabb_t inner;
        chunk_tree_bounds(&ntree, c, &inner, NULL);
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

            /* Build this leaf's OWN fine SVO over its outer box, gather, free. */
            phys_aabb_t nouter;
            chunk_tree_bounds(&ntree, c, NULL, &nouter);
            npc_svo_grid_t csvo;
            /* The WHOLE chunk octree -- occupancy and materials -- comes from
             * the GPU voxelization (rpg-bpiz): sparse leaf records, readback
             * scales with the octree size. CPU stamp+subsample = fallback. */
            if (!lm_gpu_chunk_svo_build(scene, nouter, fine_voxel, &csvo)) {
                if (!lm_chunk_svo_build(scene, nouter, fine_voxel, true,
                                        &csvo)) { ok = false; break; }
            }

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
                                   chunk_sdf, near_dim);
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
