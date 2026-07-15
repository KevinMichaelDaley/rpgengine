/**
 * @file chunk_grid.h
 * @brief Uniform grid partition of a scene AABB into cubic CHUNKS with an overlap
 *        margin (rpg-fzht). Each chunk owns a near SDF/SVO + luxel set; the margin
 *        lets neighbouring chunks' fields be sampled across boundaries without a
 *        seam. This is pure geometry (no GL): the offline baker and the runtime
 *        streamer share it to decide which chunks a mesh/luxel/query touches.
 *
 * Ownership: @ref chunk_grid_t is a value type, no heap. Nullability: pointer
 * args are required. Error semantics: init returns false on a NULL grid, a
 * non-positive chunk size, or inverted bounds. Chunks tile uniformly from the
 * bounds minimum; the final chunk on each axis may extend past the bounds max
 * (empty space), which is fine for sampling.
 */
#ifndef FERRUM_RENDERER_CHUNK_CHUNK_GRID_H
#define FERRUM_RENDERER_CHUNK_CHUNK_GRID_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/physics/aabb.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A uniform cubic-chunk partition of a scene AABB. */
typedef struct chunk_grid {
    float    min[3];      /**< Scene AABB minimum corner (grid origin). */
    float    chunk_size;  /**< Edge length of each cubic chunk. */
    float    margin;      /**< Overlap added to every side of a chunk's outer box. */
    int      dims[3];     /**< Chunk count per axis (>= 1). */
} chunk_grid_t;

/**
 * @brief Partition @p bounds into cubic chunks of @p chunk_size, each with a
 *        @p margin overlap on its outer box. Returns false on a NULL grid,
 *        chunk_size <= 0, or a min > max on any axis.
 */
bool chunk_grid_init(chunk_grid_t *g, phys_aabb_t bounds, float chunk_size, float margin);

/** @brief Total chunk count (product of dims). */
uint32_t chunk_grid_count(const chunk_grid_t *g);

/** @brief Linear index of chunk (i,j,k). Caller ensures the coords are in range. */
uint32_t chunk_grid_index(const chunk_grid_t *g, int i, int j, int k);

/** @brief Decompose a linear @p index back into @p out_ijk[3]. */
void chunk_grid_ijk(const chunk_grid_t *g, uint32_t index, int out_ijk[3]);

/**
 * @brief Linear index of the chunk whose INNER cell contains world point
 *        (@p x,@p y,@p z), or UINT32_MAX if the point is outside the grid.
 */
uint32_t chunk_grid_of_point(const chunk_grid_t *g, float x, float y, float z);

/**
 * @brief Inner (no-margin) and outer (margin-expanded) AABBs of chunk @p index.
 *        Either out-pointer may be NULL.
 */
void chunk_grid_bounds(const chunk_grid_t *g, uint32_t index,
                       phys_aabb_t *inner, phys_aabb_t *outer);

/** @brief True if @p box intersects chunk @p index's OUTER (margin) box. */
bool chunk_grid_overlaps_aabb(const chunk_grid_t *g, uint32_t index, phys_aabb_t box);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_CHUNK_CHUNK_GRID_H */
