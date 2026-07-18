/**
 * @file gi_static_volume.h
 * @brief Coarse world-space volume of baked-lightmap irradiance E (rpg-pau4).
 *
 * The offline lightmap stores static irradiance per surface luxel (2D atlas,
 * indexed by uv1). To fold that STATIC ambience into the dynamic GI probes, we
 * splat the lightmap E into a coarse 3D world grid so the probe-update cone
 * trace can gather it by world position at each surface hit -- one bounce more
 * than the offline bake (@ref gi_probe_gpu_set_static).
 *
 * This module owns only the GL 3D texture + its grid metadata; the caller
 * builds the RGB float grid (from meshes + the loaded lightmap) and hands it in.
 * Ownership: @ref gi_static_volume_upload creates the texture; free it with
 * @ref gi_static_volume_destroy.
 */
#ifndef FERRUM_RENDERER_GI_GI_STATIC_VOLUME_H
#define FERRUM_RENDERER_GI_GI_STATIC_VOLUME_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** A baked-irradiance 3D texture plus the world grid it covers. */
typedef struct gi_static_volume {
    unsigned int tex;       /**< GL_TEXTURE_3D, RGB32F (0 until uploaded). */
    float        origin[3]; /**< world min corner of cell (0,0,0). */
    float        voxel;     /**< cell size in metres. */
    int          dims[3];   /**< grid resolution in cells. */
} gi_static_volume_t;

/**
 * @brief Upload an RGB float irradiance grid (@p rgb, dims[0]*dims[1]*dims[2]*3
 *        floats, x-fastest then y then z) as a trilinear GL_TEXTURE_3D and
 *        record the grid placement. Returns false on a NULL/invalid argument or
 *        GL failure. @p v is zeroed first. Needs a current GL context.
 */
bool gi_static_volume_upload(gi_static_volume_t *v, const float *rgb,
                             const int dims[3], const float origin[3],
                             float voxel);

/** @brief Free the GL texture. NULL-safe; safe to call on a zeroed struct. */
void gi_static_volume_destroy(gi_static_volume_t *v);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_GI_GI_STATIC_VOLUME_H */
