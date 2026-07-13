/**
 * @file lm_indirect.h
 * @brief Indirect-only injection of analytic lights (point / directional /
 *        spot) into the radiosity solve.
 *
 * Regular analytic lights contribute ONLY indirect light to the bake -- the
 * runtime handles their dynamic direct term. This pass computes, per luxel, the
 * unshadowed-then-SVO-shadowed direct irradiance those lights deposit on the
 * surface (cosine-weighted, summed over all lights). The result is written to a
 * caller-owned per-luxel RGB buffer and is NOT added to any luxel's SH: it is
 * the first-bounce seed the radiosity solver multiplies by albedo and shoots,
 * so only the resulting *indirect* bounces end up baked.
 *
 * Ownership: none -- reads @p lm, writes @p out_irradiance. Nullability: @p lm
 * and @p lights non-NULL; @p svo may be NULL (no shadowing). Errors: none.
 * Side effects: overwrites @p out_irradiance (3 floats per luxel, row-major).
 */
#ifndef FERRUM_LIGHTMAP_LM_INDIRECT_H
#define FERRUM_LIGHTMAP_LM_INDIRECT_H

#include <stdint.h>

#include "ferrum/lightmap/lm_light.h"
#include "ferrum/lightmap/lm_lightmap.h"
#include "ferrum/npc/npc_svo.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Compute per-luxel direct irradiance from @p lights[0..n_lights) into
 *        @p out_irradiance (must hold res_u*res_v*3 floats). Each luxel sums,
 *        over every light, the light's incident irradiance times the surface
 *        cosine, gated by an SVO shadow ray toward the light. This is the
 *        solver's indirect seed -- callers must NOT add it to the SH directly.
 */
void lm_indirect_direct_irradiance(const lm_lightmap_t *lm,
                                   const lm_light_t *lights, uint32_t n_lights,
                                   const npc_svo_grid_t *svo,
                                   float *out_irradiance);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_LM_INDIRECT_H */
