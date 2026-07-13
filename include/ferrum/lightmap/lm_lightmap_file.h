/**
 * @file lm_lightmap_file.h
 * @brief Serialize a baked SH lightmap to disk and load it back.
 *
 * A bake is expensive and offline; the runtime loads the result. This stores the
 * atlas dimensions, the 9 SH coefficient images (RGB32F, one per band term), and
 * the per-mesh atlas rects (so the runtime can remap each mesh's lightmap UV into
 * its atlas region). @ref lm_lightmap_save reads the coefficients out of a bake
 * result; @ref lm_lightmap_load reconstructs an @ref lm_lightmap_data the runtime
 * uploads as 9 textures.
 *
 * Format (native-endian): magic "FLM1", uint32 atlas_w, atlas_h, n_coeffs(=9),
 * n_meshes; then 9 * atlas_w*atlas_h*3 floats; then n_meshes lm_atlas_rect_t.
 *
 * Ownership: load allocates the arrays; free with @ref lm_lightmap_data_free.
 */
#ifndef FERRUM_LIGHTMAP_LM_LIGHTMAP_FILE_H
#define FERRUM_LIGHTMAP_LM_LIGHTMAP_FILE_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/lightmap/lm_atlas.h"
#include "ferrum/lightmap/lm_mesh_bake.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A loaded lightmap: 9 SH coefficient atlas images + per-mesh rects. */
typedef struct lm_lightmap_data {
    uint32_t         atlas_w;
    uint32_t         atlas_h;
    uint32_t         n_meshes;
    float           *coeffs[9]; /**< each atlas_w*atlas_h*3 floats. */
    lm_atlas_rect_t *rects;     /**< n_meshes atlas placements. */
} lm_lightmap_data_t;

/**
 * @brief Write the baked lightmap in @p result to @p path. Returns false on IO
 *        error.
 */
bool lm_lightmap_save(const lm_mesh_bake_result_t *result, const char *path);

/**
 * @brief Load a lightmap file into @p out (allocates its arrays). Returns false
 *        on IO error, bad magic, or out of memory.
 */
bool lm_lightmap_load(const char *path, lm_lightmap_data_t *out);

/**
 * @brief Free the arrays owned by @p data and zero it. NULL-safe.
 */
void lm_lightmap_data_free(lm_lightmap_data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_LM_LIGHTMAP_FILE_H */
