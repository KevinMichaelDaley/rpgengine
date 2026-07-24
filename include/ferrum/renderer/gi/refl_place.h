/**
 * @file refl_place.h
 * @brief Sparse reflection-probe placement (rpg-akwc): a uniform grid of
 *        cell centres over the scene AABB, culled by the baked SDF so no
 *        probe sits inside or hugging geometry. Pure CPU, no allocation.
 */
#ifndef FERRUM_RENDERER_GI_REFL_PLACE_H
#define FERRUM_RENDERER_GI_REFL_PLACE_H

#include "ferrum/renderer/gi/refl_occl.h"
#include "ferrum/renderer/gi/refl_probe.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Append grid probes to @p set: cell centres of a @p spacing-metre grid over
 * [@p mn, @p mx]. When @p sdf_dist is non-NULL (layout as
 * @ref gi_sdf_baked_sample: @p dims / @p origin / @p voxel), a probe is kept
 * only where the sampled distance exceeds @p min_clear (in air, clear of
 * walls); NULL @p sdf_dist keeps every centre. Tiles are assigned in append
 * order; @c ao initialises to 1. Stops silently at capacity.
 *
 * @return probes appended. 0 on NULL set / degenerate box / zero spacing.
 */
/**
 * As @ref refl_place_grid but culling through sample callback @p fn (NULL
 * fn keeps every centre). @p near_max > 0 additionally prunes centres whose
 * distance exceeds it (far from all geometry -- open sky), which keeps
 * big open-world grids inside the probe cap. Used with the chunked-SDF
 * sampler at bake time.
 */
uint32_t refl_place_grid_fn(refl_probe_set_t *set, const float mn[3],
                            const float mx[3], float spacing,
                            refl_sdf_fn fn, void *user, float min_clear,
                            float near_max);

uint32_t refl_place_grid(refl_probe_set_t *set, const float mn[3],
                         const float mx[3], float spacing,
                         const float *sdf_dist, const int32_t dims[3],
                         const float origin[3], float voxel,
                         float min_clear);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_GI_REFL_PLACE_H */
