/**
 * @file gi_sdf_stream.h
 * @brief Streaming residency for the baked per-chunk SDF (rpg-fo9r / rpg-iudw).
 *
 * The baked SDF sidecars (`<prefix>_cNNN.sdf`) correspond to the scene chunks and
 * are paged the SAME way as the SH lightmap: the visibility prepass
 * (@ref gi_vis_prepass) yields the on-screen chunk set, and only those chunks'
 * distance fields are resident on the GPU as R32F 3D textures (a bounded pool
 * with LRU eviction). The probe cone-trace samples the resident chunks (each
 * covers a world box) combined with dynamic-collider SDFs.
 *
 * Because the SDF chunks are SPATIAL regions, a mesh is mapped to the SDF chunk
 * whose box contains its centroid (@ref gi_sdf_stream_mesh_chunks) so the same
 * prepass machinery drives paging. GL-only (glad). Owns host caches + GL
 * textures; frees them in @ref gi_sdf_stream_destroy.
 */
#ifndef FERRUM_RENDERER_GI_GI_SDF_STREAM_H
#define FERRUM_RENDERER_GI_GI_SDF_STREAM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Max SDF chunks resident on the GPU at once (bounds VRAM). */
#define GI_SDF_MAX_RESIDENT 8

/** One baked SDF chunk cached in host RAM (loaded from a .sdf sidecar). */
typedef struct gi_sdf_chunk_ram {
    int32_t dims[3];
    float   voxel;
    float   origin[3];   /**< world min corner of the chunk box. */
    float  *dist;        /**< dims product floats (owned). */
    float  *albedo;      /**< v2: dims product * 3 RGB (owned); NULL for v1 chunks. */
} gi_sdf_chunk_ram_t;

/** Streaming residency for the scene's baked SDF chunks. */
typedef struct gi_sdf_stream {
    int                 n_chunks;
    gi_sdf_chunk_ram_t *ram;                        /**< [n_chunks] host cache. */
    unsigned int        tex[GI_SDF_MAX_RESIDENT];   /**< resident R32F 3D textures. */
    int                 slot_chunk[GI_SDF_MAX_RESIDENT]; /**< layer -> chunk (-1). */
    int                 slot_used[GI_SDF_MAX_RESIDENT];  /**< LRU frame stamp. */
    int                *page;                        /**< [n_chunks] chunk -> slot (-1). */
    int                 frame;
    int                 resident;                    /**< resident chunk count this frame. */
    int                 resident_slot[GI_SDF_MAX_RESIDENT]; /**< the resident slots this frame. */
    float              *upload_rgba;                  /**< scratch: interleave dist+albedo -> RGBA. */
    int                 slot_dims[3];                 /**< allocated 3D-texture dims (max chunk). */
    int                *scan_cc;                       /**< [n_chunks] source file index (on-demand load). */
} gi_sdf_stream_t;

/**
 * @brief Load every `<prefix>_cNNN.sdf` (N scanned from 0) into host RAM and
 *        create the bounded 3D-texture pool. Returns the chunk count, or -1.
 */
int gi_sdf_stream_load(gi_sdf_stream_t *s, const char *prefix);

/**
 * @brief On-demand variant of @ref gi_sdf_stream_load: scan every
 *        `<prefix>_cNNN.sdf` HEADER only (dims/voxel/origin/file-index, no field
 *        data) and create the GPU pool. Chunk distance/albedo then loads on demand
 *        via @ref gi_sdf_stream_chunk_load (driven by the asset streamer). Returns
 *        the chunk count, or -1. Needs a current GL context.
 */
int gi_sdf_stream_scan(gi_sdf_stream_t *s, const char *prefix);

/** @brief Load chunk @p c's distance (+ albedo) into host RAM from @p prefix.
 *  @return RAM bytes loaded (0 on failure). No GL. Safe on a job fiber. */
size_t gi_sdf_stream_chunk_load(gi_sdf_stream_t *s, int c, const char *prefix);

/** @brief Free chunk @p c's host RAM + evict its GPU slot (render thread). */
void gi_sdf_stream_chunk_evict(gi_sdf_stream_t *s, int c);

/** @brief 1 if chunk @p c's distance field is RAM-resident, else 0. */
int gi_sdf_stream_chunk_loaded(const gi_sdf_stream_t *s, int c);

/** @brief World boxes (3 floats each) of the RAM-resident chunks, up to @p cap.
 *  Used to gate streamed probe sets to the loaded chunks. Returns the count. */
int gi_sdf_stream_resident_boxes(const gi_sdf_stream_t *s, float *out_min,
                                 float *out_max, int cap);

/**
 * @brief Fill @p out_min / @p out_max (3 floats per chunk) with each SDF chunk's
 *        world box, for the WORLD-mode visibility prepass to classify fragments
 *        by world position. Returns the chunk count.
 */
int gi_sdf_stream_boxes(const gi_sdf_stream_t *s, float *out_min, float *out_max);

/**
 * @brief Page the visible chunks (@p visible[0..n_chunks), from the prepass) into
 *        residency, evicting LRU slots, and record the resident set for this
 *        frame. Call once per frame after the prepass.
 */
void gi_sdf_stream_page(gi_sdf_stream_t *s, const uint8_t *visible);

/**
 * @brief Host-side sample of the baked combined SDF at world @p p: the min over
 *        the chunks whose box contains @p p (large positive if none). Used to
 *        prune probes that aren't near any surface (adaptive placement).
 */
float gi_sdf_stream_sample(const gi_sdf_stream_t *s, const float p[3]);

/** @brief Free host caches + GL textures. NULL-safe. */
void gi_sdf_stream_destroy(gi_sdf_stream_t *s);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_GI_GI_SDF_STREAM_H */
