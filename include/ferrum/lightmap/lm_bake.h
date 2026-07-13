/**
 * @file lm_bake.h
 * @brief Lightmap bake orchestrator: scene -> luxels -> atlas -> SVO -> direct
 *        -> indirect seed -> far-field -> radiosity solve -> readback.
 *
 * @ref lm_bake runs the whole offline pipeline for a @ref lm_scene: it stamps
 * every surface into a material-carrying SVO, dices all surfaces into one
 * combined luxel array packed into a scene atlas, bakes full direct lighting
 * from emissive/area surfaces, seeds analytic lights' first bounce, gathers the
 * SVO far field, then runs progressive form-factor radiosity across all luxels
 * (so colour bleeds between surfaces). @ref lm_bake_readback evaluates the
 * solved SH into a linear-RGB atlas image.
 *
 * Ownership: all result buffers are arena-allocated from the caller's @p arena
 * and live as long as it does. Nullability: pointers non-NULL. Errors: returns
 * false on arena exhaustion or an unfit atlas. Offline / not perf-critical.
 */
#ifndef FERRUM_LIGHTMAP_LM_BAKE_H
#define FERRUM_LIGHTMAP_LM_BAKE_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/lightmap/lm_atlas.h"
#include "ferrum/lightmap/lm_lightmap.h"
#include "ferrum/lightmap/lm_scene.h"
#include "ferrum/memory/arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Output of a bake: the solved combined lightmap and its atlas layout. */
typedef struct lm_bake_result {
    lm_lightmap_t    combined;        /**< All luxels (res_u=n_luxels, res_v=1). */
    uint32_t        *surface_offsets; /**< n_surfaces+1 luxel offsets (prefix). */
    float           *luxel_areas;     /**< Per-luxel patch area. */
    lm_atlas_rect_t *rects;           /**< n_surfaces atlas placements. */
    lm_atlas_t       atlas;           /**< Packed atlas dimensions. */
    uint32_t         n_surfaces;
    uint32_t         n_luxels;
} lm_bake_result_t;

/**
 * @brief Bake @p scene per @p config into @p result. Returns false on arena
 *        exhaustion or if the atlas cannot fit a surface.
 */
bool lm_bake(const lm_scene_t *scene, const lm_bake_config_t *config,
             lm_bake_result_t *result, arena_t *arena);

/**
 * @brief Evaluate the solved lightmap into a linear-RGB atlas image
 *        @p out_rgb (atlas.width*atlas.height*3 floats, row-major, cleared to
 *        0 then filled per surface rect). Values are clamped to >= 0.
 */
void lm_bake_readback(const lm_bake_result_t *result, float *out_rgb);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_LM_BAKE_H */
