/**
 * @file lm_mesh_bake.h
 * @brief Bake a scene of triangle meshes to an SH lightmap atlas.
 *
 * The triangle-mesh counterpart of lm_bake: packs each mesh's lightmap-UV layout
 * into an atlas region sized by its lightmap_resolution, luxelizes covered
 * texels (world pos/normal via barycentric), stamps the meshes into the SVO as
 * occluders/reflectors, bakes the analytic lights' direct illumination into the
 * SH, then runs progressive radiosity for the bounces. @ref
 * lm_mesh_bake_readback_sh reads one SH coefficient back into an atlas image.
 *
 * Unlike lm_bake's indirect-only analytic seeding, the mesh baker treats the
 * lights as BAKED (their direct term is deposited into the lightmap), which is
 * what a static directional sun needs.
 *
 * Ownership: result buffers are arena-allocated. Diffuse-only, offline.
 */
#ifndef FERRUM_LIGHTMAP_LM_MESH_BAKE_H
#define FERRUM_LIGHTMAP_LM_MESH_BAKE_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/lightmap/lm_atlas.h"
#include "ferrum/lightmap/lm_bake.h"   /* lm_bake_config_t */
#include "ferrum/lightmap/lm_lightmap.h"
#include "ferrum/lightmap/lm_mesh.h"
#include "ferrum/memory/arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Output of a mesh bake: solved luxels + their atlas coordinates. */
typedef struct lm_mesh_bake_result {
    lm_lightmap_t combined;    /**< all luxels (res_u=n_luxels, res_v=1). */
    uint32_t     *atlas_x;     /**< atlas X per luxel. */
    uint32_t     *atlas_y;     /**< atlas Y per luxel. */
    float        *luxel_areas; /**< per-luxel world area. */
    lm_atlas_t    atlas;       /**< packed atlas dimensions. */
    uint32_t      n_luxels;
} lm_mesh_bake_result_t;

/**
 * @brief Bake @p scene per @p config into @p result. @c config.atlas_width /
 *        padding / svo / direct-not-used / farfield / solve / seed apply.
 *        Returns false on arena exhaustion or an unfit atlas.
 */
bool lm_mesh_bake(const lm_mesh_scene_t *scene, const lm_bake_config_t *config,
                  lm_mesh_bake_result_t *result, arena_t *arena);

/**
 * @brief Read back one SH coefficient (0..8) into a linear-RGB atlas image
 *        @p out_rgb (atlas.width*atlas.height*3 floats, cleared then filled at
 *        each luxel's atlas texel).
 */
void lm_mesh_bake_readback_sh(const lm_mesh_bake_result_t *result,
                              uint32_t coeff, float *out_rgb);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_LM_MESH_BAKE_H */
