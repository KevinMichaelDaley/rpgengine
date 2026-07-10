/**
 * @file srd_bridge.h
 * @brief SRD bridge: ASCII floor plan → optimized SVO.
 *
 * Parses an ASCII dungeon grid, initializes a voxel SDF grid from the
 * room layout, runs the discrete descent optimizer, and converts the
 * final grid to a sparse voxel octree (SVO).
 *
 * Non-static functions declared (4): srd_generate_svo, srd_generate_svo_ex,
 *                                     srd_generate, srd_free_geometry
 */
#ifndef FERRUM_PROCGEN_SRD_BRIDGE_H
#define FERRUM_PROCGEN_SRD_BRIDGE_H

#include <stdint.h>
#include "ferrum/procgen/procgen_srd_types.h"
#include "ferrum/npc/npc_svo.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configuration for dungeon SDF generation dimensions.
 *
 * Controls the spatial dimensions of the generated dungeon geometry.
 * All values are in meters. Use srd_dungeon_config_default() or
 * zero-initialize and call srd_generate_svo_ex() which fills defaults
 * for any zero fields.
 */
typedef struct srd_dungeon_config {
    float cell_size;      /**< Meters per ASCII grid cell (XZ). Default: 2.0 */
    float room_height;    /**< Full room interior height (Y). Default: 4.0 */
    float floor_spacing;  /**< Vertical distance between floor centers. Default: 5.0
                               Must be > room_height to leave a solid slab between floors. */
    float voxel_size;     /**< SDF grid resolution (meters per voxel). Default: 0.5 */
    float margin;         /**< Extra margin around dungeon bounds (meters). Default: 2.0 */
    int   stair_steps;    /**< Number of stair steps between floors. Default: 8 */
} srd_dungeon_config_t;

/**
 * @brief Generate dungeon SVO from an ASCII floor plan.
 *
 * Pipeline: parse ASCII → seed rooms → SDF grid + room map →
 * descent optimize → SDF to SVO.
 *
 * Uses default dungeon dimensions. For custom dimensions, use
 * srd_generate_svo_ex().
 *
 * @param ascii       Multi-line ASCII grid string.
 * @param seed        Random seed for reproducibility (0 = time-based).
 * @param time_budget Maximum wall-clock seconds for optimization.
 * @param svo_out     Output SVO grid. Caller must destroy via npc_svo_grid_destroy().
 * @return 0 on success, -1 on error.
 */
int srd_generate_svo(const char *ascii, uint32_t seed, double time_budget,
                     npc_svo_grid_t *svo_out);

/**
 * @brief Generate dungeon SVO with custom dimension configuration.
 *
 * Same pipeline as srd_generate_svo(), but uses caller-provided
 * dimensions for cell size, room height, floor spacing, etc.
 * Any zero-valued fields in cfg are replaced with defaults.
 *
 * @param ascii       Multi-line ASCII grid string.
 * @param seed        Random seed for reproducibility (0 = time-based).
 * @param time_budget Maximum wall-clock seconds for optimization.
 * @param cfg         Dungeon dimension config. NULL for defaults.
 * @param svo_out     Output SVO grid. Caller must destroy via npc_svo_grid_destroy().
 * @return 0 on success, -1 on error.
 */
int srd_generate_svo_ex(const char *ascii, uint32_t seed, double time_budget,
                        const srd_dungeon_config_t *cfg,
                        npc_svo_grid_t *svo_out);

/**
 * @brief Legacy API: generate dungeon geometry from ASCII floor plan.
 *
 * Internally calls srd_generate_svo. Returns empty room/corridor arrays
 * (the new pipeline produces SVO output, not tiles).
 *
 * @param ascii       Multi-line ASCII grid string.
 * @param seed        Random seed (0 = time-based).
 * @param time_budget Maximum optimization budget in seconds.
 * @param rooms_out   Output room array (set to NULL).
 * @param n_rooms_out Number of rooms (set to 0).
 * @param corridors_out Output corridor array (set to NULL).
 * @param n_corridors_out Number of corridors (set to 0).
 * @return 0 on success, -1 on error.
 */
int srd_generate(const char *ascii, uint32_t seed, double time_budget,
                 fr_room_box_t **rooms_out, uint32_t *n_rooms_out,
                 fr_corridor_seg_t **corridors_out, uint32_t *n_corridors_out);

/**
 * @brief Free geometry arrays returned by srd_generate().
 */
void srd_free_geometry(fr_room_box_t *rooms, fr_corridor_seg_t *corridors);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PROCGEN_SRD_BRIDGE_H */
