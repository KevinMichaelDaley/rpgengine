/**
 * @file procgen_svo_builder.h
 * @brief Convert fr_dungeon_layout_t to SVO solid voxels + generate meshes.
 *
 * Configurable voxel size and octree depth.  The rasterizer is
 * pluggable — the current implementation is direct voxel scanning;
 * future implementations can use BSPs, clipping, etc.
 */

#ifndef FERRUM_PROCGEN_SVO_BUILDER_H
#define FERRUM_PROCGEN_SVO_BUILDER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "ferrum/procgen/procgen_layout.h"
#include "ferrum/npc/npc_svo.h"

/* ── Configuration ────────────────────────────────────────────── */

/** @brief Default voxel size: 0.125m (1/8 Minecraft block). */
#define PROCGEN_DEFAULT_VOXEL_SIZE 0.125f

/** @brief Default world half-extent: 64m (128m total span). */
#define PROCGEN_DEFAULT_WORLD_EXTENT 64.0f

/** @brief Default octree depth.  9 → 512 cells/axis, 0.25m at 128m span. */
#define PROCGEN_DEFAULT_MAX_DEPTH 7

/** @brief Rasterizer type (for pluggable interface). */
typedef enum {
    PROCGEN_RASTER_VOXEL  = 0,  /**< Direct voxel scanning (current). */
    PROCGEN_RASTER_BSP    = 1,  /**< BSP/clipping (future). */
    PROCGEN_RASTER_MESH   = 2   /**< Static mesh rasterizer (future). */
} procgen_raster_type_t;

/* ── Rasterizer config ────────────────────────────────────────── */

typedef struct {
    procgen_raster_type_t type;       /**< Rasterizer type. */
    float                 voxel_size; /**< Meters per voxel (default 0.125). */
    uint32_t              max_depth;  /**< Octree depth (default 8). */
    float                 world_extent; /**< World half-extent in meters. */
} procgen_raster_config_t;

/** @brief Fill config with defaults. */
void procgen_raster_config_default(procgen_raster_config_t *cfg);

/* ── Build SVO ────────────────────────────────────────────────── */

/**
 * @brief Build an SVO grid from a dungeon layout using the given config.
 *
 * @param cfg       Rasterizer configuration.
 * @param layout    Dungeon layout.
 * @param out_grid  Output SVO grid (allocated by caller, will be initialized).
 * @return Number of solid voxels marked, or 0 on failure.
 */
uint32_t procgen_svo_build_cfg(const procgen_raster_config_t *cfg,
                                const fr_dungeon_layout_t *layout,
                                npc_svo_grid_t *out_grid);

/**
 * @brief Build SVO with default config (backward-compatible convenience).
 */
uint32_t procgen_svo_build(npc_svo_grid_t *grid,
                           const fr_dungeon_layout_t *layout);

/* ── Mesh generation ──────────────────────────────────────────── */

/** @brief A generated triangle mesh from the SVO. */
typedef struct {
    float    *vertices;    /**< Interleaved xyz floats, 3 * vertex_count. */
    uint32_t  vertex_count;
    uint32_t  vertex_cap;
} procgen_mesh_t;

/** @brief Initialize an empty mesh. */
void procgen_mesh_init(procgen_mesh_t *m);

/** @brief Free mesh memory. */
void procgen_mesh_destroy(procgen_mesh_t *m);

/**
 * @brief Generate a triangle mesh from an SVO grid.
 *
 * For each solid voxel, emits the 6 faces that are adjacent to
 * non-solid or grid-boundary neighbors.  Uses greedy meshing
 * (merges adjacent coplanar quads) to reduce triangle count.
 *
 * @param grid  Populated SVO (must have SOLID voxels).
 * @param mesh  Output mesh (will be populated).
 * @return Number of triangles generated.
 */
uint32_t procgen_mesh_from_svo(const npc_svo_grid_t *grid,
                                procgen_mesh_t *mesh);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PROCGEN_SVO_BUILDER_H */
