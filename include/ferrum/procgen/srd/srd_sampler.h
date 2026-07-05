#ifndef FERRUM_PROCGEN_SRD_SAMPLER_H
#define FERRUM_PROCGEN_SRD_SAMPLER_H

#include <stdint.h>
#include "ferrum/procgen/procgen_srd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float x, y, z;
} srd_sample_point_t;

/**
 * @brief Generate uniform grid sample points within a room box.
 *
 * @param room   Room definition.
 * @param grid_n Number of samples per axis (e.g. 4 → 64 points).
 * @param points_out Output array.
 * @param cap     Capacity of points_out.
 * @param count_out Number of points written.
 * @return 0 on success, -1 on error.
 */
int srd_sample_room(const fr_room_box_t *room, int grid_n,
                    srd_sample_point_t *points_out,
                    uint32_t cap, uint32_t *count_out);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PROCGEN_SRD_SAMPLER_H */
