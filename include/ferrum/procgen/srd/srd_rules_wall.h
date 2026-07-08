/**
 * @file srd_rules_wall.h
 * @brief Wall rewrite rules for voxel SDF grids.
 *
 * Four wall rules that modify a single face of a room:
 *   - Push: shrink room by moving wall inward
 *   - Pull: expand room by moving wall outward
 *   - Bevel: chamfer the wall-ceiling edge
 *   - Niche: carve a rectangular alcove into the wall
 *
 * Push and Pull are natural inverses. Bevel and Niche can be undone
 * by applying the inverse operation (unbevel / fill niche) which is
 * equivalent to Push on the affected region.
 *
 * Types (0): uses srd_voxel_selection_t from srd_voxel_rule.h.
 */
#ifndef SRD_RULES_WALL_H
#define SRD_RULES_WALL_H

#include "ferrum/procgen/srd/srd_sdf_grid.h"
#include "ferrum/procgen/srd/srd_room_map.h"
#include "ferrum/procgen/srd/srd_voxel_rule.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Push / Pull (srd_rules_wall.c) ───────────────────────────────── */

/**
 * @brief Push a wall inward, shrinking the room on the selected face.
 *
 * Fills in sel->param voxels from the face inward: sets SDF to positive
 * and clears room ownership. The room becomes smaller on that side.
 *
 * @param grid  SDF grid to modify. Must not be NULL.
 * @param map   Room map to modify. Must not be NULL.
 * @param sel   Selection with room_id, face (N/S/E/W), and param (voxel count).
 * @return 0 on success, -1 on invalid input.
 */
int srd_rule_wall_push(srd_sdf_grid_t *grid, srd_room_map_t *map,
                       const srd_voxel_selection_t *sel);

/**
 * @brief Pull a wall outward, expanding the room on the selected face.
 *
 * Carves sel->param voxels beyond the face: sets SDF to negative
 * and assigns room ownership. The room becomes larger on that side.
 *
 * @param grid  SDF grid to modify. Must not be NULL.
 * @param map   Room map to modify. Must not be NULL.
 * @param sel   Selection with room_id, face (N/S/E/W), and param (voxel count).
 * @return 0 on success, -1 on invalid input.
 */
int srd_rule_wall_pull(srd_sdf_grid_t *grid, srd_room_map_t *map,
                       const srd_voxel_selection_t *sel);

/* ── Bevel / Niche (srd_rules_wall_shape.c) ──────────────────────── */

/**
 * @brief Bevel the wall-ceiling edge on the selected face.
 *
 * Carves a 45-degree chamfer where the wall meets the ceiling,
 * with width = sel->param voxels. Only modifies the corner region.
 *
 * @param grid  SDF grid to modify. Must not be NULL.
 * @param map   Room map to modify. Must not be NULL.
 * @param sel   Selection with room_id, face (N/S/E/W), and param (bevel width).
 * @return 0 on success, -1 on invalid input.
 */
int srd_rule_wall_bevel(srd_sdf_grid_t *grid, srd_room_map_t *map,
                        const srd_voxel_selection_t *sel);

/**
 * @brief Carve a rectangular niche into the selected wall face.
 *
 * The niche extends sel->param voxels into the wall, centered on
 * the face. Width and height are each half the face dimensions.
 *
 * @param grid  SDF grid to modify. Must not be NULL.
 * @param map   Room map to modify. Must not be NULL.
 * @param sel   Selection with room_id, face (N/S/E/W), and param (niche depth).
 * @return 0 on success, -1 on invalid input.
 */
int srd_rule_wall_niche(srd_sdf_grid_t *grid, srd_room_map_t *map,
                        const srd_voxel_selection_t *sel);

#ifdef __cplusplus
}
#endif

#endif /* SRD_RULES_WALL_H */
