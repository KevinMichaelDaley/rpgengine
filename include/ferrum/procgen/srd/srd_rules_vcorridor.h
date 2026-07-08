/**
 * @file srd_rules_vcorridor.h
 * @brief Voxel corridor rewrite rules for SDF grids.
 *
 * Two corridor rules that modify the width of a corridor room:
 *   - Widen: expand corridor cross-section
 *   - Narrow: shrink corridor cross-section
 *
 * Widen and Narrow are natural inverses. The corridor's long axis
 * is auto-detected from its bounding box aspect ratio.
 *
 * Types (0): uses srd_voxel_selection_t from srd_voxel_rule.h.
 */
#ifndef SRD_RULES_VCORRIDOR_H
#define SRD_RULES_VCORRIDOR_H

#include "ferrum/procgen/srd/srd_sdf_grid.h"
#include "ferrum/procgen/srd/srd_room_map.h"
#include "ferrum/procgen/srd/srd_voxel_rule.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Widen a corridor by param voxels on each side.
 *
 * Auto-detects the corridor's long axis and expands perpendicular to it.
 * Uses sel->face = SRD_FACE_NONE, sel->param = voxel count per side.
 *
 * @return 0 on success, -1 on invalid input.
 */
int srd_rule_corridor_widen(srd_sdf_grid_t *grid, srd_room_map_t *map,
                            const srd_voxel_selection_t *sel);

/**
 * @brief Narrow a corridor by param voxels on each side.
 *
 * Auto-detects the corridor's long axis and shrinks perpendicular to it.
 * Uses sel->face = SRD_FACE_NONE, sel->param = voxel count per side.
 *
 * @return 0 on success, -1 on invalid input.
 */
int srd_rule_corridor_narrow(srd_sdf_grid_t *grid, srd_room_map_t *map,
                             const srd_voxel_selection_t *sel);

#ifdef __cplusplus
}
#endif

#endif /* SRD_RULES_VCORRIDOR_H */
