/**
 * @file lm_light.h
 * @brief Analytic (delta) light sources -- point, directional, spot -- for the
 *        lightmap baker's indirect pass.
 *
 * These lights are baked as INDIRECT only: their direct term is evaluated at
 * runtime, so the baker uses them to seed the first-bounce radiosity (their
 * direct illumination reflects off surfaces and the bounces are stored) but
 * does NOT add their direct contribution to the lightmap. Area lights and
 * static emissive materials, by contrast, are full-direct and are modeled as
 * emissive @ref lm_surface patches handled by the direct pass.
 *
 * @ref lm_light_incident returns, for a shading point, the direction to the
 * light, the distance to use for the shadow ray, and the unshadowed irradiance
 * arriving ALONG that direction (the receiver applies its own cosine term). It
 * returns false when the light cannot reach the point (beyond range / outside
 * the spot cone), so the caller can skip the shadow ray.
 *
 * POD; no ownership. Directions are unit, world space.
 */
#ifndef FERRUM_LIGHTMAP_LM_LIGHT_H
#define FERRUM_LIGHTMAP_LM_LIGHT_H

#include <stdbool.h>

#include "ferrum/math/vec3.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Analytic light source kind. */
typedef enum lm_light_kind {
    LM_LIGHT_POINT = 0,      /**< Omni point light (inverse-square). */
    LM_LIGHT_DIRECTIONAL,    /**< Parallel rays from infinity (no falloff). */
    LM_LIGHT_SPOT            /**< Point light restricted to a cone. */
} lm_light_kind_t;

/** An analytic light. Fields used depend on @ref kind. */
typedef struct lm_light {
    lm_light_kind_t kind;
    vec3_t position;   /**< Point/spot world position. */
    vec3_t direction;  /**< Directional/spot emission direction (unit, points away from the light). */
    vec3_t color;      /**< Radiant intensity (point/spot) or irradiance (directional), per channel. */
    float  range;      /**< Point/spot cutoff distance; <= 0 means unbounded. */
    float  cos_inner;  /**< Spot: cosine of the inner (full-bright) cone half-angle. */
    float  cos_outer;  /**< Spot: cosine of the outer (zero) cone half-angle. */
} lm_light_t;

/**
 * @brief Incident illumination of @p light at world @p point. On success fills
 *        @p out_dir (unit direction from the point toward the light), @p out_dist
 *        (shadow-ray length; a large finite value for directional), and
 *        @p out_irradiance (unshadowed irradiance along the ray). Returns false
 *        (and touches nothing) when the light does not reach the point.
 */
bool lm_light_incident(const lm_light_t *light, vec3_t point, vec3_t *out_dir,
                       float *out_dist, vec3_t *out_irradiance);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_LM_LIGHT_H */
