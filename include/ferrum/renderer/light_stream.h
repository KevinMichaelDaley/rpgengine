/**
 * @file light_stream.h
 * @brief Standalone client light-data streaming: pages baked lightmap SH chunks
 *        into bounded GPU residency via the prioritized asset streamer (rpg-oda7).
 *
 * This is a SEPARATE subsystem from scene-descriptor loading (client_scene). It
 * owns one fr_asset_stream + a chunk table over the lightmap SH chunks, and the
 * 9 GL_TEXTURE_2D_ARRAY SH coefficient pages the forward pass samples. Chunk file
 * decode runs on a JOB FIBER (fr_asset_stream load callback, no GL); the GPU
 * upload runs on the render/GL thread (the streamer's upload callback, invoked
 * from fr_asset_stream_tick on the owner thread) -- the job/command-buffer
 * separation the engine already uses. A single-atlas .flm is the one-chunk case
 * of this general chunked system; the design generalizes to zones + many chunks
 * (see ref/gi_streaming_design.md).
 *
 * Ownership: owns its fr_asset_stream, chunk storage, SH textures, per-mesh rect/
 * chunk tables. Borrows the job system + gl_loader passed in the config. Not
 * thread-safe: init/tick/destroy on the render (GL-owning) thread; the streamer
 * dispatches decode fibers internally.
 */
#ifndef FERRUM_RENDERER_LIGHT_STREAM_H
#define FERRUM_RENDERER_LIGHT_STREAM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/asset/asset_stream.h"
#include "ferrum/asset/chunk_table.h"
#include "ferrum/lightmap/lm_atlas.h"
#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/gi/gi_sdf_stream.h"

struct job_system; /* ferrum/job/job_system.h */

/** Max lightmap SH chunks resident on the GPU at once (SH-array layer count). */
#define CLIENT_LM_MAX_RESIDENT 8u

/** Config for client_light_stream_init. Paths are resolved under base_dir. */
typedef struct client_light_stream_config {
    const gl_loader_t  *loader;      /**< GL entry points (borrowed). */
    struct job_system  *jobs;        /**< async decode executor (NULL => inline). */
    const char         *base_dir;    /**< level asset directory. */
    const char         *lm_prefix;   /**< lightmap ref, e.g. "great_hall.flm". */
    const char         *sdf_prefix;  /**< SDF/voxel chunk ref (<prefix>_cNNN.sdf); NULL = none. */
    uint32_t            n_meshes;     /**< scene mesh count (per-mesh chunk/rect). */
    size_t              ram_budget;   /**< RAM residency budget in bytes (0=unbounded). */
    size_t              vram_budget;  /**< VRAM residency budget in bytes (0=disabled). */
} client_light_stream_config_t;

/** A lightmap SH chunk streamer. Fields are read-only from outside. */
typedef struct client_light_stream {
    fr_asset_stream_t   stream;        /**< residency manager. */
    fr_chunk_table_t    table;         /**< world-boxed lightmap chunks over @c stream. */
    fr_chunk_entry_t   *entries;       /**< chunk-table storage [n_chunks]. */
    void               *slots;         /**< per-chunk slot array (private type). */
    uint32_t            n_chunks;
    unsigned int        sh_tex[9];      /**< 9 SH coeff GL_TEXTURE_2D_ARRAY pages. */
    uint32_t            n_layers;       /**< resident layers per SH array. */
    int                *layer_chunk;    /**< [n_layers] chunk id in each layer (-1 free). */
    lm_atlas_t          atlas;          /**< SH array dims (max chunk atlas size). */
    lm_atlas_rect_t    *mrect;          /**< [n_meshes] per-mesh atlas rect. */
    int                *mchunk;         /**< [n_meshes] per-mesh chunk id (-1 none). */
    uint32_t            n_meshes;
    gi_sdf_stream_t     sdf;           /**< SDF/voxel chunks (owned; GI borrows via ext_sdf). */
    int                 has_sdf;       /**< 1 = @c sdf loaded. */
    const gl_loader_t  *loader;
} client_light_stream_t;

/**
 * @brief Initialize the streamer: read the lightmap manifest (single-atlas .flm
 *        => one chunk; per-mesh rects + atlas dims), create the 9 SH pages, and
 *        register each chunk with the asset streamer over @p scene_min/@p scene_max
 *        (the chunk's world box drives distance-priority residency).
 * @return false on bad config, missing lightmap, or OOM. Never crashes.
 */
bool client_light_stream_init(client_light_stream_t *ls,
                              const client_light_stream_config_t *cfg,
                              const float scene_min[3], const float scene_max[3]);

/**
 * @brief Per frame (render thread): refresh chunk interest from @p cam_pos and
 *        drive one streaming step (harvest completed decodes, admit + upload the
 *        highest-priority chunks within budget, evict the rest).
 */
void client_light_stream_tick(client_light_stream_t *ls, const float cam_pos[3]);

/**
 * @brief The resident SH-array layer for a mesh's lightmap chunk, or -1 if its
 *        chunk is not resident (mesh falls back to probe GI / ambient). Set into
 *        render_scene item sh_layer each frame.
 */
int client_light_stream_mesh_layer(const client_light_stream_t *ls, uint32_t mesh_idx);

/** @brief Free GL + host resources. Evicts residents first. NULL-safe. */
void client_light_stream_destroy(client_light_stream_t *ls);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_LIGHT_STREAM_H */
