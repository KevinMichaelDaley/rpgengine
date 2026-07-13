/**
 * @file lm_atlas.h
 * @brief Lightmap atlas packing prepass: pack per-surface lightmaps into one
 *        image and remap each surface's uv1 into its atlas rectangle.
 *
 * Meshes arrive with per-object lightmap UVs already laid out in [0,1]^2; the
 * baker packs every surface's res_u x res_v tile into a single scene atlas so
 * one image holds them all. @ref lm_atlas_pack places each input rectangle
 * (its pixel size in @c w/@c h) at an @c x/@c y in the atlas via a
 * height-sorted shelf packer with a padding gutter, and reports the final atlas
 * dimensions. @ref lm_atlas_remap_uv then turns a surface-local uv into the
 * atlas uv.
 *
 * Ownership: none -- @p rects is caller-owned and written in place. Nullability:
 * pointers non-NULL. Errors: pack returns false if a rectangle is wider than
 * @p max_width (cannot fit any shelf). Offline / not perf-critical.
 */
#ifndef FERRUM_LIGHTMAP_LM_ATLAS_H
#define FERRUM_LIGHTMAP_LM_ATLAS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One packed rectangle: @c w/@c h are inputs, @c x/@c y filled by the packer. */
typedef struct lm_atlas_rect {
    uint32_t w, h; /**< Tile size in luxels (input). */
    uint32_t x, y; /**< Placement origin in the atlas (output). */
} lm_atlas_rect_t;

/** Final atlas dimensions. */
typedef struct lm_atlas {
    uint32_t width;  /**< == max_width. */
    uint32_t height; /**< Grown to fit all shelves. */
} lm_atlas_t;

/**
 * @brief Shelf-pack @p rects[0..count) into an atlas at most @p max_width wide,
 *        leaving a @p padding gutter around each tile. Writes each rect's x/y
 *        and fills @p out with the atlas size. Returns false if any tile is too
 *        wide to fit.
 */
bool lm_atlas_pack(lm_atlas_rect_t *rects, uint32_t count, uint32_t max_width,
                   uint32_t padding, lm_atlas_t *out);

/**
 * @brief Map a surface-local uv (each in [0,1]) into the atlas uv for @p rect.
 */
static inline void lm_atlas_remap_uv(const lm_atlas_rect_t *rect,
                                     const lm_atlas_t *atlas, float lu, float lv,
                                     float *out_u, float *out_v)
{
    *out_u = ((float)rect->x + lu * (float)rect->w) / (float)atlas->width;
    *out_v = ((float)rect->y + lv * (float)rect->h) / (float)atlas->height;
}

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_LM_ATLAS_H */
