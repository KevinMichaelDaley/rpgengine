/**
 * @file lm_image.h
 * @brief A CPU-side 2D image the baker samples for per-texel material values.
 *
 * The baker reads diffuse reflectance (albedo) and emissive colour from the
 * material's textures per luxel, so it needs to sample those images on the CPU.
 * lm_image wraps an 8-bit RGB/RGBA buffer; @ref lm_image_sample does a wrapped
 * bilinear lookup returning a linear-space colour (sRGB images are decoded).
 *
 * Ownership: pixels are borrowed. Nullability: pixels non-NULL when width/height
 * > 0.
 */
#ifndef FERRUM_LIGHTMAP_LM_IMAGE_H
#define FERRUM_LIGHTMAP_LM_IMAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/math/vec3.h"

#ifdef __cplusplus
extern "C" {
#endif

/** An 8-bit CPU image. */
typedef struct lm_image {
    const uint8_t *pixels;  /**< width*height*channels, row-major. */
    uint32_t       width;
    uint32_t       height;
    uint32_t       channels;/**< 3 (RGB) or 4 (RGBA). */
    bool           srgb;    /**< decode sRGB -> linear on sample. */
} lm_image_t;

/**
 * @brief Wrapped bilinear sample at UV @p u,@p v, returned as linear RGB.
 *        Returns (0,0,0) if @p img or its pixels are NULL.
 */
vec3_t lm_image_sample(const lm_image_t *img, float u, float v);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_LM_IMAGE_H */
