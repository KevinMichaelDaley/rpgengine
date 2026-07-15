/**
 * @file lm_gpu_pack.h
 * @brief Pack the offline lightmap scene (SVO nodes, luxels, lights, params)
 *        into std430 GPU buffers for the compute-shader gather. Pure host-side
 *        repacking -- no GL, no allocation (caller provides the buffers).
 *
 * The SVO's node pool is already a flat, index-based array (rpg-8je5), so
 * packing is a strided copy that drops the nav-only fields (parent/occupancy/
 * flags) and keeps only what the gather reads: children + the diffuse/emissive
 * shading pyramid.
 */
#ifndef FERRUM_LIGHTMAP_GPU_LM_GPU_PACK_H
#define FERRUM_LIGHTMAP_GPU_LM_GPU_PACK_H

#include <stdint.h>

#include "ferrum/lightmap/gpu/lm_gpu_buffers.h"
#include "ferrum/lightmap/gpu/lm_gpu_scene.h"
#include "ferrum/lightmap/lm_light.h"
#include "ferrum/lightmap/lm_lightmap.h"
#include "ferrum/npc/npc_svo.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Pack @p svo's node pool into @p out. Returns the node count. If
 *        @p out_cap < node_count nothing is written (query the size, then
 *        re-call). Returns 0 for NULL @p svo.
 */
uint32_t lm_gpu_pack_nodes(const npc_svo_grid_t *svo, lm_gpu_node_t *out,
                           uint32_t out_cap);

/**
 * @brief Pack @p lm's luxels (res_u*res_v world pos + normal) into @p out.
 *        Returns the luxel count; nothing written if @p out_cap is too small.
 */
uint32_t lm_gpu_pack_luxels(const lm_lightmap_t *lm, lm_gpu_luxel_t *out,
                            uint32_t out_cap);

/**
 * @brief Pack @p n analytic @p lights into @p out. Returns the count; nothing
 *        written if @p out_cap is too small.
 */
uint32_t lm_gpu_pack_lights(const lm_light_t *lights, uint32_t n,
                            lm_gpu_light_t *out, uint32_t out_cap);

/**
 * @brief Fill @p out with the grid + gather config (bounds, voxel_size,
 *        transition, maxdist, bounces, and the buffer counts). NULL-safe.
 */
void lm_gpu_pack_params(const npc_svo_grid_t *svo, uint32_t n_luxels,
                        uint32_t n_lights, float transition, float maxdist,
                        uint32_t bounces, lm_gpu_params_t *out);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_GPU_LM_GPU_PACK_H */
