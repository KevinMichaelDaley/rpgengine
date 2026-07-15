/**
 * @file lm_gpu_gather.h
 * @brief GPU path of the GI gather (rpg-k4lk): a drop-in for @ref lm_gi_gather
 *        that runs the whole path-traced gather on the GPU.
 *
 * Ties the three validated compute stages together: rasterise the SVO occupancy
 * to a coarse dense grid, build a signed distance field by Jump Flood, pack the
 * SVO nodes + luxels + lights into SSBOs, and dispatch the gather kernel
 * (sphere-trace the SDF, refine into the SVO for material, multi-bounce, all
 * analytic light types + emissive voxels), reading the per-luxel SH9 back into
 * @p accum. Matches the CPU convention (uniform-hemisphere weight 2pi/n^2, Y
 * basis) so results are parity-comparable within Monte-Carlo tolerance.
 *
 * GL: needs a current GL 4.3+ (compute) context. The lightmap library is
 * headless, so GL is loaded via the caller's @ref gl_loader_t rather than glad.
 * State is file-static (offline, single-threaded bake use).
 */
#ifndef FERRUM_LIGHTMAP_GPU_LM_GPU_GATHER_H
#define FERRUM_LIGHTMAP_GPU_LM_GPU_GATHER_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/lightmap/lm_light.h"
#include "ferrum/lightmap/lm_lightmap.h"
#include "ferrum/lightmap/lm_mesh.h"
#include "ferrum/lightmap/lm_sh.h"
#include "ferrum/lightmap/lm_sky.h"
#include "ferrum/npc/npc_svo.h"
#include "ferrum/renderer/gl_loader.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Load GL 4.3 entry points via @p loader and compile the JFA + gather
 *        compute programs. Requires a current compute-capable context. Returns
 *        false on a missing entry point or a shader compile/link failure.
 */
bool lm_gpu_gather_init(const gl_loader_t *loader);

/** @brief Release the compute programs. NULL-safe. */
void lm_gpu_gather_shutdown(void);

/**
 * @brief Run the full GPU gather over @p lm's luxels and write the per-luxel
 *        SH9 estimate into @p accum (3 coeff-sets per luxel, laid out as the CPU
 *        gather's accum). @p samples is the total per-luxel sample count for
 *        this call (stratified n x n, n = floor(sqrt(samples))). Returns false
 *        on any GL/allocation failure or if @ref lm_gpu_gather_init was not run.
 */
bool lm_gpu_gather_run(const lm_lightmap_t *lm, lm_sh9_t *accum,
                       const npc_svo_grid_t *svo, const lm_mesh_scene_t *scene,
                       const lm_light_t *lights,
                       uint32_t n_lights, const lm_sky_t *sky, float transition,
                       float maxdist, uint32_t samples, uint32_t bounces,
                       uint32_t seed);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_GPU_LM_GPU_GATHER_H */
