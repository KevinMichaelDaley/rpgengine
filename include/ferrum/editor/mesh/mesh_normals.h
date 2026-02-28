/**
 * @file mesh_normals.h
 * @brief Normal operations, face split, and detach.
 *
 * Functions: mesh_flip_normals, mesh_recalculate_normals,
 *            mesh_split_selection, mesh_detach.
 */
#ifndef MESH_NORMALS_H
#define MESH_NORMALS_H

#include "ferrum/editor/mesh/mesh_slot.h"
#include "ferrum/editor/mesh/mesh_edit.h"

#include <stdbool.h>

/**
 * @brief Flip face winding for selected faces (reverse normals).
 *
 * @param slot Mesh to modify. Not NULL.
 * @param sel  Face selection bitset. Not NULL.
 * @return true on success.
 */
bool mesh_flip_normals(mesh_slot_t *slot, const mesh_sel_bitset_t *sel);

/**
 * @brief Recalculate all vertex normals from face geometry.
 *
 * Computes area-weighted average of incident face normals per vertex.
 *
 * @param slot Mesh to modify. Not NULL.
 * @return true on success.
 */
bool mesh_recalculate_normals(mesh_slot_t *slot);

/**
 * @brief Split shared vertices at selection boundary.
 *
 * Vertices shared between selected and unselected faces are
 * duplicated so selected faces get independent vertex copies.
 *
 * @param slot Mesh to modify. Not NULL.
 * @param sel  Face selection bitset. Not NULL.
 * @return true on success.
 */
bool mesh_split_selection(mesh_slot_t *slot, const mesh_sel_bitset_t *sel);

/**
 * @brief Detach selected faces to a target slot.
 *
 * Removes selected faces from source, copies them to target.
 *
 * @param slot   Source mesh. Not NULL.
 * @param sel    Face selection. Not NULL.
 * @param target Target mesh slot (may be empty). Not NULL.
 * @return true on success.
 */
bool mesh_detach(mesh_slot_t *slot, const mesh_sel_bitset_t *sel,
                 mesh_slot_t *target);

#endif /* MESH_NORMALS_H */
