/**
 * @file mesh_material_ops.h
 * @brief Material lift (eyedropper) and bulk replace operations.
 *
 * Types: none (uses mesh_material_map_t from mesh_material.h).
 *
 * Ownership: does not take ownership of any arguments.
 * Nullability: NULL pointers handled gracefully (return NULL/false).
 * Thread safety: not thread-safe.
 */
#ifndef FERRUM_EDITOR_MESH_MATERIAL_OPS_H
#define FERRUM_EDITOR_MESH_MATERIAL_OPS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ferrum/editor/mesh/mesh_material.h"
#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Material lift (mesh_material_lift.c)                                */
/* ------------------------------------------------------------------ */

/**
 * @brief Sample the material assigned to a face (eyedropper).
 *
 * @param slot  Mesh slot. NULL returns NULL.
 * @param map   Material map. NULL returns NULL.
 * @param face  Face index.
 * @return Material path string, or NULL if unmapped or out of bounds.
 */
const char *mesh_material_lift(const mesh_slot_t *slot,
                               const mesh_material_map_t *map,
                               uint32_t face);

/* ------------------------------------------------------------------ */
/* Material replace (mesh_material_replace.c)                          */
/* ------------------------------------------------------------------ */

/**
 * @brief Replace all occurrences of one material with another.
 *
 * Updates the polygroup→material mapping in @p map so every entry
 * that was @p old_path becomes @p new_path.
 *
 * @param map       Material map. NULL returns false.
 * @param old_path  Path to replace. NULL returns false.
 * @param new_path  Replacement path. NULL returns false.
 * @return true if at least one entry was replaced.
 */
bool mesh_material_replace(mesh_material_map_t *map,
                           const char *old_path,
                           const char *new_path);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_MESH_MATERIAL_OPS_H */
