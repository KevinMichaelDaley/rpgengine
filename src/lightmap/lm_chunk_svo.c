/**
 * @file lm_chunk_svo.c
 * @brief Per-chunk fine SVO builder for the chunked GPU bake (see lm_chunk_svo.h).
 */
#include "ferrum/lightmap/lm_chunk_svo.h"

#include <stdlib.h>
#include <string.h>

#include "ferrum/lightmap/lm_svo_material.h"
#include "ferrum/lightmap/lm_svo_voxelize.h"

/* Mesh world AABB overlaps @p box? (Cheap cull so a chunk only stamps/voxelizes
 * the few meshes that touch it, instead of the whole scene, per chunk.) */
static bool mesh_overlaps(const lm_mesh_t *m, phys_aabb_t box) {
    if (m->vert_count == 0) return false;
    float mn[3] = { m->positions[0], m->positions[1], m->positions[2] };
    float mx[3] = { mn[0], mn[1], mn[2] };
    for (uint32_t v = 1; v < m->vert_count; ++v)
        for (int c = 0; c < 3; ++c) {
            float p = m->positions[v*3+c];
            if (p < mn[c]) mn[c] = p;
            if (p > mx[c]) mx[c] = p;
        }
    return mx[0] >= box.min.x && mn[0] <= box.max.x &&
           mx[1] >= box.min.y && mn[1] <= box.max.y &&
           mx[2] >= box.min.z && mn[2] <= box.max.z;
}

/* Stamp one mesh's triangles into the SVO with its material id (mirrors the
 * whole-scene lm_mesh_stamp in lm_mesh_bake.c). Triangles outside the octree
 * bounds are clipped away by the stamp, so passing every mesh is correct -- only
 * the geometry overlapping this chunk's box lands in its SVO. */
static void chunk_stamp(npc_svo_grid_t *svo, const lm_mesh_t *m) {
    for (uint32_t t = 0; t + 2 < m->index_count; t += 3) {
        uint32_t a = m->indices[t], b = m->indices[t + 1], c = m->indices[t + 2];
        phys_triangle_t tri;
        tri.v[0] = (phys_vec3_t){ m->positions[a*3], m->positions[a*3+1], m->positions[a*3+2] };
        tri.v[1] = (phys_vec3_t){ m->positions[b*3], m->positions[b*3+1], m->positions[b*3+2] };
        tri.v[2] = (phys_vec3_t){ m->positions[c*3], m->positions[c*3+1], m->positions[c*3+2] };
        lm_svo_stamp_triangle(svo, &tri, m->material);
    }
}

/* Depth whose voxel edge is closest to @p voxel over the box's longest extent,
 * clamped to [1, NPC_SVO_MAX_DEPTH] (matches lm_mesh_bake's derivation). */
static uint32_t chunk_depth(phys_aabb_t box, float voxel) {
    float ex = box.max.x - box.min.x, ey = box.max.y - box.min.y, ez = box.max.z - box.min.z;
    float ext = ex > ey ? (ex > ez ? ex : ez) : (ey > ez ? ey : ez);
    if (voxel <= 0.0f || ext <= 0.0f) return NPC_SVO_MAX_DEPTH;
    uint32_t d = 1;
    while (d < NPC_SVO_MAX_DEPTH && ext / (float)(1u << d) > voxel) ++d;
    if (d > 1) {
        float coarse = ext / (float)(1u << (d - 1)), fine = ext / (float)(1u << d);
        if (coarse - voxel < voxel - fine) --d; /* the coarser voxel is closer */
    }
    return d;
}

bool lm_chunk_svo_build(const lm_mesh_scene_t *scene, phys_aabb_t box,
                        float voxel, bool fill_materials,
                        npc_svo_grid_t *out_svo) {
    if (!scene || !out_svo) return false;
    memset(out_svo, 0, sizeof *out_svo);
    uint32_t depth = chunk_depth(box, voxel);
    if (!npc_svo_grid_init(out_svo, box, depth)) return false;

    /* Compact the meshes that actually overlap this chunk, so both the stamp and
     * the voxelize are O(local geometry) rather than O(whole scene) per chunk. */
    lm_mesh_t *local = malloc((size_t)(scene->n_meshes ? scene->n_meshes : 1) * sizeof(lm_mesh_t));
    if (!local) { npc_svo_grid_destroy(out_svo); return false; }
    uint32_t nlocal = 0;
    for (uint32_t i = 0; i < scene->n_meshes; ++i)
        if (mesh_overlaps(&scene->meshes[i], box)) local[nlocal++] = scene->meshes[i];

    for (uint32_t i = 0; i < nlocal; ++i)
        chunk_stamp(out_svo, &local[i]);

    if (!fill_materials) {          /* a GPU material fill follows (rpg-bpiz) */
        free(local);
        return true;
    }

    /* Voxelize textured material + smooth normal into the leaves (node-count
     * sized scratch, small per chunk). */
    uint32_t nc = out_svo->node_count;
    float  *area = malloc((size_t)(nc ? nc : 1) * sizeof(float));
    vec3_t *nrm  = malloc((size_t)(nc ? nc : 1) * sizeof(vec3_t));
    if (!area || !nrm) { free(area); free(nrm); free(local); npc_svo_grid_destroy(out_svo); return false; }
    lm_svo_voxelize(out_svo, local, nlocal, area, nrm);
    free(area); free(nrm); free(local);
    return true;
}
