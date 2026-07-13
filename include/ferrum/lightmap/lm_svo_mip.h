/**
 * @file lm_svo_mip.h
 * @brief Filtered shading pyramid over the SVO for the far-field gather.
 *
 * The far-field gather (@ref lm_farfield_gather) samples distant geometry
 * straight from the SVO. Reading a single leaf voxel per ray is noisy (the
 * "far-field speckle") and, for very distant geometry, wasteful. Instead we
 * pre-filter: every interior SVO node stores the solid-leaf-weighted AVERAGE of
 * its subtree's diffuse reflectance and emissive radiance (built bottom-up into
 * the node's @ref npc_svo_node.diffuse / @ref npc_svo_node.emissive fields). A
 * ray then cone-samples a COARSE ancestor whose voxel roughly matches the ray's
 * footprint at the hit distance, giving a smooth, pre-integrated result and
 * letting truly distant rays read a shallow level.
 *
 * Ownership: writes into the borrowed @p svo's nodes. Nullability: build/sample
 * are NULL-safe. Errors: none. Side effects: mutates node colour fields (call
 * once after the SVO material stamp, before the gather). Offline only.
 */
#ifndef FERRUM_LIGHTMAP_LM_SVO_MIP_H
#define FERRUM_LIGHTMAP_LM_SVO_MIP_H

#include <stdint.h>

#include "ferrum/lightmap/lm_material.h"
#include "ferrum/math/vec3.h"
#include "ferrum/npc/npc_svo.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A filtered voxel shade: average reflectance + emitted radiance of a subtree. */
typedef struct lm_svo_shade {
    vec3_t diffuse;  /**< Average diffuse reflectance (albedo). */
    vec3_t emissive; /**< Average emitted radiance. */
} lm_svo_shade_t;

/**
 * @brief Build the shading pyramid: fill every node's diffuse/emissive with the
 *        solid-leaf-weighted average of its subtree, using @p table to shade
 *        leaves by material id. Recurses from the root (node 0). NULL-safe.
 */
void lm_svo_mip_build(npc_svo_grid_t *svo, const lm_material_table_t *table);

/**
 * @brief Build the pyramid from per-luxel material instead of a table: splat
 *        each luxel's albedo/emissive into the leaf voxel it occupies (averaged
 *        when several share a voxel), then average up the octree. Use this when
 *        the surface material is per-texel (e.g. sampled from an albedo image)
 *        rather than a flat material id. @p count is scratch sized to the node
 *        count. Reads @p pos[i]/@p albedo[i]/@p emissive[i] for i in [0,@p n).
 */
void lm_svo_mip_splat_luxels(npc_svo_grid_t *svo, const vec3_t *pos,
                             const vec3_t *albedo, const vec3_t *emissive,
                             uint32_t n, uint32_t *count);

/**
 * @brief Sample the pyramid at @p leaf_node's ancestor @p levels_up levels
 *        toward the root (0 = the node itself; clamps at the root). Returns the
 *        black shade if @p svo is NULL or @p leaf_node is invalid.
 */
lm_svo_shade_t lm_svo_mip_sample(const npc_svo_grid_t *svo, uint32_t leaf_node,
                                 uint32_t levels_up);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_LM_SVO_MIP_H */
