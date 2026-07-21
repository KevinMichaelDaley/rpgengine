/**
 * @file probe_place.h
 * @brief Headless probe placement from a scene descriptor's probe spec (rpg-ft0g).
 *
 * The default is an auto-generated regular grid (probe_place_grid), reproducing
 * the layout hall_lit_dynamic.c builds from GI_PSPACE/GI_VSPACE. Importance
 * boxes densify chosen regions (probe_place_refine_importance) to realise
 * distance/LOD resolution, and chunk gating (probe_place_filter_chunks) keeps
 * only the probes belonging to resident light-data chunks. No GL, no malloc
 * (all output goes to a caller arena).
 */
#ifndef FERRUM_PROBE_PROBE_PLACE_H
#define FERRUM_PROBE_PROBE_PLACE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/probe/probe_set.h"
#include "ferrum/probe/place/probe_brick.h"
#include "ferrum/scene/scene_desc_probes.h"

struct arena; /* ferrum/memory/arena.h */

/**
 * @brief Build the default regular probe grid over an AABB.
 *
 * Uses @p spec->spacing / @p spec->vspacing (<=0, or @p spec NULL, => engine
 * defaults) and concentrates probes in the lower volume, exactly as
 * hall_lit_dynamic.c does. @p out gets a regular-grid probe_set (grid_dim>0).
 *
 * @return true on success; false on NULL args or arena exhaustion.
 */
bool probe_place_grid(const scene_desc_probes_t *spec, const float aabb_min[3],
                      const float aabb_max[3], struct arena *arena,
                      probe_set_t *out);

/**
 * @brief Refine a base set by adding denser probes inside importance boxes.
 *
 * For each box with density_mult > 1, a finer sub-lattice (base cell / mult,
 * clipped to the box and AABB) is appended -- realising per-region resolution
 * (distance/LOD via importance). The result is an unstructured point set
 * (grid_dim = 0) that includes all base probes plus the refinements. Boxes with
 * density_mult <= 1 are left unchanged. If no box densifies, @p out is a copy of
 * @p base as a point set.
 *
 * @return true on success; false on NULL args or arena exhaustion.
 */
bool probe_place_refine_importance(const probe_set_t *base,
                                   const scene_desc_probes_t *spec,
                                   const float aabb_min[3],
                                   const float aabb_max[3], struct arena *arena,
                                   probe_set_t *out);

/**
 * @brief Keep only probes inside at least one resident chunk box.
 *
 * Chunk gating (rpg-nbp2): a probe belongs to the light-data chunk whose world
 * box contains it, so a probe outside every resident chunk is not loaded. Copies
 * the surviving positions (and baked SH, if present) into @p arena as a point set.
 *
 * @param chunk_min,chunk_max  n_chunks world boxes (3 floats each).
 * @return the number of probes kept (0 if none, or on arena exhaustion).
 */
uint32_t probe_place_filter_chunks(const probe_set_t *set,
                                   const float *chunk_min, const float *chunk_max,
                                   uint32_t n_chunks, struct arena *arena,
                                   probe_set_t *out);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PROBE_PROBE_PLACE_H */
