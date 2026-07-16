/**
 * @file gi_probe_place.h
 * @brief Probe placement seeding for the adaptive probe set (rpg-qthg).
 *
 * The probes are stored by explicit position (not a grid), but they still need
 * to be seeded somewhere. @ref gi_probe_seed_box is the simple starting strategy:
 * fill a box (the play volume) with probes at a lattice of cell CENTRES. More
 * adaptive strategies (concentrate near surfaces, prune empty space) can be
 * layered on later -- they just add/remove positions in the same set.
 *
 * Ownership: none. No allocation.
 */
#ifndef FERRUM_RENDERER_GI_GI_PROBE_PLACE_H
#define FERRUM_RENDERER_GI_GI_PROBE_PLACE_H

#include <stdint.h>

#include "ferrum/renderer/gi/gi_probe_set.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Seed @p set with probes at the cell centres of a @p spacing lattice
 *        filling [@p aabb_min, @p aabb_max] (n = floor(span/spacing) per axis).
 *        Appends to whatever is already in the set; stops early if it fills.
 *        Returns the number of probes added.
 */
uint32_t gi_probe_seed_box(gi_probe_set_t *set, const float aabb_min[3],
                           const float aabb_max[3], float spacing);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_GI_GI_PROBE_PLACE_H */
