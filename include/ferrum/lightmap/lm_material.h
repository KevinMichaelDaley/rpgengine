/**
 * @file lm_material.h
 * @brief Material-id -> (albedo, emissive) table for shading SVO reflectors.
 *
 * SVO voxels carry only a 16-bit @c material id (see npc_svo_node_t), not a
 * colour. When a gather/bounce ray leaves the near-field luxel patches and hits
 * a distant voxel, the solver looks up that id here to get the reflector's
 * diffuse albedo and emissive radiance. The table is a flat array indexed by id
 * with a fallback for unknown ids.
 *
 * Ownership: the table BORROWS the @p entries array (caller owns it). POD.
 * Nullability: @p table non-NULL; @p entries may be NULL only if count == 0.
 */
#ifndef FERRUM_LIGHTMAP_LM_MATERIAL_H
#define FERRUM_LIGHTMAP_LM_MATERIAL_H

#include <stdint.h>

#include "ferrum/math/vec3.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Diffuse surface response of a reflector. */
typedef struct lm_material {
    vec3_t albedo;   /**< Diffuse reflectance (0..1 per channel). */
    vec3_t emissive; /**< Emitted radiance (per channel). */
} lm_material_t;

/** Flat material table indexed by SVO voxel material id. */
typedef struct lm_material_table {
    const lm_material_t *entries;  /**< entries[0..count). */
    uint16_t             count;    /**< Number of ids. */
    lm_material_t        fallback; /**< Returned for ids >= count. */
} lm_material_table_t;

/** Material for voxel material id @p id, or the table's fallback if unknown. */
lm_material_t lm_material_lookup(const lm_material_table_t *table, uint16_t id);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_LM_MATERIAL_H */
