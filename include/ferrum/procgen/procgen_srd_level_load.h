#ifndef FERRUM_PROCGEN_SRD_LEVEL_LOAD_H
#define FERRUM_PROCGEN_SRD_LEVEL_LOAD_H

#include <stdint.h>
#include "ferrum/procgen/procgen_svo_builder.h"
#include "ferrum/procgen/procgen_srd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    procgen_mesh_t   mesh;
    fr_room_box_t   *rooms;
    uint32_t         room_count;
    fr_corridor_seg_t *corridors;
    uint32_t           corridor_count;
    int               ok;
} procgen_srd_level_t;

void procgen_srd_level_init(procgen_srd_level_t *lvl);
void procgen_srd_level_free(procgen_srd_level_t *lvl);

/**
 * @brief Load a dungeon from an ASCII floor plan file.
 *
 * Reads the file content, runs SRD optimization with the given budget,
 * rasterizes the resulting geometry into an SVO, and generates a
 * triangle mesh.  All chunks are pre-generated eagerly.
 *
 * @param lvl        Output level struct.
 * @param path       Path to .asc file with ASCII grid + LOSS: block.
 * @param seed       Random seed (0 = time-based).
 * @param time_budget  SRD optimization budget in seconds (0 = default 3s).
 * @return 0 on success, -1 on error.
 */
int procgen_srd_level_load(procgen_srd_level_t *lvl,
                            const char *path,
                            uint32_t seed,
                            double time_budget);

/**
 * @brief Load a dungeon from an in-memory ASCII string.
 */
int procgen_srd_level_load_string(procgen_srd_level_t *lvl,
                                   const char *ascii,
                                   uint32_t seed,
                                   double time_budget);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PROCGEN_SRD_LEVEL_LOAD_H */
