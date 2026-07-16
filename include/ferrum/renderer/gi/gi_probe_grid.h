/**
 * @file gi_probe_grid.h
 * @brief Coarse uniform accel grid for nearest-probe lookup over an ADAPTIVE
 *        probe set (rpg-qthg).
 *
 * The probes themselves are adaptively placed (@ref gi_probe_set); this is only
 * a lookup structure. A uniform grid over an AABB bins each probe into a cell
 * (CSR layout: @c cell_start prefix sums + a @c probe_idx list sorted by cell),
 * so a shader/CPU query can gather the handful of probes in a point's cell and
 * its neighbours and distance-weight their SH -- without scanning every probe.
 *
 * Storage is caller-provided so the grid uploads as two SSBOs. Ownership: the
 * probe set and both backing arrays are borrowed. Build is O(probes + cells).
 */
#ifndef FERRUM_RENDERER_GI_GI_PROBE_GRID_H
#define FERRUM_RENDERER_GI_GI_PROBE_GRID_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/renderer/gi/gi_probe_set.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A uniform bin grid indexing an adaptive probe set (CSR cell -> probes). */
typedef struct gi_probe_grid {
    float     origin[3];   /**< Grid minimum corner (world). */
    float     cell_size;   /**< Cell edge (m). */
    int32_t   dims[3];     /**< Cells per axis (>= 1). */
    uint32_t  ncells;      /**< dims[0]*dims[1]*dims[2]. */
    uint32_t *cell_start;  /**< ncells+1 prefix sums (borrowed). */
    uint32_t *probe_idx;   /**< set->count probe indices, sorted by cell (borrowed). */
} gi_probe_grid_t;

/**
 * @brief Bin @p set's probes into a uniform grid spanning [@p aabb_min,
 *        @p aabb_max] at @p cell_size. @p cell_start_backing must hold
 *        >= ncells+1 uints, @p probe_idx_backing >= set->count uints. Returns
 *        false on a NULL/degenerate argument or if a backing array is too small.
 */
bool gi_probe_grid_build(gi_probe_grid_t *grid, const gi_probe_set_t *set,
                         const float aabb_min[3], const float aabb_max[3],
                         float cell_size, uint32_t *cell_start_backing,
                         uint32_t cell_start_cap, uint32_t *probe_idx_backing,
                         uint32_t probe_idx_cap);

/** @brief Linear cell index containing (@p x,@p y,@p z), clamped to the grid. */
uint32_t gi_probe_grid_cell(const gi_probe_grid_t *grid, float x, float y, float z);

/**
 * @brief Gather the probe indices in the cell containing (@p x,@p y,@p z) and its
 *        26 neighbours into @p out (up to @p cap). Returns how many were written
 *        (<= cap). The caller distance-weights the returned probes' SH.
 */
uint32_t gi_probe_grid_gather(const gi_probe_grid_t *grid, float x, float y,
                              float z, uint32_t *out, uint32_t cap);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_GI_GI_PROBE_GRID_H */
