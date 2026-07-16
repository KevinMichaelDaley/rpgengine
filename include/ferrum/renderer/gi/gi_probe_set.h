/**
 * @file gi_probe_set.h
 * @brief An ADAPTIVE set of irradiance probes for dynamic-light GI (rpg-qthg).
 *
 * Probes are NOT on a uniform grid -- each has an explicit stored world position
 * (placed adaptively near surfaces / the play space) and an SH9 (per-colour)
 * irradiance that the probe-update kernel fills by cone-tracing the scene SDF to
 * the dynamic lights. The forward+ material later gathers the nearest probes
 * (via @ref gi_probe_grid) and blends their SH.
 *
 * Storage is caller-provided (arena/pool): two flat backing arrays, positions
 * (3 floats/probe) and SH (27 floats/probe = 9 bands x RGB), so the whole set
 * uploads as two SSBOs. Ownership: backing arrays borrowed. Nullability: set +
 * backing non-NULL. No allocation.
 */
#ifndef FERRUM_RENDERER_GI_GI_PROBE_SET_H
#define FERRUM_RENDERER_GI_GI_PROBE_SET_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** A set of adaptively-placed probes over caller-provided backing storage. */
typedef struct gi_probe_set {
    float   *pos;      /**< 3*capacity: xyz world position per probe. */
    float   *sh;       /**< 27*capacity: 9 SH bands x RGB irradiance per probe. */
    uint32_t count;    /**< probes in use. */
    uint32_t capacity; /**< backing capacity (probes). */
} gi_probe_set_t;

/**
 * @brief Initialise @p set over @p pos_backing (>= 3*capacity floats) and
 *        @p sh_backing (>= 27*capacity floats). Sets count = 0.
 */
void gi_probe_set_init(gi_probe_set_t *set, float *pos_backing,
                       float *sh_backing, uint32_t capacity);

/**
 * @brief Append a probe at (@p x,@p y,@p z) with a cleared SH. Returns its index,
 *        or -1 if @p set is full / NULL.
 */
int32_t gi_probe_add(gi_probe_set_t *set, float x, float y, float z);

/** @brief Drop all probes (count = 0); backing/capacity unchanged. NULL-safe. */
void gi_probe_set_reset(gi_probe_set_t *set);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_GI_GI_PROBE_SET_H */
