/**
 * @file lm_sh.h
 * @brief Order-2 (9-coefficient) real spherical harmonics for the lightmap
 *        baker's directional light storage.
 *
 * Each luxel stores incident radiance as three @ref lm_sh9_t (one per RGB
 * channel). The solver projects every incoming-radiance sample into the SH
 * with @ref lm_sh9_add_sample; at runtime the shader reconstructs diffuse
 * irradiance in the (normal-mapped) surface direction with
 * @ref lm_sh9_irradiance, which applies the Ramamoorthi-Hanrahan cosine-lobe
 * convolution coefficients (A0 = pi, A1 = 2pi/3, A2 = pi/4). Nine coefficients
 * capture Lambertian irradiance to ~1% error, so this is enough to combine the
 * bake with a normal map without storing a flat, direction-less value.
 *
 * Single channel per struct; callers hold three for RGB. Ownership: none (POD).
 * Nullability: pointer args must be non-NULL. Directions need not be
 * pre-normalised (normalised internally). Side effects: none beyond the *out.
 */
#ifndef FERRUM_LIGHTMAP_LM_SH_H
#define FERRUM_LIGHTMAP_LM_SH_H

#include "ferrum/math/vec3.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Order-2 real spherical harmonic, one colour channel (9 coefficients). */
typedef struct lm_sh9 {
    float c[9];
} lm_sh9_t;

/** Clear all coefficients to zero. */
void lm_sh9_zero(lm_sh9_t *sh);

/** Evaluate the 9 real SH basis functions at a UNIT direction into out[9]. */
void lm_sh9_basis(vec3_t dir, float out[9]);

/**
 * @brief Project a radiance sample of magnitude @p value arriving from
 *        direction @p dir into the SH, scaled by @p weight (the sample's solid-
 *        angle / Monte-Carlo measure): c_i += value * Y_i(dir) * weight.
 */
void lm_sh9_add_sample(lm_sh9_t *sh, vec3_t dir, float value, float weight);

/**
 * @brief Reconstruct diffuse (cosine-convolved) irradiance in direction
 *        @p normal from the stored incident-radiance SH. Clamp the result to
 *        >= 0 at the call site if a strict non-negative value is required (the
 *        low-order reconstruction can ring slightly negative).
 */
float lm_sh9_irradiance(const lm_sh9_t *sh, vec3_t normal);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_LM_SH_H */
