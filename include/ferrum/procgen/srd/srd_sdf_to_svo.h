/**
 * @file srd_sdf_to_svo.h
 * @brief Convert SDF grid to sparse voxel octree (SVO).
 *
 * Booleanizes the SDF at threshold 0: voxels with SDF >= 0 are marked
 * as SOLID in the SVO (walls/floor/ceiling). Voxels with SDF < 0 are
 * air (room interior). The SVO is initialized with bounds matching
 * the grid's world extent.
 *
 * Types: none (uses srd_sdf_grid_t and npc_svo_grid_t).
 */
#ifndef SRD_SDF_TO_SVO_H
#define SRD_SDF_TO_SVO_H

#include "ferrum/procgen/srd/srd_sdf_grid.h"
#include "ferrum/npc/npc_svo.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Convert an SDF grid to a sparse voxel octree.
 *
 * Initializes svo_out with world bounds derived from the grid, then
 * walks every voxel: those with SDF >= 0 are inserted as SOLID nodes
 * in the octree. Air voxels (SDF < 0) are omitted (the SVO is sparse).
 *
 * The SVO depth is chosen so that the SVO voxel size is <= the grid's
 * voxel_size, ensuring no loss of resolution.
 *
 * @param grid     Input SDF grid. Must not be NULL.
 * @param svo_out  Output SVO grid. Caller must call npc_svo_grid_destroy().
 * @return 0 on success, -1 on error.
 *
 * @note Ownership: svo_out is initialized by this function. The caller
 *       must destroy it when done.
 */
int srd_sdf_to_svo(const srd_sdf_grid_t *grid, npc_svo_grid_t *svo_out);

#ifdef __cplusplus
}
#endif

#endif /* SRD_SDF_TO_SVO_H */
