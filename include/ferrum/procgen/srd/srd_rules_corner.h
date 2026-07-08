/**
 * @file srd_rules_corner.h
 * @brief Corner chamfer and round rewrite rules for voxel SDF grids.
 *
 * Two corner rules that modify an XZ corner of a room:
 *   - Chamfer: 45-degree diagonal cut
 *   - Round: circular arc cut
 *
 * Types (0): uses srd_voxel_selection_t from srd_voxel_rule.h.
 */
#ifndef SRD_RULES_CORNER_H
#define SRD_RULES_CORNER_H

#include "ferrum/procgen/srd/srd_sdf_grid.h"
#include "ferrum/procgen/srd/srd_room_map.h"
#include "ferrum/procgen/srd/srd_voxel_rule.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Chamfer a room corner with a 45-degree cut.
 *
 * Carves a triangular region at the selected XZ corner, extending
 * sel->param voxels along each axis. Corner indices:
 *   0=NE (+X,-Z), 1=NW (-X,-Z), 2=SE (+X,+Z), 3=SW (-X,+Z).
 *
 * @param grid  SDF grid to modify. Must not be NULL.
 * @param map   Room map to modify. Must not be NULL.
 * @param sel   Selection with room_id, corner (0-3), and param (cut size).
 * @return 0 on success, -1 on invalid input.
 */
int srd_rule_corner_chamfer(srd_sdf_grid_t *grid, srd_room_map_t *map,
                            const srd_voxel_selection_t *sel);

/**
 * @brief Round a room corner with a circular arc.
 *
 * Carves voxels at the selected XZ corner where distance from the
 * corner point is <= sel->param (radius in voxels). Same corner
 * indices as chamfer.
 *
 * @param grid  SDF grid to modify. Must not be NULL.
 * @param map   Room map to modify. Must not be NULL.
 * @param sel   Selection with room_id, corner (0-3), and param (radius).
 * @return 0 on success, -1 on invalid input.
 */
int srd_rule_corner_round(srd_sdf_grid_t *grid, srd_room_map_t *map,
                          const srd_voxel_selection_t *sel);

#ifdef __cplusplus
}
#endif

#endif /* SRD_RULES_CORNER_H */
