/**
 * @file lm_sdf_file.h
 * @brief Serialize a baked signed-distance field (one per bake chunk) to a
 *        sidecar file, and load it back.
 *
 * The GPU bake builds a dense JFA distance field per near chunk to sphere-trace
 * the gather (rpg-bpyr/rpg-fzht). That field is reusable at runtime -- coarse
 * real-time GI / soft shadows / AO for DYNAMIC lights, dynamic-object contact
 * shadows, collision/nav queries -- so it is worth persisting. Rather than
 * bloating the SH lightmap (.flm), each chunk's field is written to its own
 * @c .sdf sidecar (rpg-iudw): a small header (dims, voxel edge, world origin)
 * plus the raw @c dims.x*dims.y*dims.z float distances (negative inside, in
 * metres), laid out so it uploads straight into a GL_R32F 3D texture (x fastest,
 * then y, then z).
 *
 * Format (native-endian): magic "FSDF", uint32 version(=1), int32 dims[3],
 * float voxel, float origin[3], then dims.x*dims.y*dims.z floats.
 *
 * Ownership: @ref lm_sdf_load allocates @c dist; free with @ref
 * lm_sdf_data_free. Nullability: pointers non-NULL. Errors: bool. Offline.
 */
#ifndef FERRUM_LIGHTMAP_LM_SDF_FILE_H
#define FERRUM_LIGHTMAP_LM_SDF_FILE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** A loaded distance field: grid metadata, dense distances, optional albedo.
 *  @c albedo is non-NULL only for v2 files; it carries the voxelised static-scene
 *  diffuse colour so a GI cone hit can tint its bounce (RGB, planar, 3/voxel). */
typedef struct lm_sdf_data {
    int32_t dims[3];   /**< Grid resolution (cells per axis). */
    float   voxel;     /**< Cell edge length (metres). */
    float   origin[3]; /**< World-space minimum corner. */
    float  *dist;      /**< dims.x*dims.y*dims.z floats, x fastest (owned). */
    float  *albedo;    /**< v2 only: dims product * 3 floats (RGB), else NULL (owned). */
} lm_sdf_data_t;

/**
 * @brief Write a distance field to @p path (see the format above). @p dims
 *        components must be >= 1. Returns false on a NULL argument, a
 *        non-positive dimension, or an IO error.
 */
bool lm_sdf_save(const char *path, const int32_t dims[3], float voxel,
                 const float origin[3], const float *dist);

/**
 * @brief Write a distance field PLUS per-voxel RGB albedo (v2). @p albedo holds
 *        dims product * 3 floats (planar RGB, x fastest, same layout as @p dist).
 *        Loads back through @ref lm_sdf_load (which sets @c out->albedo). Same
 *        error semantics as @ref lm_sdf_save; @p albedo must be non-NULL.
 */
bool lm_sdf_save_rgba(const char *path, const int32_t dims[3], float voxel,
                      const float origin[3], const float *dist,
                      const float *albedo);

/**
 * @brief Load an @c .sdf file into @p out (allocates @c out->dist). Returns
 *        false on a NULL argument, IO error, bad magic/version, or a bad size.
 */
bool lm_sdf_load(const char *path, lm_sdf_data_t *out);

/** @brief Free @p data->dist and zero @p data. NULL-safe. */
void lm_sdf_data_free(lm_sdf_data_t *data);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_LIGHTMAP_LM_SDF_FILE_H */
