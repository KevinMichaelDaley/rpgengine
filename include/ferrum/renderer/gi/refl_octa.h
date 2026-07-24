/**
 * @file refl_octa.h
 * @brief Octahedral direction <-> uv mapping + cube-face resampling for the
 *        reflection-probe atlas (rpg-akwc). Pure math, no allocation.
 *
 * The octahedral parameterisation folds the unit sphere onto the [0,1]^2
 * square (lower hemisphere folded outward), so a whole probe fits one 2D
 * atlas tile with cheap bilinear sampling.
 */
#ifndef FERRUM_RENDERER_GI_REFL_OCTA_H
#define FERRUM_RENDERER_GI_REFL_OCTA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Encode unit direction @p dir into octahedral uv in [0,1]^2. A zero /
 * denormal direction encodes to the +z centre (never NaN). NULL args are
 * ignored (uv untouched).
 */
void refl_octa_encode(const float dir[3], float uv[2]);

/**
 * Decode octahedral @p uv (any values; clamped to [0,1]) back to a unit
 * direction. NULL args are ignored.
 */
void refl_octa_decode(const float uv[2], float dir[3]);

/**
 * Resample six RGBA32F cube faces (GL order +x -x +y -y +z -z, each
 * @p face_res x @p face_res, row-major from the conventional GL face
 * orientation) into a @p res x @p res RGBA32F octahedral image @p dst.
 * Bilinear taps on the source face selected per texel direction. All four
 * channels resampled. NULL faces/dst or zero sizes: no-op.
 */
void refl_octa_from_cube(const float *const faces[6], uint32_t face_res,
                         float *dst, uint32_t res);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_GI_REFL_OCTA_H */
