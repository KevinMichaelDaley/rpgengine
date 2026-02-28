/**
 * @file mesh_inset.h
 * @brief Face inset and outset operations.
 *
 * Public functions: mesh_inset, mesh_outset.
 */
#ifndef MESH_INSET_H
#define MESH_INSET_H

#include "ferrum/editor/mesh/mesh_slot.h"
#include "ferrum/editor/mesh/mesh_edit.h"

#include <stdbool.h>

/**
 * @brief Inset selected faces, creating a border ring.
 *
 * Each selected face is shrunk toward its centroid by @p amount,
 * and new border triangles fill the gap between original edges
 * and inset edges. If @p depth != 0, inset vertices are offset
 * along the face normal.
 *
 * @param slot   Mesh to modify. Not NULL.
 * @param sel    Face selection bitset. Not NULL.
 * @param amount Inset distance toward centroid (0..1 range typical).
 * @param depth  Optional normal offset for inset face.
 * @return true on success, false on empty selection or error.
 *
 * Ownership: caller owns slot and sel. Slot modified in-place.
 */
bool mesh_inset(mesh_slot_t *slot, mesh_sel_bitset_t *sel,
                float amount, float depth);

/**
 * @brief Outset selected faces (scale outward from centroid).
 *
 * Moves face vertices outward from their centroid by @p amount.
 * Does not create new geometry — modifies vertex positions in-place.
 *
 * @param slot   Mesh to modify. Not NULL.
 * @param sel    Face selection bitset. Not NULL.
 * @param amount Outset distance.
 * @return true on success, false on empty selection or error.
 *
 * Ownership: caller owns slot and sel. Slot modified in-place.
 */
bool mesh_outset(mesh_slot_t *slot, mesh_sel_bitset_t *sel,
                 float amount);

#endif /* MESH_INSET_H */
