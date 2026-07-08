/**
 * @file srd_rules_vfeature.h
 * @brief Voxel feature rewrite rules for SDF grids.
 *
 * Three feature rules:
 *   - AddPillar: stamp a solid cylinder into a room
 *   - RemovePillar: carve the cylinder back out (inverse)
 *   - ConvertType: cycle a room's type in the room map
 *
 * Types (0): uses srd_voxel_selection_t from srd_voxel_rule.h.
 */
#ifndef SRD_RULES_VFEATURE_H
#define SRD_RULES_VFEATURE_H

#include "ferrum/procgen/srd/srd_sdf_grid.h"
#include "ferrum/procgen/srd/srd_room_map.h"
#include "ferrum/procgen/srd/srd_voxel_rule.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Add a solid pillar at the center of a room.
 *
 * Stamps a vertical cylinder (radius = sel->param voxels) at the
 * XZ center of the room, spanning the full Y extent. Makes those
 * voxels solid and clears room ownership.
 *
 * @return 0 on success, -1 on invalid input.
 */
int srd_rule_add_pillar(srd_sdf_grid_t *grid, srd_room_map_t *map,
                        const srd_voxel_selection_t *sel);

/**
 * @brief Remove a pillar from the center of a room (inverse of add).
 *
 * Carves a vertical cylinder at the XZ center, restoring those
 * voxels to air and assigning room ownership.
 *
 * @return 0 on success, -1 on invalid input.
 */
int srd_rule_remove_pillar(srd_sdf_grid_t *grid, srd_room_map_t *map,
                           const srd_voxel_selection_t *sel);

/**
 * @brief Cycle a room's type to the next value.
 *
 * Advances the room type by (int)sel->param steps, wrapping around
 * SRD_ROOM_TYPE_COUNT. Self-inverse with negated param.
 *
 * @return 0 on success, -1 on invalid input.
 */
int srd_rule_convert_type(srd_sdf_grid_t *grid, srd_room_map_t *map,
                          const srd_voxel_selection_t *sel);

#ifdef __cplusplus
}
#endif

#endif /* SRD_RULES_VFEATURE_H */
