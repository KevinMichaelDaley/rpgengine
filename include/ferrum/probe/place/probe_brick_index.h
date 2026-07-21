/**
 * @file probe_brick_index.h
 * @brief Dense voxel -> brick lookup for brick-placed probes (rpg-pjkb,
 *        feature 3, headless half).
 *
 * The runtime indirection of the survey plan (ref/probe_placement_survey.md):
 * a voxel grid at FINEST-brick granularity over the placement AABB where each
 * voxel stores the id of the brick shading it, finer bricks pre-splatted over
 * coarser ones. Sampling is then one fetch (voxel -> brick -> 8 of its 64
 * probes by local trilinear cell) with no hierarchy walk. The flat arrays are
 * upload-ready for an SSBO; the GL binding lives with the renderer, not here.
 *
 * Ownership: @c brick_of is carved from the caller's arena (nothing to free).
 * Error semantics: false on NULL cfg/arena/out, invalid cfg fields, or arena
 * exhaustion; @p out untouched on failure. No side effects beyond the arena.
 */
#ifndef FERRUM_PROBE_PLACE_PROBE_BRICK_INDEX_H
#define FERRUM_PROBE_PLACE_PROBE_BRICK_INDEX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/probe/place/probe_brick.h"

struct arena; /* ferrum/memory/arena.h */

/**
 * @brief The lookup grid. Voxel (x,y,z) of world point p:
 *        v = floor((p - origin) / voxel), id = brick_of[(z*dim.y + y)*dim.x + x],
 *        -1 = uncovered (fall back to whatever the sampler's default is).
 */
typedef struct probe_brick_index {
    int32_t *brick_of;   /**< [dim.x*dim.y*dim.z] brick id per voxel, -1 = none. */
    int32_t  dim[3];     /**< voxel counts per axis. */
    float    origin[3];  /**< world position of voxel (0,0,0)'s min corner. */
    float    voxel;      /**< voxel edge = finest brick size (m). */
} probe_brick_index_t;

/**
 * @brief Build the index from a placement result.
 *
 * @param cfg     the SAME config the bricks were placed with (grid geometry).
 * @param bricks  bricks from probe_brick_place, in its DFS emit order --
 *                ancestors precede descendants, so in-order splatting makes
 *                the finest covering brick win (overlaps only occur along
 *                ancestor chains). NULL iff @p n_bricks is 0.
 * @param arena   backing for the voxel array.
 * @param out     receives the index.
 * @return true on success; false on NULL cfg/arena/out, cfg->levels outside
 *         [1, PROBE_BRICK_MAX_LEVELS], cfg->coarse_brick <= 0, or arena
 *         exhaustion.
 */
bool probe_brick_index_build(const probe_brick_config_t *cfg,
                             const probe_brick_t *bricks, uint32_t n_bricks,
                             struct arena *arena, probe_brick_index_t *out);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PROBE_PLACE_PROBE_BRICK_INDEX_H */
