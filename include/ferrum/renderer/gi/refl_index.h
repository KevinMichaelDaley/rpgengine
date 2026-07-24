/**
 * @file refl_index.h
 * @brief Coarse world-grid probe index for streamed reflection probes
 *        (rpg-wlh9): the fragment shader cannot loop thousands of resident
 *        probes, so residency changes rebuild a cell -> probe-id table
 *        (REFL_INDEX_PER_CELL ids per cell, -1 padded) uploaded as a TBO.
 *        Pure CPU, caller-owned storage.
 */
#ifndef FERRUM_RENDERER_GI_REFL_INDEX_H
#define FERRUM_RENDERER_GI_REFL_INDEX_H

#include "ferrum/renderer/gi/refl_probe.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Probe ids stored per grid cell (nearest wins on overflow). */
#define REFL_INDEX_PER_CELL 4u

/**
 * Build the index over [@p mn, @p mx] at @p cell metres: dims written to
 * @p out_dims (>=1 per axis), cells into caller-owned @p cells
 * (@p cell_capacity cells x REFL_INDEX_PER_CELL i32, -1 padded). A cell
 * keeps the probes NEAREST its centre when more than PER_CELL land in it;
 * probes outside the grid are dropped. Returns the cell count used
 * (0 on NULL args / capacity too small -- nothing written).
 */
uint32_t refl_index_build(const refl_probe_set_t *set, const float mn[3],
                          const float mx[3], float cell, int32_t *cells,
                          uint32_t cell_capacity, int32_t out_dims[3]);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_GI_REFL_INDEX_H */
