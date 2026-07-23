/**
 * @file lm_gpu_chunk_mat.c
 * @brief GPU material fill for a chunk SVO (see lm_gpu_chunk_mat.h, rpg-bpiz).
 *
 * The CPU pass (@ref lm_svo_voxelize) subsamples every triangle's surface and
 * scatters texture samples into the solid leaves -- minutes per chunk on big
 * scenes. Here the GPU rasterizer produces the same dense material grid in one
 * pass; each grid cell is folded into the SOLID leaf its centre queries to,
 * area-weighted, preserving the CPU semantics (diffuse area-MEAN, emissive
 * area-SUM over the voxel cross-section, neutral 0.5 fallback for solid
 * leaves no surface sample reached).
 */
#include "ferrum/lightmap/gpu/lm_gpu_chunk_mat.h"

#include <stdio.h>
#include <stdlib.h>

#include "ferrum/lightmap/gpu/lm_gpu_voxelize.h"
#include "ferrum/lightmap/lm_svo_mip.h"

/* CPU-side transient budget: refuse absurd grids and fall back to the CPU
 * subsample pass instead of thrashing (2^depth per axis; 256^3 = 16.7M ok). */
#define LM_CHUNK_MAT_MAX_CELLS (32u * 1024u * 1024u)

bool lm_gpu_chunk_svo_materials(npc_svo_grid_t *svo, const lm_mesh_t *meshes,
                                uint32_t n_meshes)
{
    if (svo == NULL || svo->nodes == NULL || svo->node_count == 0 ||
        (meshes == NULL && n_meshes > 0))
        return false;
    uint32_t cells_axis = 1u << svo->max_depth;
    size_t n_cells = (size_t)cells_axis * cells_axis * cells_axis;
    if (n_cells > LM_CHUNK_MAT_MAX_CELLS)
        return false;

    int dims[3] = { (int)cells_axis, (int)cells_axis, (int)cells_axis };
    lm_gpu_vox_grid_t g;
    if (!lm_gpu_voxelize_run(meshes, n_meshes, &svo->world_bounds, dims, &g))
        return false;

    /* Node-indexed accumulators (a leaf can span several grid cells when it is
     * coarser than max depth; fold its cells area-weighted). */
    uint32_t nc = svo->node_count;
    float *acc = calloc((size_t)nc * 8u, sizeof(float));
    if (acc == NULL) {
        lm_gpu_vox_grid_free(&g);
        return false;
    }
    for (int z = 0; z < dims[2]; ++z)
        for (int y = 0; y < dims[1]; ++y)
            for (int x = 0; x < dims[0]; ++x) {
                size_t ci = ((size_t)z * (size_t)dims[1] + (size_t)y) *
                            (size_t)dims[0] + (size_t)x;
                if (g.area[ci] <= 0.0f && g.emissive[ci * 3] <= 0.0f &&
                    g.emissive[ci * 3 + 1] <= 0.0f &&
                    g.emissive[ci * 3 + 2] <= 0.0f)
                    continue;
                phys_vec3_t p = {
                    g.origin[0] + ((float)x + 0.5f) * g.cell[0],
                    g.origin[1] + ((float)y + 0.5f) * g.cell[1],
                    g.origin[2] + ((float)z + 0.5f) * g.cell[2]
                };
                uint32_t node = NPC_SVO_INVALID_NODE;
                uint8_t flags = npc_svo_query_point(svo, p, &node);
                if (!(flags & NPC_SVO_FLAG_SOLID) || node >= nc)
                    continue;
                float *a = &acc[(size_t)node * 8u];
                float w = g.area[ci];
                a[0] += g.albedo[ci * 3 + 0] * w;
                a[1] += g.albedo[ci * 3 + 1] * w;
                a[2] += g.albedo[ci * 3 + 2] * w;
                a[3] += w;
                a[4] += g.emissive[ci * 3 + 0];
                a[5] += g.emissive[ci * 3 + 1];
                a[6] += g.emissive[ci * 3 + 2];
                a[7] += 1.0f;
            }
    lm_gpu_vox_grid_free(&g);

    /* Write the nodes: area-mean diffuse, per-cross-section emissive (cell
     * average when a coarse leaf spans several cells), CPU-matching stats. */
    uint32_t solid_leaves = 0, solid_no_mat = 0;
    for (uint32_t i = 0; i < nc; ++i) {
        npc_svo_node_t *nd = &svo->nodes[i];
        const float *a = &acc[(size_t)i * 8u];
        bool leaf = nd->occupancy == 0 && (nd->flags & NPC_SVO_FLAG_SOLID);
        if (a[3] > 0.0f) {
            float inv = 1.0f / a[3];
            nd->diffuse[0] = a[0] * inv;
            nd->diffuse[1] = a[1] * inv;
            nd->diffuse[2] = a[2] * inv;
        } else {
            nd->diffuse[0] = nd->diffuse[1] = nd->diffuse[2] = 0.0f;
        }
        float cinv = a[7] > 0.0f ? 1.0f / a[7] : 0.0f;
        nd->emissive[0] = a[4] * cinv;
        nd->emissive[1] = a[5] * cinv;
        nd->emissive[2] = a[6] * cinv;
        if (leaf) {
            ++solid_leaves;
            if (a[3] <= 0.0f) {
                ++solid_no_mat;
                /* Same neutral fallback as the CPU pass: a black solid leaf
                 * is a light SINK for every gather ray that hits it. */
                nd->diffuse[0] = nd->diffuse[1] = nd->diffuse[2] = 0.5f;
            }
        }
    }
    free(acc);
    fprintf(stderr,
            "voxelize(gpu): %u solid leaves, %u with NO material (%.1f%% gap)\n",
            solid_leaves, solid_no_mat,
            solid_leaves ? 100.0f * (float)solid_no_mat / (float)solid_leaves
                         : 0.0f);
    lm_svo_mip_average_up(svo);
    return true;
}
