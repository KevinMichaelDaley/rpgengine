/**
 * @file lm_chunk_svo.c
 * @brief Per-chunk fine SVO builder for the chunked GPU bake (see lm_chunk_svo.h).
 */
#include "ferrum/lightmap/lm_chunk_svo.h"

#include <stdlib.h>
#include <string.h>

#include "ferrum/lightmap/lm_svo_material.h"
#include "ferrum/lightmap/lm_svo_voxelize.h"

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
                        float voxel, npc_svo_grid_t *out_svo) {
    if (!scene || !out_svo) return false;
    memset(out_svo, 0, sizeof *out_svo);
    uint32_t depth = chunk_depth(box, voxel);
    if (!npc_svo_grid_init(out_svo, box, depth)) return false;

    for (uint32_t i = 0; i < scene->n_meshes; ++i)
        chunk_stamp(out_svo, &scene->meshes[i]);

    /* Voxelize textured material + smooth normal into the leaves (node-count
     * sized scratch, small per chunk). */
    uint32_t nc = out_svo->node_count;
    float  *area = malloc((size_t)(nc ? nc : 1) * sizeof(float));
    vec3_t *nrm  = malloc((size_t)(nc ? nc : 1) * sizeof(vec3_t));
    if (!area || !nrm) { free(area); free(nrm); npc_svo_grid_destroy(out_svo); return false; }
    lm_svo_voxelize(out_svo, scene->meshes, scene->n_meshes, area, nrm);
    free(area); free(nrm);
    return true;
}
