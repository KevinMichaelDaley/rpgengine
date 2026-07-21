/**
 * @file probe_chunk_sdf.h
 * @brief CPU sampler over baked _cNNN.sdf chunks (rpg-pjkb, feature 4).
 *
 * The offline probe-placement pass runs AFTER the baker over the full baked
 * field, so it needs the chunked FSDF sidecars readable without GL (the
 * runtime's gi_sdf_stream is texture-coupled). This loads every contiguous
 * <prefix>_cNNN.sdf via lm_sdf_file and answers trilinear signed-distance
 * queries: the MIN over chunks containing the point (matching the compute
 * shader's scene_sdf), and a large positive value outside all chunks.
 *
 * Ownership: probe_chunk_sdf_open allocates chunk storage (malloc, offline
 * tool context -- not per-frame); release with probe_chunk_sdf_close.
 * Nullability: sample's @p user must be a valid probe_chunk_sdf_t. Errors:
 * open returns false when no chunk loads. No side effects beyond allocation.
 */
#ifndef FERRUM_PROBE_PLACE_PROBE_CHUNK_SDF_H
#define FERRUM_PROBE_PLACE_PROBE_CHUNK_SDF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/lightmap/lm_sdf_file.h"

/** A loaded chunk list; fields are read-only for callers. */
typedef struct probe_chunk_sdf {
    lm_sdf_data_t *chunks;   /**< [count] loaded chunk fields (owned). */
    uint32_t       count;    /**< loaded chunk count. */
} probe_chunk_sdf_t;

/**
 * @brief Load every contiguous <prefix>_cNNN.sdf (N = 000, 001, ... until the
 *        first missing file). Returns false (and loads nothing) when @p prefix
 *        or @p out is NULL or no chunk file exists.
 */
bool probe_chunk_sdf_open(const char *prefix, probe_chunk_sdf_t *out);

/**
 * @brief Trilinear signed distance at @p p: min over containing chunks, 1e9
 *        outside all. Signature matches the probe_brick/probe_fixup callback
 *        (@p user = the probe_chunk_sdf_t).
 */
float probe_chunk_sdf_sample(const float p[3], void *user);

/** @brief Free all chunk storage and zero the struct. NULL-safe. */
void probe_chunk_sdf_close(probe_chunk_sdf_t *cs);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PROBE_PLACE_PROBE_CHUNK_SDF_H */
