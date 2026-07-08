/**
 * @file srd_room_map.h
 * @brief Per-voxel room identity tracking for SRD voxel SDF grids.
 *
 * Maintains a parallel uint8 grid mapping each voxel to a room ID
 * (0 = wall/void, 1..N = room), plus per-room type and a pairwise
 * adjacency matrix. Rewrite rules use this to know which voxels
 * belong to which room.
 *
 * Types (1): srd_room_map_t
 */
#ifndef SRD_ROOM_MAP_H
#define SRD_ROOM_MAP_H

#include "ferrum/procgen/srd/srd_room_type.h"
#include "ferrum/procgen/srd/srd_sdf_grid.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum number of rooms trackable in a single room map. */
#define SRD_ROOM_MAP_MAX_ROOMS 254

/**
 * @brief Per-voxel room identity grid with adjacency and type data.
 *
 * Ownership: ids array is owned by the map and freed by destroy().
 * Room IDs are 1-based (0 = wall/void). Room index in the types
 * and adjacency arrays is (id - 1).
 *
 * @note Not safe for concurrent access.
 */
typedef struct {
    uint8_t *ids;           /**< Per-voxel room ID, row-major [z*ny*nx + y*nx + x]. */
    int      nx, ny, nz;   /**< Grid dimensions (must match the SDF grid). */
    int      n_rooms;       /**< Number of rooms added (0..SRD_ROOM_MAP_MAX_ROOMS). */

    /** @brief Room types, indexed by (room_id - 1). */
    srd_room_type_t types[SRD_ROOM_MAP_MAX_ROOMS];

    /** @brief Flat adjacency matrix, indexed [a * SRD_ROOM_MAP_MAX_ROOMS + b].
     *  Nonzero iff rooms (a+1) and (b+1) are adjacent. Symmetric. */
    uint8_t adj[SRD_ROOM_MAP_MAX_ROOMS * SRD_ROOM_MAP_MAX_ROOMS];
} srd_room_map_t;

/* ── Lifecycle (srd_room_map.c) ────────────────────────────────── */

/**
 * @brief Initialize a room map with the given grid dimensions.
 *
 * All voxels start with room_id = 0 (wall/void).
 *
 * @param map         Output map. Must not be NULL.
 * @param nx,ny,nz    Dimensions (must all be > 0, should match the SDF grid).
 * @return 0 on success, -1 on error.
 */
int srd_room_map_init(srd_room_map_t *map, int nx, int ny, int nz);

/**
 * @brief Destroy a room map, freeing the ids array.
 *
 * Safe to call with NULL.
 */
void srd_room_map_destroy(srd_room_map_t *map);

/**
 * @brief Get the room ID at voxel (x, y, z).
 *
 * @return Room ID (0 = wall/void), or 0 for NULL/out-of-bounds.
 */
uint8_t srd_room_map_get(const srd_room_map_t *map, int x, int y, int z);

/**
 * @brief Set the room ID at voxel (x, y, z).
 *
 * No-op for NULL/out-of-bounds.
 */
void srd_room_map_set(srd_room_map_t *map, int x, int y, int z, uint8_t room_id);

/* ── Room management (srd_room_map_ops.c) ──────────────────────── */

/**
 * @brief Add a new room with the given type. Returns the room ID (1-based).
 *
 * @return Room ID > 0 on success, 0 if at capacity.
 */
uint8_t srd_room_map_add_room(srd_room_map_t *map, srd_room_type_t type);

/**
 * @brief Get the type of a room.
 *
 * @param room_id 1-based room ID.
 * @return Room type, or SRD_ROOM_GENERIC for invalid IDs.
 */
srd_room_type_t srd_room_map_get_type(const srd_room_map_t *map, uint8_t room_id);

/**
 * @brief Set the type of a room.
 *
 * @param room_id 1-based room ID.
 */
void srd_room_map_set_type(srd_room_map_t *map, uint8_t room_id, srd_room_type_t type);

/**
 * @brief Stamp room identity from an SDF grid.
 *
 * For each voxel where grid->values < 0 AND map->ids == 0 (unassigned),
 * sets the room ID. This prevents overwriting existing room assignments.
 *
 * @param map     Room map to modify.
 * @param grid    SDF grid to read from (must have same dimensions).
 * @param room_id Room ID to assign.
 */
void srd_room_map_stamp_from_sdf(srd_room_map_t *map,
                                 const srd_sdf_grid_t *grid,
                                 uint8_t room_id);

/* ── Adjacency (srd_room_map_adj.c) ───────────────────────────── */

/**
 * @brief Compute adjacency by scanning for neighbouring voxels with
 *        different room IDs.
 *
 * Clears and rebuilds the adjacency matrix. Two rooms are adjacent if
 * any voxel of room A is face-adjacent (6-connected) to a voxel of room B.
 */
void srd_room_map_compute_adjacency(srd_room_map_t *map);

/**
 * @brief Check if two rooms are adjacent.
 *
 * @param a,b 1-based room IDs.
 * @return true if adjacent.
 */
bool srd_room_map_are_adjacent(const srd_room_map_t *map, uint8_t a, uint8_t b);

/**
 * @brief Manually set or clear adjacency between two rooms.
 *
 * @param a,b 1-based room IDs.
 * @param adjacent true to set, false to clear.
 */
void srd_room_map_set_adjacent(srd_room_map_t *map, uint8_t a, uint8_t b, bool adjacent);

/**
 * @brief Count the number of voxels belonging to a room.
 *
 * @param room_id 1-based room ID.
 * @return Voxel count (0 for invalid room IDs).
 */
int srd_room_map_count_volume(const srd_room_map_t *map, uint8_t room_id);

/* ── Copy (srd_room_map_copy.c) ───────────────────────────────── */

/**
 * @brief Deep-copy a room map. Allocates a new ids array for dst.
 *
 * @param dst Output map (must not be NULL). Previous contents are overwritten.
 * @param src Source map (must not be NULL).
 * @return 0 on success, -1 on error.
 */
int srd_room_map_copy(srd_room_map_t *dst, const srd_room_map_t *src);

#ifdef __cplusplus
}
#endif

#endif /* SRD_ROOM_MAP_H */
