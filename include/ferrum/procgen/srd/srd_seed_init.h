/**
 * @file srd_seed_init.h
 * @brief Convert grammar seed layout to voxel SDF grid + room map.
 *
 * The grammar produces a seed layout: room boxes with positions,
 * extents, types, and connectivity. This module stamps those boxes
 * into an srd_sdf_grid_t (carving room interiors) and populates an
 * srd_room_map_t (assigning room IDs and adjacency). For each
 * adjacency pair, a doorway is carved through the shared wall.
 *
 * Types (1): srd_seed_room_t
 */
#ifndef SRD_SEED_INIT_H
#define SRD_SEED_INIT_H

#include "ferrum/procgen/srd/srd_sdf_grid.h"
#include "ferrum/procgen/srd/srd_room_map.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Seed room descriptor: box position, extents, and type.
 *
 * Describes one room in the grammar's seed layout. The room is an
 * axis-aligned box in 3D world space (Y = up).
 */
typedef struct {
    float           cx, cy, cz; /**< Center position in world space. */
    float           hx, hy, hz; /**< Half-extents along each axis. */
    srd_room_type_t type;       /**< Semantic room type. */
} srd_seed_room_t;

/**
 * @brief Convert a seed layout to a voxel SDF grid and room map.
 *
 * For each room: stamps its box SDF into the grid (CSG union), then
 * assigns room IDs via stamp_from_sdf. For each adjacency pair: carves
 * a doorway at the shared wall and sets adjacency in the room map.
 *
 * The grid is sized to fit all rooms plus a margin. The origin and
 * dimensions are computed automatically.
 *
 * @param rooms       Array of seed room descriptors.
 * @param n_rooms     Number of rooms (must be > 0).
 * @param adj_pairs   Flat array of adjacency pairs [a0, b0, a1, b1, ...].
 *                    Indices are 0-based into the rooms array.
 *                    May be NULL if n_pairs == 0.
 * @param n_pairs     Number of adjacency pairs.
 * @param voxel_size  Meters per voxel (e.g. 0.25).
 * @param margin      Extra world-space margin around the bounding box (meters).
 * @param grid_out    Output SDF grid (caller must destroy).
 * @param map_out     Output room map (caller must destroy).
 * @return 0 on success, -1 on error.
 *
 * @note Ownership: grid_out and map_out are initialized by this function.
 *       The caller is responsible for calling srd_sdf_grid_destroy() and
 *       srd_room_map_destroy() when done.
 */
int srd_seed_to_grid(const srd_seed_room_t *rooms, int n_rooms,
                     const int *adj_pairs, int n_pairs,
                     float voxel_size, float margin,
                     srd_sdf_grid_t *grid_out,
                     srd_room_map_t *map_out);

#ifdef __cplusplus
}
#endif

#endif /* SRD_SEED_INIT_H */
