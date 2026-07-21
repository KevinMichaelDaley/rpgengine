/**
 * @file gi_brick_gpu.h
 * @brief GPU objects for brick-structured probe sampling (rpg-pjkb).
 *
 * The forward pass samples brick-placed probes in O(1): fragment -> one voxel
 * fetch (R32I 3D index, -1 = uncovered) -> brick meta (min+size) -> 8 of the
 * brick's 64 probe ids by local trilinear cell -> the existing SH/SG/visibility
 * fetches. No froxel binning anywhere. This module owns the static GL objects
 * uploaded once from the offline .bricks data; binding lives with gi_runtime.
 *
 * Ownership: create() makes GL objects (destroy() deletes them); the CPU-side
 * inputs are only read during create. Errors: false on NULL args/GL failure.
 */
#ifndef FERRUM_RENDERER_GI_GI_BRICK_GPU_H
#define FERRUM_RENDERER_GI_GI_BRICK_GPU_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "ferrum/probe/place/probe_brick_file.h"
#include "ferrum/probe/place/probe_brick_index.h"

/** GL objects + grid params for the brick sampling path. */
typedef struct gi_brick_gpu {
    unsigned int index_tex;             /**< R32I 3D voxel -> brick id (-1 none). */
    unsigned int meta_buf, meta_tex;    /**< RGBA32F TBO: brick min.xyz + size. */
    unsigned int pidx_buf, pidx_tex;    /**< R32UI TBO: 64 probe ids per brick. */
    unsigned int valid_buf, valid_tex;  /**< R8UI TBO: per-probe validity. */
    int   dim[3];                       /**< index voxel counts. */
    float origin[3];                    /**< index voxel (0,0,0) min corner. */
    float voxel;                        /**< index voxel edge (m). */
    int   on;                           /**< 1 once created. */
} gi_brick_gpu_t;

/**
 * @brief Upload @p bd + @p ix into fresh GL objects (a 4.3 context current).
 * @return true on success; false on NULL args or zero bricks.
 */
bool gi_brick_gpu_create(gi_brick_gpu_t *g, const probe_brick_data_t *bd,
                         const probe_brick_index_t *ix);

/** @brief Delete the GL objects and zero the struct. NULL-safe. */
void gi_brick_gpu_destroy(gi_brick_gpu_t *g);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_GI_GI_BRICK_GPU_H */
