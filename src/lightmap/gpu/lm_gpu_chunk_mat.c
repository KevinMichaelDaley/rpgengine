/**
 * @file lm_gpu_chunk_mat.c
 * @brief GPU material fill for a chunk SVO (see lm_gpu_chunk_mat.h, rpg-bpiz).
 *
 * The CPU pass (@ref lm_svo_voxelize) subsamples every triangle's surface and
 * scatters texture samples into the solid leaves -- minutes per chunk on big
 * scenes. Here the GPU voxelizes the chunk at FULL leaf resolution (tiled
 * sliced-render-target rasterization, @ref lm_gpu_voxelize_sample) and every
 * SOLID leaf reads its own cell: diffuse keeps its area-MEAN, emissive its
 * per-cross-section SUM, and solid leaves no surface sample reached get the
 * CPU pass's neutral 0.5 fallback.
 */
#include "ferrum/lightmap/gpu/lm_gpu_chunk_mat.h"

#include <stdio.h>
#include <stdlib.h>

#include "ferrum/lightmap/gpu/lm_gpu_voxelize.h"
#include "ferrum/lightmap/lm_svo_mip.h"

/* Explicit DFS stack entry (max depth * 7 + 8 live entries; 13 * 7 + 8 = 99). */
typedef struct chunk_mat_walk {
    uint32_t    node;
    phys_aabb_t box;
} chunk_mat_walk_t;

/* Walk the SOLID leaves (root = node 0 over world_bounds; children in Morton
 * order x=bit0, y=bit1, z=bit2). Emits each leaf's index + centre; returns
 * the leaf count. @p out_idx/@p out_ctr may be NULL to only count. */
static uint32_t walk_leaves(const npc_svo_grid_t *svo, uint32_t *out_idx,
                            float *out_ctr)
{
    chunk_mat_walk_t stack[256];
    uint32_t found = 0;
    int top = 0;
    stack[top].node = 0u;
    stack[top].box = svo->world_bounds;
    ++top;
    while (top > 0) {
        chunk_mat_walk_t e = stack[--top];
        if (e.node >= svo->node_count)
            continue;
        const npc_svo_node_t *nd = &svo->nodes[e.node];
        if (nd->occupancy == 0) {                             /* leaf */
            if (nd->flags & NPC_SVO_FLAG_SOLID) {
                if (out_idx != NULL) out_idx[found] = e.node;
                if (out_ctr != NULL) {
                    out_ctr[found * 3 + 0] = (e.box.min.x + e.box.max.x) * 0.5f;
                    out_ctr[found * 3 + 1] = (e.box.min.y + e.box.max.y) * 0.5f;
                    out_ctr[found * 3 + 2] = (e.box.min.z + e.box.max.z) * 0.5f;
                }
                ++found;
            }
            continue;
        }
        float mid[3] = { (e.box.min.x + e.box.max.x) * 0.5f,
                         (e.box.min.y + e.box.max.y) * 0.5f,
                         (e.box.min.z + e.box.max.z) * 0.5f };
        for (int k = 0; k < 8; ++k) {
            if (nd->children[k] == NPC_SVO_INVALID_NODE)
                continue;
            if (top >= (int)(sizeof stack / sizeof stack[0]))
                break;                        /* cannot happen at legal depth */
            chunk_mat_walk_t *s = &stack[top++];
            s->node = nd->children[k];
            s->box.min.x = (k & 1) ? mid[0] : e.box.min.x;
            s->box.max.x = (k & 1) ? e.box.max.x : mid[0];
            s->box.min.y = (k & 2) ? mid[1] : e.box.min.y;
            s->box.max.y = (k & 2) ? e.box.max.y : mid[1];
            s->box.min.z = (k & 4) ? mid[2] : e.box.min.z;
            s->box.max.z = (k & 4) ? e.box.max.z : mid[2];
        }
    }
    return found;
}

bool lm_gpu_chunk_svo_materials(npc_svo_grid_t *svo, const lm_mesh_t *meshes,
                                uint32_t n_meshes)
{
    if (svo == NULL || svo->nodes == NULL || svo->node_count == 0 ||
        (meshes == NULL && n_meshes > 0))
        return false;

    uint32_t n_leaves = walk_leaves(svo, NULL, NULL);
    for (uint32_t i = 0; i < svo->node_count; ++i) {
        npc_svo_node_t *nd = &svo->nodes[i];
        nd->diffuse[0] = nd->diffuse[1] = nd->diffuse[2] = 0.0f;
        nd->emissive[0] = nd->emissive[1] = nd->emissive[2] = 0.0f;
    }
    if (n_leaves == 0) {
        fprintf(stderr, "voxelize(gpu): 0 solid leaves, 0 with NO material "
                        "(0.0%% gap)\n");
        lm_svo_mip_average_up(svo);
        return true;
    }

    uint32_t *idx = malloc((size_t)n_leaves * sizeof(uint32_t));
    float *ctr = malloc((size_t)n_leaves * 3u * sizeof(float));
    float *area = malloc((size_t)n_leaves * sizeof(float));
    float *alb = malloc((size_t)n_leaves * 3u * sizeof(float));
    float *emi = malloc((size_t)n_leaves * 3u * sizeof(float));
    if (!idx || !ctr || !area || !alb || !emi) {
        free(idx); free(ctr); free(area); free(alb); free(emi);
        return false;
    }
    walk_leaves(svo, idx, ctr);

    uint32_t cells_axis = 1u << svo->max_depth;
    int dims[3] = { (int)cells_axis, (int)cells_axis, (int)cells_axis };
    if (!lm_gpu_voxelize_sample(meshes, n_meshes, &svo->world_bounds, dims,
                                ctr, n_leaves, area, alb, emi)) {
        free(idx); free(ctr); free(area); free(alb); free(emi);
        return false;
    }

    uint32_t solid_no_mat = 0;
    for (uint32_t i = 0; i < n_leaves; ++i) {
        npc_svo_node_t *nd = &svo->nodes[idx[i]];
        if (area[i] > 0.0f) {
            nd->diffuse[0] = alb[i * 3 + 0];
            nd->diffuse[1] = alb[i * 3 + 1];
            nd->diffuse[2] = alb[i * 3 + 2];
        } else {
            /* No surface sample in this leaf's cell: the CPU pass's neutral
             * fallback (a black solid leaf is a light SINK for gather rays). */
            nd->diffuse[0] = nd->diffuse[1] = nd->diffuse[2] = 0.5f;
            ++solid_no_mat;
        }
        nd->emissive[0] = emi[i * 3 + 0];
        nd->emissive[1] = emi[i * 3 + 1];
        nd->emissive[2] = emi[i * 3 + 2];
    }
    free(idx); free(ctr); free(area); free(alb); free(emi);
    fprintf(stderr,
            "voxelize(gpu): %u solid leaves, %u with NO material (%.1f%% gap)\n",
            n_leaves, solid_no_mat,
            n_leaves ? 100.0f * (float)solid_no_mat / (float)n_leaves : 0.0f);
    lm_svo_mip_average_up(svo);
    return true;
}
