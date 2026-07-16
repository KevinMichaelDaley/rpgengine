/**
 * @file gi_probe_kernel.h
 * @brief Dynamic-light probe update kernel (rpg-p3w3): cone/sphere-march the
 *        combined SDF from each probe to the dynamic lights and accumulate SH9.
 *
 * For each probe in a range, for each dynamic light (point/spot/directional):
 * compute the incident direction + radiance (range/spot falloff), sphere-march
 * the combined dynamic SDF (@ref gi_sdf_combined) from the probe toward the light
 * for a SOFT visibility (penumbra from the closest approach), and project
 * radiance*visibility from the incident direction into the probe's SH9 (three
 * @ref lm_sh9 blocks R,G,B laid out as probe.sh[ch*9 + band]). The forward+
 * material later samples the nearest probes and reconstructs cosine irradiance.
 *
 * The update takes a [from,to) probe range so it PARALLELISES OVER PROBES: a job
 * system fans the range out across workers (or a compute shader does one thread
 * per probe). Pure CPU reference here; no allocation, no global state.
 */
#ifndef FERRUM_RENDERER_GI_GI_PROBE_KERNEL_H
#define FERRUM_RENDERER_GI_GI_PROBE_KERNEL_H

#include <stdint.h>

#include "ferrum/renderer/gi/gi_probe_set.h"
#include "ferrum/renderer/gi/gi_sdf.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Dynamic light kind for the probe trace. */
typedef enum gi_light_kind {
    GI_LIGHT_DIRECTIONAL = 0, /**< @c dir = travel direction; no falloff. */
    GI_LIGHT_POINT = 1,       /**< @c pos, @c range. */
    GI_LIGHT_SPOT = 2         /**< @c pos, @c dir, @c range, cone @c cos_inner/outer. */
} gi_light_kind_t;

/** A dynamic light the probes gather from. */
typedef struct gi_light {
    gi_light_kind_t kind;
    float pos[3];       /**< point/spot position. */
    float dir[3];       /**< directional/spot travel direction (need not be unit). */
    float color[3];     /**< radiance (linear RGB). */
    float range;        /**< point/spot cutoff (m). */
    float cos_inner;    /**< spot: cone start (cos angle). */
    float cos_outer;    /**< spot: cone end (cos angle). */
} gi_light_t;

/**
 * @brief Update probes [@p from, @p to) of @p set: trace the combined SDF
 *        (baked field @p dist/@p dims/@p origin/@p voxel -- pass @p dist=NULL for
 *        none -- min the @p colliders) to each of @p lights and write each
 *        probe's SH9. @p march_steps bounds the sphere-march; @p soft_k controls
 *        penumbra sharpness (larger = harder). Overwrites the probes' SH.
 */
void gi_probe_kernel_update(gi_probe_set_t *set, uint32_t from, uint32_t to,
                            const float *dist, const int32_t dims[3],
                            const float origin[3], float voxel,
                            const gi_collider_t *colliders, uint32_t n_col,
                            const gi_light_t *lights, uint32_t n_lights,
                            uint32_t march_steps, float soft_k);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_GI_GI_PROBE_KERNEL_H */
