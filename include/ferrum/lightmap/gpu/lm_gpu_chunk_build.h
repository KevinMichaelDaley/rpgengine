/**
 * @file lm_gpu_chunk_build.h
 * @brief GPU chunk-SVO build (rpg-bpiz): the whole per-chunk octree --
 *        occupancy AND materials -- comes from the GPU voxelization, replacing
 *        both the CPU triangle stamping and the CPU surface subsample.
 *
 * The chunk box is rasterized in cubic full-resolution tiles with the
 * sliced-render-target pipeline; a compact compute pass composes the three
 * axis passes and appends one sparse record per covered cell (cell coords,
 * area, albedo sum, emissive sum), so the readback scales with the octree's
 * LEAF COUNT, never the dense volume. The CPU then descend-inserts the leaf
 * records into a fresh npc_svo grid (interior nodes + occupancy masks
 * materialize on the way, exactly as the triangle rasterizer builds them)
 * and mips the material pyramid.
 *
 * Ownership: on success @p out_svo is a ready octree (release with
 * npc_svo_grid_destroy). Error semantics: false on NULL args, rasterizer
 * unavailable, or GL/allocation failure -- @p out_svo is destroyed/zeroed,
 * and the caller falls back to the CPU build. Side effects: prints the same
 * voxelize coverage statistics line as the CPU pass.
 */
#ifndef FERRUM_LIGHTMAP_GPU_LM_GPU_CHUNK_BUILD_H
#define FERRUM_LIGHTMAP_GPU_LM_GPU_CHUNK_BUILD_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/lightmap/lm_mesh.h"
#include "ferrum/npc/npc_svo.h"
#include "ferrum/physics/aabb.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build a fine chunk SVO over @p box from @p scene's overlapping
 *        meshes at the leaf size nearest @p voxel (the same depth derivation
 *        as @ref lm_chunk_svo_build), entirely from the GPU voxelization.
 */
bool lm_gpu_chunk_svo_build(const lm_mesh_scene_t *scene, phys_aabb_t box,
                            float voxel, npc_svo_grid_t *out_svo);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_GPU_LM_GPU_CHUNK_BUILD_H */
