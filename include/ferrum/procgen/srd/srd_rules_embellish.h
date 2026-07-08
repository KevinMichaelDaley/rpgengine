/**
 * @file srd_rules_embellish.h
 * @brief Embellishment rewrite rules for voxel SDF grids.
 *
 * Three decorative/structural rules:
 *   - Alcove: carve a semicircular recess into a wall
 *   - FloorPit: lower a region of the floor (sunken area)
 *   - FloorPitFill: fill the pit back in (inverse)
 *
 * Types (0): uses srd_voxel_selection_t from srd_voxel_rule.h.
 */
#ifndef SRD_RULES_EMBELLISH_H
#define SRD_RULES_EMBELLISH_H

#include "ferrum/procgen/srd/srd_sdf_grid.h"
#include "ferrum/procgen/srd/srd_room_map.h"
#include "ferrum/procgen/srd/srd_voxel_rule.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Carve a semicircular alcove into a wall face.
 *
 * Creates a rounded recess centered on the wall face. The alcove
 * extends sel->param voxels into the wall with a semicircular profile
 * in the face's normal-vs-lateral plane. Height spans the middle
 * half of the wall.
 *
 * @param grid  SDF grid to modify. Must not be NULL.
 * @param map   Room map to modify. Must not be NULL.
 * @param sel   Selection with room_id, face (N/S/E/W), param (depth).
 * @return 0 on success, -1 on invalid input.
 */
int srd_rule_alcove(srd_sdf_grid_t *grid, srd_room_map_t *map,
                    const srd_voxel_selection_t *sel);

/**
 * @brief Carve a pit (sunken area) in the center of a room's floor.
 *
 * Extends the room downward by sel->param voxels in the center
 * half of the XZ footprint. Inverse of srd_rule_floor_pit_fill.
 *
 * @param grid  SDF grid to modify. Must not be NULL.
 * @param map   Room map to modify. Must not be NULL.
 * @param sel   Selection with room_id, face = SRD_FACE_FLOOR, param (depth).
 * @return 0 on success, -1 on invalid input.
 */
int srd_rule_floor_pit(srd_sdf_grid_t *grid, srd_room_map_t *map,
                       const srd_voxel_selection_t *sel);

/**
 * @brief Fill a floor pit back in (inverse of srd_rule_floor_pit).
 *
 * Fills voxels below the room's floor in the center region,
 * restoring them to solid.
 *
 * @param grid  SDF grid to modify. Must not be NULL.
 * @param map   Room map to modify. Must not be NULL.
 * @param sel   Selection with room_id, face = SRD_FACE_FLOOR, param (depth).
 * @return 0 on success, -1 on invalid input.
 */
int srd_rule_floor_pit_fill(srd_sdf_grid_t *grid, srd_room_map_t *map,
                            const srd_voxel_selection_t *sel);

#ifdef __cplusplus
}
#endif

#endif /* SRD_RULES_EMBELLISH_H */
