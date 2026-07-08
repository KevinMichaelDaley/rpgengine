/**
 * @file srd_rules_height.h
 * @brief Ceiling/floor height rewrite rules for voxel SDF grids.
 *
 * Three height rules:
 *   - CeilingRaise: expand room upward
 *   - CeilingLower: shrink room from above
 *   - FloorStep: raise a platform in a sub-region of the floor
 *
 * CeilingRaise and CeilingLower are natural inverses.
 *
 * Types (0): uses srd_voxel_selection_t from srd_voxel_rule.h.
 */
#ifndef SRD_RULES_HEIGHT_H
#define SRD_RULES_HEIGHT_H

#include "ferrum/procgen/srd/srd_sdf_grid.h"
#include "ferrum/procgen/srd/srd_room_map.h"
#include "ferrum/procgen/srd/srd_voxel_rule.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Raise the ceiling of a room by param voxels.
 *
 * Carves voxels above the current ceiling, expanding the room upward.
 * Uses sel->face = SRD_FACE_CEIL, sel->param = voxel count.
 *
 * @return 0 on success, -1 on invalid input.
 */
int srd_rule_ceiling_raise(srd_sdf_grid_t *grid, srd_room_map_t *map,
                           const srd_voxel_selection_t *sel);

/**
 * @brief Lower the ceiling of a room by param voxels.
 *
 * Fills in voxels at the top of the room, shrinking it from above.
 * Uses sel->face = SRD_FACE_CEIL, sel->param = voxel count.
 *
 * @return 0 on success, -1 on invalid input.
 */
int srd_rule_ceiling_lower(srd_sdf_grid_t *grid, srd_room_map_t *map,
                           const srd_voxel_selection_t *sel);

/**
 * @brief Create a raised floor step (platform) in the center of a room.
 *
 * Fills in voxels from the floor upward by param voxels, in the center
 * half of the room's XZ footprint. Creates a raised platform.
 * Uses sel->face = SRD_FACE_FLOOR, sel->param = step height in voxels.
 *
 * @return 0 on success, -1 on invalid input.
 */
int srd_rule_floor_step(srd_sdf_grid_t *grid, srd_room_map_t *map,
                        const srd_voxel_selection_t *sel);

#ifdef __cplusplus
}
#endif

#endif /* SRD_RULES_HEIGHT_H */
