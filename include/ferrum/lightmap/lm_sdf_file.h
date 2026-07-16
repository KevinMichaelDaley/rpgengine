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

/** A loaded distance field: grid metadata + the dense distances. */
typedef struct lm_sdf_data {
    int32_t dims[3];   /**< Grid resolution (cells per axis). */
    float   voxel;     /**< Cell edge length (metres). */
    float   origin[3]; /**< World-space minimum corner. */
    float  *dist;      /**< dims.x*dims.y*dims.z floats, x fastest (owned). */
} lm_sdf_data_t;

/**
 * @brief Write a distance field to @p path (see the format above). @p dims
 *        components must be >= 1. Returns false on a NULL argument, a
 *        non-positive dimension, or an IO error.
 */
bool lm_sdf_save(const char *path, const int32_t dims[3], float voxel,
                 const float origin[3], const float *dist);

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
