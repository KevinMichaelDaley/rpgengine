/**
 * @file refl_filter.h
 * @brief Progressive prefilter for octahedral probe tiles (rpg-akwc): each
 *        atlas mip = downsample of the previous + direction-weighted
 *        smoothing passes, approximating GGX prefiltering for increasing
 *        roughness. All four channels (radiance RGB + occlusion A) filter
 *        together. Pure CPU, caller-owned buffers, no allocation.
 */
#ifndef FERRUM_RENDERER_GI_REFL_FILTER_H
#define FERRUM_RENDERER_GI_REFL_FILTER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * One in-place 3x3 smoothing pass over the RGBA32F octahedral image @p img
 * (@p res x @p res). Tap weights follow pow(max(dot(dir_i, dir_c), 0),
 * @p sharpness) so the blur is spherical, not planar; weights renormalise
 * per texel (constant images are invariant). @p tmp is caller scratch of
 * the same size. NULL args / res==0: no-op.
 */
void refl_filter_smooth(float *img, uint32_t res, float sharpness,
                        float *tmp);

/**
 * 2x2 box downsample of RGBA32F @p src (@p sres x @p sres, sres even) into
 * @p dst (sres/2 x sres/2). NULL args / sres<2: no-op.
 */
void refl_filter_downsample(const float *src, uint32_t sres, float *dst);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_GI_REFL_FILTER_H */
