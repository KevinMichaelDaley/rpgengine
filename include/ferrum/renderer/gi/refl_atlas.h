/**
 * @file refl_atlas.h
 * @brief Reflection-probe atlas layout math (rpg-akwc). The atlas is a
 *        tiles_x x tiles_y grid of octahedral tiles; GL mip level m halves
 *        the whole atlas, so every tile keeps its grid slot at every mip.
 *        Pure math, no allocation, no GL.
 */
#ifndef FERRUM_RENDERER_GI_REFL_ATLAS_H
#define FERRUM_RENDERER_GI_REFL_ATLAS_H

#include <stdbool.h>

#include "ferrum/renderer/gi/refl_probe.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Atlas pixel dimensions at @p mip (tile_res>>mip per tile). Out-of-range
 * mip clamps to the last valid level; NULL args ignored.
 */
void refl_atlas_dims(const refl_probe_set_t *set, uint32_t mip,
                     uint32_t *out_w, uint32_t *out_h);

/**
 * Pixel rect of @p tile at @p mip: top-left (@p out_x, @p out_y) and edge
 * @p out_res. Returns false (outputs untouched) when @p tile is outside the
 * tile grid, @p mip >= set->mips, or @p set is NULL/degenerate.
 */
bool refl_atlas_tile_rect(const refl_probe_set_t *set, uint32_t tile,
                          uint32_t mip, uint32_t *out_x, uint32_t *out_y,
                          uint32_t *out_res);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_GI_REFL_ATLAS_H */
