#ifndef FERRUM_PROCGEN_SRD_BRIDGE_H
#define FERRUM_PROCGEN_SRD_BRIDGE_H

#include <stdint.h>
#include "ferrum/procgen/procgen_srd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Generate dungeon geometry from an ASCII floor plan + loss expression.
 *
 * Parses the ASCII grid, compiles the LOSS expression, runs SRD optimization,
 * and returns room + corridor geometry arrays (caller must free each pointer).
 *
 * @param ascii       Multi-floor ASCII grid string with LOSS: block.
 * @param seed        Random seed for reproducibility (0 = time-based).
 * @param time_budget Maximum wall-clock seconds for SRD optimization.
 * @param rooms_out   Output room array (allocated, caller frees via free()).
 * @param n_rooms_out Number of rooms written.
 * @param corridors_out  Output corridor array.
 * @param n_corridors_out Number of corridors.
 * @return 0 on success, -1 on error.
 */
int srd_generate(const char *ascii, uint32_t seed, double time_budget,
                 fr_room_box_t **rooms_out, uint32_t *n_rooms_out,
                 fr_corridor_seg_t **corridors_out, uint32_t *n_corridors_out);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PROCGEN_SRD_BRIDGE_H */
