/**
 * @file lm_farfield.h
 * @brief Far-field gather: distant reflectors queried directly from the SVO.
 *
 * The progressive solver (@ref lm_solve) only shoots between near-field luxels
 * found via the kd-tree; geometry beyond that radius -- and geometry that lives
 * only in the SVO, with no luxels of its own -- is gathered here instead. Each
 * luxel traces a hemisphere of rays about its normal into the SVO; a ray that
 * enters a SOLID voxel past @p near_radius reads that voxel's emissive term from
 * the material table (@ref lm_material_lookup) and deposits it as distant
 * incident radiance into the luxel's SH. Hits closer than @p near_radius are
 * ignored so the near-field solver's contribution is not double-counted. A ray
 * that escapes the scene entirely (no hit) samples the environment @p sky, so
 * skylight enters through openings and then bounces through the solver.
 *
 * Ownership: none -- writes into @p lm's luxel SH; @p sky borrowed. Nullability:
 * @p lm, @p svo, @p table non-NULL; @p sky may be NULL (no sky). Errors: none.
 * Side effects: accumulates into luxel SH (run once, after/alongside the direct
 * pass; the solver reads the seeded SH).
 */
#ifndef FERRUM_LIGHTMAP_LM_FARFIELD_H
#define FERRUM_LIGHTMAP_LM_FARFIELD_H

#include <stdint.h>

#include "ferrum/lightmap/lm_lightmap.h"
#include "ferrum/lightmap/lm_material.h"
#include "ferrum/lightmap/lm_sky.h"
#include "ferrum/npc/npc_svo.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Gather far-field incident radiance into every luxel of @p lm by
 *        tracing @p samples hemisphere rays per luxel into @p svo and shading
 *        each far hit (t > @p near_radius) with its material's emissive from
 *        @p table. Rays that escape sample @p sky (may be NULL). @p max_dist
 *        bounds the ray length; @p seed makes the stochastic hemisphere sampling
 *        deterministic.
 */
void lm_farfield_gather(lm_lightmap_t *lm, const npc_svo_grid_t *svo,
                        const lm_material_table_t *table, const lm_sky_t *sky,
                        uint32_t samples, float near_radius, float max_dist,
                        uint32_t seed);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_LM_FARFIELD_H */
