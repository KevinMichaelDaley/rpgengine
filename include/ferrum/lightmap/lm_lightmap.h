/**
 * @file lm_lightmap.h
 * @brief A per-surface lightmap: a grid of luxels extracted from a surface,
 *        the container the baker's passes read and write.
 *
 * @ref lm_lightmap_from_surface dices a surface into its res_u x res_v luxel
 * grid (positions from the surface parametrisation, normal/albedo/emissive
 * inherited, SH cleared) into arena storage. Passes then accumulate incident
 * radiance into each luxel's SH; @ref lm_lightmap_readback evaluates the SH
 * against each luxel's normal to produce a linear-RGB irradiance image for the
 * atlas / preview.
 *
 * Ownership: luxels are allocated from the caller's @p arena. Nullability: all
 * pointer args non-NULL. Errors: from_surface returns false on arena
 * exhaustion. Side effects: none beyond the written buffers.
 */
#ifndef FERRUM_LIGHTMAP_LM_LIGHTMAP_H
#define FERRUM_LIGHTMAP_LM_LIGHTMAP_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/lightmap/lm_types.h"
#include "ferrum/memory/arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A grid of luxels covering one surface. */
typedef struct lm_lightmap {
    lm_luxel_t *luxels; /**< res_u * res_v, row-major (iv outer, iu inner). */
    uint32_t    res_u;  /**< Columns. */
    uint32_t    res_v;  /**< Rows. */
} lm_lightmap_t;

/** Luxel at grid (iu, iv). No bounds checking. */
static inline lm_luxel_t *lm_lightmap_at(const lm_lightmap_t *lm, uint32_t iu,
                                         uint32_t iv)
{
    return &lm->luxels[(size_t)iv * lm->res_u + iu];
}

/**
 * @brief Allocate and fill a lightmap from @p surface: each luxel gets its
 *        world position, the surface normal/albedo/emissive, and a zeroed SH.
 *        Returns false on arena exhaustion.
 */
bool lm_lightmap_from_surface(lm_lightmap_t *lm, const lm_surface_t *surface,
                              arena_t *arena);

/**
 * @brief Evaluate every luxel's SH against its normal into a linear-RGB image
 *        @p out_rgb (3 floats per luxel, row-major, clamped to >= 0). @p out_rgb
 *        must hold at least res_u*res_v*3 floats.
 */
void lm_lightmap_readback(const lm_lightmap_t *lm, float *out_rgb);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_LM_LIGHTMAP_H */
