/**
 * @file lm_chunk_svo.h
 * @brief Build a fine per-chunk SVO for the chunked GPU lightmap bake (rpg-fzht).
 *
 * A massive scene cannot afford one whole-scene fine octree (its node count, and
 * the node-count-sized voxelize scratch, blow past memory). Instead each bake
 * chunk builds its OWN fine SVO over its outer box from the scene triangles that
 * overlap it, providing the gather's near-hit occupancy + material for that
 * chunk only. Distant geometry is handled by the coarse medium/far SDF fields.
 *
 * Offline (bake) only; no GL. Ownership: @p out_svo is initialised on success
 * and must be released with @ref npc_svo_grid_destroy.
 */
#ifndef FERRUM_LIGHTMAP_LM_CHUNK_SVO_H
#define FERRUM_LIGHTMAP_LM_CHUNK_SVO_H

#include <stdbool.h>

#include "ferrum/lightmap/lm_mesh.h"
#include "ferrum/npc/npc_svo.h"
#include "ferrum/physics/aabb.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build a fine SVO over @p box from every @p scene triangle overlapping
 *        it, stamping occupancy + material. When @p fill_materials is true the
 *        CPU surface subsample also voxelizes textured reflectance / emission /
 *        smooth normals into the leaves; pass false when a GPU material fill
 *        (@ref lm_gpu_chunk_svo_materials) follows, so the expensive CPU pass
 *        is skipped (leaf materials are then zero until filled). The octree
 *        depth is derived from @p voxel over the box's longest extent (clamped
 *        to NPC_SVO_MAX_DEPTH). On success @p out_svo is a ready octree
 *        (destroy with @ref npc_svo_grid_destroy); returns false on init/alloc
 *        failure.
 */
bool lm_chunk_svo_build(const lm_mesh_scene_t *scene, phys_aabb_t box,
                        float voxel, bool fill_materials,
                        npc_svo_grid_t *out_svo);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_LM_CHUNK_SVO_H */
