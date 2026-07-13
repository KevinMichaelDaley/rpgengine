/**
 * @file lm_sky.h
 * @brief Environment sky sampled by far-field rays that escape the scene.
 *
 * A far-field gather ray that hits nothing leaves the scene and sees the sky.
 * The sky is a distant light source: it contributes ambient skylight that then
 * bounces through the radiosity solve (so an interior lit only through openings
 * still fills in). The sky is either a uniform colour or an equirectangular HDRI
 * environment map; in the HDRI case @ref lm_sky.color acts as an exposure gain.
 *
 * Directions use a Y-up equirectangular convention: longitude from atan2(x, z)
 * (rotated by @ref lm_sky.yaw about +Y), latitude from asin(y). The top of the
 * image (v = 0) is the zenith (+Y).
 *
 * Ownership: the HDRI image is borrowed. Nullability: @ref lm_sky_sample is
 * NULL-safe (returns black). Offline (bake-time) use only.
 */
#ifndef FERRUM_LIGHTMAP_LM_SKY_H
#define FERRUM_LIGHTMAP_LM_SKY_H

#include "ferrum/lightmap/lm_image.h"
#include "ferrum/math/vec3.h"

#ifdef __cplusplus
extern "C" {
#endif

/** How an @ref lm_sky produces radiance for an escaping direction. */
typedef enum lm_sky_kind {
    LM_SKY_CONSTANT = 0, /**< Uniform @ref lm_sky.color in every direction. */
    LM_SKY_HDRI     = 1, /**< Equirectangular env map, tinted by color (gain). */
} lm_sky_kind_t;

/** A bake-time environment light sampled by escaping far-field rays. */
typedef struct lm_sky {
    lm_sky_kind_t     kind;  /**< Constant colour vs. HDRI. */
    vec3_t            color; /**< Sky radiance (CONSTANT) or HDRI gain (HDRI). */
    const lm_image_t *hdri;  /**< Equirectangular env map (LM_SKY_HDRI only). */
    float             yaw;   /**< HDRI rotation about +Y, radians. */
} lm_sky_t;

/**
 * @brief Radiance seen along @p dir (need not be normalised).
 * @return The uniform colour, or the HDRI sample times the gain. Black if
 *         @p sky is NULL; falls back to @ref lm_sky.color if kind is HDRI but
 *         the image is missing.
 */
vec3_t lm_sky_sample(const lm_sky_t *sky, vec3_t dir);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_LM_SKY_H */
