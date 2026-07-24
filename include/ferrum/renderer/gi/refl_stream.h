/**
 * @file refl_stream.h
 * @brief Streamed reflection-probe residency (rpg-wlh9): a fixed GPU atlas
 *        of octahedral tile SLOTS fed from the per-chunk .rprobe payloads
 *        that gi_sdf_stream pages in with the visibility-gated chunk
 *        residency. Chunk in -> its probes take free slots (subimage per
 *        mip + depth tile); chunk out -> slots free. Residency changes
 *        rebuild the probe meta TBO and the coarse world-grid index the
 *        fragment shader uses instead of looping every resident probe.
 *
 * Ownership: init owns all GL objects + CPU mirrors until destroy. Sync
 * never allocates (fixed pools sized at init); upload traffic is
 * glTexSubImage2D / glBufferData only when residency changed.
 */
#ifndef FERRUM_RENDERER_GI_REFL_STREAM_H
#define FERRUM_RENDERER_GI_REFL_STREAM_H

#include <stdbool.h>

#include "ferrum/renderer/gi/refl_file.h"
#include "ferrum/renderer/gi/refl_slots.h"
#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/shader_uniforms.h"

#ifdef __cplusplus
extern "C" {
#endif

struct gi_sdf_stream;

/** Streamed reflection-probe GPU state (one per render world). */
typedef struct refl_stream {
    /* GL objects. */
    uint32_t atlas;        /**< RGBA16F slot atlas, `mips` levels. */
    uint32_t depth_tex;    /**< RG32F visibility-depth slot atlas. */
    uint32_t meta_buf, meta_tex;    /**< RGBA32F TBO: 2 texels/slot. */
    uint32_t index_buf, index_tex;  /**< R32I TBO: cell -> slot ids. */
    /* Layout. */
    uint32_t slot_capacity, slots_x, slots_y;
    uint32_t tile_res, mips, depth_res;
    /* Residency (fixed pools, init-time malloc). */
    refl_slot_pool_t pool;
    uint16_t *pool_links;      /**< [slot_capacity]. */
    float *meta;               /**< [slot_capacity*8] CPU mirror. */
    float *probe_pos;          /**< [slot_capacity*3] for the index build. */
    float *probe_ao;           /**< [slot_capacity]. */
    uint8_t *slot_live;        /**< [slot_capacity]. */
    int32_t *cells;            /**< [REFL_STREAM_MAX_CELLS*4]. */
    uint16_t **chunk_slots;    /**< [n_chunks][REFL_CHUNK_MAX_PROBES]. */
    uint16_t *chunk_count;     /**< [n_chunks] uploaded probe count. */
    int n_chunks;
    int dirty;
    float idx_origin[3], idx_cell;
    int32_t idx_dims[3];
    float gain, range;
    /* GL entry points. */
    void (*glActiveTexture)(uint32_t);
    void (*glBindTexture)(uint32_t, uint32_t);
    void (*glDeleteTextures)(int32_t, const uint32_t *);
    void (*glDeleteBuffers)(int32_t, const uint32_t *);
    void (*glBindBuffer)(uint32_t, uint32_t);
    void (*glBufferData)(uint32_t, ptrdiff_t, const void *, uint32_t);
    void (*glTexSubImage2D)(uint32_t, int32_t, int32_t, int32_t, int32_t,
                            int32_t, uint32_t, uint32_t, const void *);
} refl_stream_t;

/** Grid-index cell budget (64x8x64 at the default 8 m cell). */
#define REFL_STREAM_MAX_CELLS (64 * 8 * 64)

/**
 * Create the slot atlas (+depth, meta, index) and CPU pools.
 * @p slot_capacity tiles of @p tile_res px with @p mips prefiltered
 * levels and @p depth_res visibility tiles. Returns false on NULL args /
 * GL failure (state cleaned, feature off).
 */
bool refl_stream_init(refl_stream_t *rs, const gl_loader_t *loader,
                      uint32_t slot_capacity, uint32_t tile_res,
                      uint32_t mips, uint32_t depth_res);

/**
 * Reconcile with the SDF stream's chunk residency: upload newly resident
 * chunks' probes into free slots, free evicted chunks' slots, and when
 * anything changed rebuild + upload the meta TBO and grid index. Chunks
 * whose payload layout mismatches the atlas are skipped with a warning.
 */
void refl_stream_sync(refl_stream_t *rs, const struct gi_sdf_stream *sdf);

/**
 * Bind atlas/meta/depth/index on units 35/41/42/43 and set every
 * u_refl_* uniform. Safe on a zeroed stream (count 0).
 */
void refl_stream_bind(const refl_stream_t *rs,
                      shader_uniform_cache_t *cache,
                      const shader_program_t *program);

/** Free GL + CPU state. NULL-safe, idempotent. */
void refl_stream_destroy(refl_stream_t *rs);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_GI_REFL_STREAM_H */
