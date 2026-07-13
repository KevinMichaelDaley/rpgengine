/**
 * @file lm_direct.h
 * @brief Direct lighting pass: full direct illumination of a lightmap from area
 *        lights and static emissive surfaces.
 *
 * Area lights and emissive materials are both emissive @ref lm_surface patches.
 * For every luxel this integrates the incident radiance from every emitter by
 * stratified Monte-Carlo area sampling: each emitter sample contributes the
 * emitter's radiance, weighted by the emitter-side cosine and inverse-square
 * over its area measure, gated by an SVO shadow ray (@ref lm_visibility_segment).
 * The receiver-side cosine is left to the SH irradiance reconstruction, so the
 * result is accumulated as INCIDENT RADIANCE into each luxel's SH (per channel)
 * -- directional, ready to combine with a normal map.
 *
 * Ownership: none -- writes into @p lm's luxels. Nullability: @p lm and
 * @p emitters non-NULL; @p svo may be NULL (everything mutually visible, no
 * shadows). Errors: none (a 0-area or back-facing emitter simply contributes
 * nothing). Side effects: accumulates into luxel SH (call once per direct pass).
 */
#ifndef FERRUM_LIGHTMAP_LM_DIRECT_H
#define FERRUM_LIGHTMAP_LM_DIRECT_H

#include <stdint.h>

#include "ferrum/lightmap/lm_lightmap.h"
#include "ferrum/lightmap/lm_types.h"
#include "ferrum/npc/npc_svo.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Accumulate direct illumination from @p emitters[0..n_emitters) into
 *        every luxel of @p lm. @p samples is the target emitter sample count
 *        (rounded up to a square for stratification). @p svo (may be NULL) is
 *        used for shadow rays; @p seed makes the stochastic sampling
 *        deterministic.
 */
void lm_direct_bake(lm_lightmap_t *lm, const lm_surface_t *emitters,
                    uint32_t n_emitters, const npc_svo_grid_t *svo,
                    uint32_t samples, uint32_t seed);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_LM_DIRECT_H */
