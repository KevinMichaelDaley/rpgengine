/**
 * @file mesh_select.h
 * @brief Mesh element selection operations.
 *
 * Provides bulk selection: by indices, select all, invert, clear,
 * flood fill, grow, shrink, select similar. All operate on a
 * mesh_sel_bitset_t.
 *
 * Ownership: caller owns the bitset and mesh slot.
 * Nullability: NULL args are safe no-ops.
 * Thread safety: not thread-safe.
 */
#ifndef FERRUM_EDITOR_MESH_SELECT_H
#define FERRUM_EDITOR_MESH_SELECT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "ferrum/editor/mesh/mesh_edit.h"
#include "ferrum/editor/mesh/mesh_slot.h"

/* ------------------------------------------------------------------------ */
/* Basic selection (mesh_select.c)                                           */
/* ------------------------------------------------------------------------ */

/** @brief Set bits at each given index. Additive. */
void mesh_select_by_indices(mesh_sel_bitset_t *sel,
                            const uint32_t *indices, uint32_t count);

/** @brief Clear bits at each given index. */
void mesh_deselect_by_indices(mesh_sel_bitset_t *sel,
                              const uint32_t *indices, uint32_t count);

/** @brief Select all elements [0, total). */
void mesh_select_all(mesh_sel_bitset_t *sel, uint32_t total);

/** @brief Invert all bits in [0, total). */
void mesh_select_invert(mesh_sel_bitset_t *sel, uint32_t total);

/* ------------------------------------------------------------------------ */
/* Topological selection (mesh_select_topo.c)                                */
/* ------------------------------------------------------------------------ */

/**
 * @brief Flood fill face selection from a seed face.
 *
 * Selects all faces connected to `seed_face` via shared edges.
 * Additive to existing selection.
 */
void mesh_select_flood(const mesh_slot_t *slot, mesh_sel_bitset_t *sel,
                       uint32_t seed_face);

/**
 * @brief Select faces with similar normal direction.
 *
 * @param slot       Mesh slot.
 * @param sel        Selection bitset (additive).
 * @param normal     Reference normal [3].
 * @param threshold  Angle threshold in degrees.
 */
void mesh_select_similar_normal(const mesh_slot_t *slot,
                                mesh_sel_bitset_t *sel,
                                const float normal[3],
                                float threshold);

/* ------------------------------------------------------------------------ */
/* Grow / Shrink (mesh_select_grow.c)                                        */
/* ------------------------------------------------------------------------ */

/**
 * @brief Expand face selection by `steps` rings of adjacency.
 */
void mesh_select_grow(const mesh_slot_t *slot, mesh_sel_bitset_t *sel,
                      uint32_t steps);

/**
 * @brief Contract face selection by `steps` rings.
 *
 * Deselects faces that have any neighbor NOT selected.
 */
void mesh_select_shrink(const mesh_slot_t *slot, mesh_sel_bitset_t *sel,
                        uint32_t steps);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_MESH_SELECT_H */
