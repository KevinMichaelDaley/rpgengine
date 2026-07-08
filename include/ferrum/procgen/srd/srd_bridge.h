/**
 * @file srd_bridge.h
 * @brief SRD bridge: ASCII floor plan → optimized SVO.
 *
 * Parses an ASCII dungeon grid, initializes a voxel SDF grid from the
 * room layout, runs the discrete descent optimizer, and converts the
 * final grid to a sparse voxel octree (SVO).
 *
 * Non-static functions declared (3): srd_generate_svo, srd_generate, srd_free_geometry
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
 * @brief Generate dungeon SVO from an ASCII floor plan.
 *
 * Pipeline: parse ASCII → seed rooms → SDF grid + room map →
 * descent optimize → SDF to SVO.
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
