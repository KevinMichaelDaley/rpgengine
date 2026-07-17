#ifndef FERRUM_RENDERER_CLUSTER_GRID_H
#define FERRUM_RENDERER_CLUSTER_GRID_H

#include <stdint.h>

#include "ferrum/renderer/light.h"
#include "ferrum/renderer/render_camera.h"

/** @file
 * @brief Froxel/cluster light culling for clustered forward+ shading.
 *
 * Divides the view frustum into a tiles_x * tiles_y * slices grid of clusters
 * (screen tiles by depth slices, depth split logarithmically) and assigns each
 * light to the clusters its bounding sphere overlaps, producing a per-cluster
 * (offset, count) into a flat light-index list. The forward+ pass looks up a
 * fragment's cluster and shades only that cluster's lights.
 *
 * All storage is caller-provided (offsets/counts sized tiles_x*tiles_y*slices,
 * indices sized to a chosen capacity); no internal allocation. Directional
 * lights affect every cluster; point/spot are culled by their range sphere;
 * area/non-realtime lights are ignored.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Cluster grid dimensions + depth range. */
typedef struct cluster_config {
    uint32_t tiles_x;   /**< screen tiles across (>= 1). */
    uint32_t tiles_y;   /**< screen tiles down (>= 1). */
    uint32_t slices;    /**< depth slices (>= 1). */
    float    near_plane;/**< near depth for the log split (> 0). */
    float    far_plane; /**< far depth for the log split (> near). */
} cluster_config_t;

/** A built cluster grid over caller-provided storage. */
typedef struct cluster_grid {
    cluster_config_t config;
    uint32_t *offsets;        /**< [cluster_total] start into indices. */
    uint32_t *counts;         /**< [cluster_total] light count per cluster. */
    uint32_t *indices;        /**< [index_capacity] flat light indices. */
    uint32_t  cluster_total;  /**< tiles_x*tiles_y*slices. */
    uint32_t  index_capacity; /**< capacity of indices. */
    uint32_t  index_count;    /**< indices actually used after a build. */
} cluster_grid_t;

/**
 * @brief Initialise the grid over caller storage (offsets/counts must hold
 *        tiles_x*tiles_y*slices entries; indices holds index_capacity).
 */
void cluster_grid_init(cluster_grid_t *grid, cluster_config_t config,
                       uint32_t *offsets, uint32_t *counts, uint32_t *indices,
                       uint32_t index_capacity);

/**
 * @brief Assign @p lights[0..n_lights) to the clusters they overlap for
 *        @p camera, filling offsets/counts/indices. Overflowing clusters are
 *        truncated to the remaining index capacity.
 */
void cluster_grid_build(cluster_grid_t *grid, const render_camera_t *camera,
                        const render_light_t *lights, uint32_t n_lights);

/**
 * @brief Bin point samples (@p positions, 3 floats each) into the SAME froxels the
 *        forward+ lights use. Each froxel gets: (1) the @p min_probes nearest probes
 *        by world-space distance to the froxel centre -- a GUARANTEED minimum so a
 *        froxel is never starved and probes don't pop in/out as the camera moves --
 *        PLUS (2) every probe inside the froxel's AABB inflated by @p sphere_margin
 *        (a tight box test; the circumscribed sphere over-includes corner probes
 *        and bloats the list). Fills offsets/counts/indices like @ref cluster_grid_build.
 *
 * @param min_probes    guaranteed K-nearest probes per froxel (clamped to 16).
 * @param sphere_margin world-space halo (m) added to each froxel's AABB bounds.
 */
void cluster_grid_build_points(cluster_grid_t *grid, const render_camera_t *camera,
                               const float *positions, uint32_t n_points,
                               uint32_t min_probes, float sphere_margin);

/** @return The linear cluster index for (tile x, tile y, depth slice). */
static inline uint32_t cluster_grid_index(const cluster_grid_t *grid,
                                          uint32_t tx, uint32_t ty, uint32_t s)
{
    return (s * grid->config.tiles_y + ty) * grid->config.tiles_x + tx;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_CLUSTER_GRID_H */
