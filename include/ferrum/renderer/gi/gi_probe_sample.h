/**
 * @file gi_probe_sample.h
 * @brief Nearest-probe SH sampler for the forward+ material (rpg-q82b).
 *
 * The CPU reference the forward+ shader mirrors: gather the probes near a world
 * point (via @ref gi_probe_grid), inverse-distance-weight their SH9, and
 * reconstruct cosine irradiance for a surface normal -- the dynamic-light
 * indirect term added on top of the baked static lightmap. Because the probes
 * are adaptive (no grid), the blend is a distance weight over the gathered
 * candidates rather than a trilinear corner interpolation.
 *
 * No allocation (a bounded on-stack candidate buffer). Ownership: none.
 */
#ifndef FERRUM_RENDERER_GI_GI_PROBE_SAMPLE_H
#define FERRUM_RENDERER_GI_GI_PROBE_SAMPLE_H

#include <stdbool.h>

#include "ferrum/renderer/gi/gi_probe_grid.h"
#include "ferrum/renderer/gi/gi_probe_set.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reconstruct the dynamic indirect irradiance at world point @p p for
 *        surface normal @p n into @p out_rgb, by inverse-distance blending the SH
 *        of the probes near @p p and cosine-reconstructing per channel. Returns
 *        false (and zeroes @p out_rgb) when no probe is near @p p.
 */
bool gi_probe_sample(const gi_probe_set_t *set, const gi_probe_grid_t *grid,
                     const float p[3], const float n[3], float out_rgb[3]);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_GI_GI_PROBE_SAMPLE_H */
