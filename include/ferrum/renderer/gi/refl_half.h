/**
 * @file refl_half.h
 * @brief IEEE-754 half-float conversion for the .rprobe atlas payloads
 *        (rpg-wlh9): streamed per-chunk reflection tiles store RGBA16F on
 *        disk (half the size of f32) and upload raw with GL_HALF_FLOAT.
 *        Pure math, round-to-nearest-even not required (truncation is fine
 *        for radiance); overflow clamps to the max finite half.
 */
#ifndef FERRUM_RENDERER_GI_REFL_HALF_H
#define FERRUM_RENDERER_GI_REFL_HALF_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Encode @p v as IEEE half. NaN -> 0; overflow clamps to 65504. */
uint16_t refl_f32_to_f16(float v);

/** Decode IEEE half @p h to float (subnormals included). */
float refl_f16_to_f32(uint16_t h);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_GI_REFL_HALF_H */
