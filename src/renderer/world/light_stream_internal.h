/**
 * @file light_stream_internal.h
 * @brief Private shared decls for the client light-data streamer TUs (rpg-oda7).
 * Not a public API.
 */
#ifndef FERRUM_RENDERER_WORLD_LIGHT_STREAM_INTERNAL_H
#define FERRUM_RENDERER_WORLD_LIGHT_STREAM_INTERNAL_H

#include <stddef.h>

#include "ferrum/renderer/light_stream.h"

/** Asset-id offset for SDF chunks in the unified stream (rpg-vfmi): lightmap
 * chunks use ids [0, n_lm); SDF chunks use [CLIENT_SDF_ID_BASE, +n_sdf) so the two
 * classes never collide while sharing one fr_asset_stream. */
#define CLIENT_SDF_ID_BASE 0x40000000ull

/** Bytes of SH data for one w*h atlas chunk: 9 RGB32F coefficient images. */
static inline size_t lm_chunk_bytes(int w, int h)
{
    return (size_t)9u * (size_t)w * (size_t)h * 3u * sizeof(float);
}

/** Per-chunk residency slot (fr_asset_stream slot_user). Stable address: the
 * slots array is never moved after init, so an in-flight decode's pointer holds. */
typedef struct lm_chunk_slot {
    client_light_stream_t *owner;
    uint32_t               chunk_id;   /**< == slot index. */
    int                    w, h;       /**< this chunk's atlas dims. */
    float                 *coeff[9];   /**< decoded RAM (NULL until loaded / after upload). */
    int                    layer;      /**< assigned SH-array layer, -1 = none. */
    char                   path[512];  /**< <base_dir>/<chunk>.flm. */
} lm_chunk_slot_t;

/* Per-chunk slot_user for a streamed SDF chunk (sdf_stream, streamed mode). */
typedef struct sdf_chunk_slot {
    client_light_stream_t *owner;
    int                    chunk;   /**< index into owner->sdf. */
} sdf_chunk_slot_t;

/* SDF stream callbacks (sdf_stream): disk->RAM on a fiber, free on the owner
 * thread. No VRAM tier -- gi_runtime GPU-pages the RAM-resident chunks. */
size_t client_sdf_load(void *user, uint64_t id, fr_asset_class_t cls, void *slot_user);
void   client_sdf_evict(void *user, uint64_t id, fr_asset_class_t cls, void *slot_user, int drop);

/* fr_asset_stream callbacks (set by client_light_stream_init).
 *  - load   runs on a job fiber: decode the chunk file into slot->coeff (no GL).
 *  - upload runs on the render thread: glTexSubImage3D into a resident layer.
 *  - evict  runs on the render thread: free the layer + any RAM copy. */
size_t client_ls_load(void *user, uint64_t id, fr_asset_class_t cls, void *slot_user);
size_t client_ls_upload(void *user, uint64_t id, fr_asset_class_t cls, void *slot_user);
void   client_ls_evict(void *user, uint64_t id, fr_asset_class_t cls, void *slot_user, int drop);

#endif /* FERRUM_RENDERER_WORLD_LIGHT_STREAM_INTERNAL_H */
