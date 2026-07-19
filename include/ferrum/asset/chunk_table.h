/**
 * @file chunk_table.h
 * @brief Spatial table of baked light-data chunks over the streaming manager
 *        (rpg-nbp2). Registers lightmap SH / SDF-albedo-voxel chunks (each keyed
 *        by a world box) as assets in an fr_asset_stream_t, assigns their
 *        priority from a point of interest, and reports which chunk boxes are
 *        currently resident -- the set that gates probe load/generation
 *        (probe_place_filter_chunks). Headless (no GL): the actual chunk upload
 *        is the streaming manager's load/upload callback.
 */
#ifndef FERRUM_ASSET_CHUNK_TABLE_H
#define FERRUM_ASSET_CHUNK_TABLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ferrum/asset/asset_stream.h"

/** One chunk's identity + world box. */
typedef struct fr_chunk_entry {
    uint64_t id;         /**< asset id in the streaming manager. */
    float    box_min[3]; /**< chunk world box min. */
    float    box_max[3]; /**< chunk world box max. */
} fr_chunk_entry_t;

/** A table of chunks layered over a streaming manager. Caller owns storage. */
typedef struct fr_chunk_table {
    fr_asset_stream_t *stream;  /**< borrowed. */
    fr_chunk_entry_t  *entries; /**< caller-provided [cap]. */
    uint32_t           cap;
    uint32_t           count;
} fr_chunk_table_t;

/** Initialize over @p stream with caller storage. */
void fr_chunk_table_init(fr_chunk_table_t *t, fr_asset_stream_t *stream,
                         fr_chunk_entry_t *storage, uint32_t cap);

/**
 * @brief Register a chunk (world box) as an asset in the streaming manager.
 * @return false if the table is full or the stream rejects the id.
 */
bool fr_chunk_table_add(fr_chunk_table_t *t, uint64_t id, fr_asset_class_t cls,
                        const float box_min[3], const float box_max[3],
                        size_t ram_size, size_t vram_size, void *user);

/**
 * @brief Set streaming priority of every chunk from a point of interest: nearer
 *        chunks get higher priority (so they stream first). @p scale converts
 *        world distance to a priority decrement.
 */
void fr_chunk_table_set_interest(fr_chunk_table_t *t, const float point[3],
                                 float scale);

/**
 * @brief Collect the world boxes of currently-resident (RAM/VRAM) chunks.
 * @param out_min,out_max  filled with up to @p cap boxes (3 floats each).
 * @return number of resident chunk boxes written.
 */
uint32_t fr_chunk_table_resident_boxes(const fr_chunk_table_t *t, float *out_min,
                                       float *out_max, uint32_t cap);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_ASSET_CHUNK_TABLE_H */
