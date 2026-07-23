/**
 * @file lm_gpu_chunk_mat.h
 * @brief GPU material fill for a chunk SVO (rpg-bpiz): replaces the CPU
 *        surface-subsampling pass (@ref lm_svo_voxelize) when a GL 4.3 context
 *        is current, using the GPU voxel rasterizer's dense grid.
 *
 * Ownership: fills @p svo's leaf nodes in place (diffuse + emissive) and mips
 * the octree; allocates only transient scratch. Error semantics: returns false
 * (leaving the SVO's materials untouched) when the rasterizer is unavailable
 * or fails -- the caller falls back to the CPU pass. Side effects: prints the
 * same voxelize coverage statistics line as the CPU pass.
 */
#ifndef FERRUM_LIGHTMAP_GPU_LM_GPU_CHUNK_MAT_H
#define FERRUM_LIGHTMAP_GPU_LM_GPU_CHUNK_MAT_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/lightmap/lm_mesh.h"
#include "ferrum/npc/npc_svo.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Voxelize @p meshes on the GPU over @p svo's bounds at its leaf
 *        resolution and write diffuse/emissive into every SOLID leaf (neutral
 *        0.5 reflectance where no surface sample landed, matching the CPU
 *        pass), then average the mips. @p svo must be a built grid (solid
 *        flags stamped). Returns false on NULL args or rasterizer failure.
 */
bool lm_gpu_chunk_svo_materials(npc_svo_grid_t *svo, const lm_mesh_t *meshes,
                                uint32_t n_meshes);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_GPU_LM_GPU_CHUNK_MAT_H */
