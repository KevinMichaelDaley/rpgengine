/**
 * @file procgen_chunk_builder.h
 * @brief Chunk-based SVO rasterizer for loading geometry around the player.
 *
 * Splits dungeon geometry into spatial chunks (e.g., 64×64×64 m).
 * Each chunk owns an independent SVO subtree and can be loaded/unloaded.
 * The rasterizer distributes rooms and corridors into the chunk(s) they
 * overlap.
 */

#ifndef FERRUM_PROCGEN_CHUNK_BUILDER_H
#define FERRUM_PROCGEN_CHUNK_BUILDER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "ferrum/procgen/procgen_layout.h"
#include "ferrum/procgen/procgen_svo_builder.h"
#include "ferrum/npc/npc_svo.h"

/** @brief Default chunk size: 64 m in each dimension. */
#define PROCGEN_CHUNK_SIZE 64.0f

/** @brief Maximum active chunks. */
#define PROCGEN_MAX_CHUNKS 256

/**
 * @brief A single spatial chunk with its own SVO and mesh.
 */
typedef struct procgen_chunk {
    int              loaded;       /**< Non-zero if this chunk has geometry. */
    float            origin_x;     /**< World-space minimum corner. */
    float            origin_y;
    float            origin_z;
    uint32_t         max_depth;    /**< Octree depth (e.g., 8 → 256 cells). */
    npc_svo_grid_t   svo;         /**< Chunk's own SVO. */
    procgen_mesh_t   mesh;        /**< Generated mesh. */
} procgen_chunk_t;

/**
 * @brief Chunk grid covering the world.
 */
typedef struct procgen_chunk_grid {
    float            chunk_size;  /**< Meters per chunk (e.g., 64). */
    uint32_t         max_depth;   /**< SVO depth per chunk. */
    float            world_extent; /**< Half-extent of entire world. */

    procgen_chunk_t *chunks;      /**< Array of all chunks. */
    uint32_t         count_x;     /**< Number of chunks along X. */
    uint32_t         count_z;     /**< Number of chunks along Z. */

    int              initialized;
} procgen_chunk_grid_t;

/**
 * @brief Initialize a chunk grid for the given world parameters.
 *
 * @param grid          Output grid (allocated by caller).
 * @param chunk_size    Meters per chunk edge (e.g., 64).
 * @param max_depth     SVO depth per chunk (e.g., 8 → 1m voxels at 256m chunk).
 * @param world_extent  Half-extent of the world in meters.
 */
void procgen_chunk_grid_init(procgen_chunk_grid_t *grid,
                             float chunk_size,
                             uint32_t max_depth,
                             float world_extent);

/**
 * @brief Free all chunks and grid resources.
 */
void procgen_chunk_grid_destroy(procgen_chunk_grid_t *grid);

/**
 * @brief Rasterize dungeon layout geometry into the chunk grid.
 *
 * Each room, corridor, and ramp is assigned to every chunk it overlaps.
 * Chunks are loaded (SVO built) on demand.
 *
 * @param grid    Chunk grid to populate.
 * @param layout  Dungeon layout with rooms, corridors, ramps.
 * @return Number of chunks that received geometry.
 */
uint32_t procgen_chunk_grid_build(procgen_chunk_grid_t       *grid,
                                  const fr_dungeon_layout_t  *layout);

/**
 * @brief Get the chunk index for a world position.
 */
int procgen_chunk_grid_chunk_at(const procgen_chunk_grid_t *grid,
                                float wx, float wy, float wz);

/**
 * @brief Unload all chunks beyond a radius from the given position.
 *
 * @param grid    Chunk grid.
 * @param cx      Player X position.
 * @param cz      Player Z position.
 * @param radius  Keep chunks within this distance (meters).
 * @return Number of chunks unloaded.
 */
uint32_t procgen_chunk_grid_unload_far(procgen_chunk_grid_t *grid,
                                       float cx, float cz,
                                       float radius);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PROCGEN_CHUNK_BUILDER_H */
