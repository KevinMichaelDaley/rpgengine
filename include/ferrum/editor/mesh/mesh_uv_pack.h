/**
 * @file mesh_uv_pack.h
 * @brief UV island packing and texel density utilities.
 *
 * Types: none (uses mesh_uv_island_set_t from mesh_uv_smart.h).
 *
 * Ownership: modifies UV coordinates in-place.
 * Nullability: NULL pointers handled gracefully.
 * Thread safety: not thread-safe.
 */
#ifndef FERRUM_EDITOR_MESH_UV_PACK_H
#define FERRUM_EDITOR_MESH_UV_PACK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ferrum/editor/mesh/mesh_uv_smart.h"
#include <stdbool.h>

/* ------------------------------------------------------------------ */
/* UV island packing (mesh_uv_pack.c)                                  */
/* ------------------------------------------------------------------ */

/**
 * @brief Pack UV islands into the [0,1] UV square.
 *
 * Uses a simple shelf-based bin packing algorithm. Each island is
 * scaled and placed to minimize wasted space.
 *
 * @param slot        Mesh with UVs to rearrange. Not NULL.
 * @param islands     Island set from mesh_uv_find_islands. Not NULL.
 * @param padding     Padding between islands (in UV units, e.g. 0.01).
 * @param resolution  Texture resolution hint (affects quantization).
 * @return true on success.
 */
bool mesh_uv_pack_islands(mesh_slot_t *slot,
                          const mesh_uv_island_set_t *islands,
                          float padding,
                          uint32_t resolution);

/* ------------------------------------------------------------------ */
/* Texel density (mesh_uv_density.c)                                   */
/* ------------------------------------------------------------------ */

/**
 * @brief Calculate average texel density (pixels per world unit).
 *
 * Compares UV-space triangle area to world-space triangle area
 * across all faces to compute average texel density.
 *
 * @param slot        Mesh with UVs. NULL returns 0.
 * @param resolution  Texture resolution in pixels.
 * @return Average texel density, or 0 on error.
 */
float mesh_uv_texel_density(const mesh_slot_t *slot, uint32_t resolution);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_MESH_UV_PACK_H */
