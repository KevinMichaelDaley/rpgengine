/**
 * @file lm_gi_gather.h
 * @brief Unified path-traced GI gather over mesh luxels.
 *
 * For every luxel this casts a stratified hemisphere of primary rays and path-
 * traces each one through the voxelized scene (@ref lm_svo_voxelize gives every
 * voxel its textured diffuse reflectance + emission). A ray is exact in the
 * NEAR field (hit distance <= @p transition): it reads the hit voxel's material,
 * adds that surface's emission + direct sun (shadow-ray tested), then scatters a
 * cosine bounce and continues -- iterative path tracing. Once a ray's length
 * exceeds the transition it BECOMES A CONE: it reads a coarse pre-filtered
 * ancestor (mip) and terminates. A ray that escapes reads the environment sky.
 * The gathered incident radiance is added into each luxel's SH, so the caller
 * seeds the SH with the luxels' own direct term first.
 *
 * Ownership: writes luxel SH; everything else borrowed. Nullability: pointers
 * non-NULL except @p sky (may be NULL). Offline (bake-time) only.
 */
#ifndef FERRUM_LIGHTMAP_LM_GI_GATHER_H
#define FERRUM_LIGHTMAP_LM_GI_GATHER_H

#include <stdint.h>

#include "ferrum/lightmap/lm_light.h"
#include "ferrum/lightmap/lm_lightmap.h"
#include "ferrum/lightmap/lm_sky.h"
#include "ferrum/npc/npc_svo.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Path-trace one indirect gather into every luxel of @p lm. @p samples
 *        primary rays per luxel (stratified into floor(sqrt) squared); each path
 *        bounces up to @p bounces times in the near field, cones the octree past
 *        @p transition, and reads @p sky on escape (rays bounded by @p maxdist).
 *        @p lights are the analytic lights sampled at each near hit. @p seed
 *        makes the sampling deterministic.
 */
void lm_gi_gather(lm_lightmap_t *lm, const npc_svo_grid_t *svo,
                  const lm_light_t *lights, uint32_t n_lights,
                  const lm_sky_t *sky, const vec3_t *vnormal, float transition,
                  float maxdist, uint32_t samples, uint32_t bounces,
                  uint32_t seed, uint32_t n_threads);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_LM_GI_GATHER_H */
