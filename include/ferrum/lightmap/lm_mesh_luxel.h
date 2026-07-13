/**
 * @file lm_mesh_luxel.h
 * @brief Rasterize a mesh's lightmap UVs into atlas texels -> luxels.
 *
 * For one mesh placed at an atlas rect, this rasterizes each triangle's uv1
 * footprint (scaled into the rect's pixel space) and emits one @ref lm_luxel per
 * covered atlas texel, with world position and normal barycentric-interpolated
 * from the covering triangle and albedo/emissive taken from the mesh. The atlas
 * (x,y) of each luxel is recorded in parallel so the SH result can be written
 * back to the atlas. Each texel is emitted at most once (a caller-provided
 * visited bitmap deduplicates shared triangle edges).
 *
 * Ownership: none -- writes into caller buffers. Nullability: pointers non-NULL.
 */
#ifndef FERRUM_LIGHTMAP_LM_MESH_LUXEL_H
#define FERRUM_LIGHTMAP_LM_MESH_LUXEL_H

#include <stdint.h>

#include "ferrum/lightmap/lm_atlas.h"
#include "ferrum/lightmap/lm_mesh.h"
#include "ferrum/lightmap/lm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Generate luxels for @p mesh at atlas @p rect (in a @p atlas_w x
 *        @p atlas_h atlas).
 * @param mesh       Source mesh (uv1 in [0,1]).
 * @param rect       This mesh's atlas placement (rect.w/h in texels).
 * @param atlas_w    Atlas width (texels).
 * @param atlas_h    Atlas height (texels).
 * @param out_luxels Output luxels (>= rect.w*rect.h capacity).
 * @param out_ax     Output atlas X per luxel (>= rect.w*rect.h).
 * @param out_ay     Output atlas Y per luxel.
 * @param visited    Scratch bitmap of rect.w*rect.h bytes (zeroed by the callee).
 * @return Number of luxels emitted.
 */
uint32_t lm_mesh_luxelize(const lm_mesh_t *mesh, const lm_atlas_rect_t *rect,
                          uint32_t atlas_w, uint32_t atlas_h,
                          lm_luxel_t *out_luxels, uint32_t *out_ax,
                          uint32_t *out_ay, uint8_t *visited);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_LM_MESH_LUXEL_H */
