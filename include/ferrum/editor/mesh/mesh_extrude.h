/**
 * @file mesh_extrude.h
 * @brief Face extrusion operations for editable meshes.
 *
 * Two public types: (none — uses mesh_slot_t and mesh_sel_bitset_t)
 * Two public functions: mesh_extrude, mesh_extrude_individual.
 */
#ifndef MESH_EXTRUDE_H
#define MESH_EXTRUDE_H

#include "ferrum/editor/mesh/mesh_slot.h"
#include "ferrum/editor/mesh/mesh_edit.h"

#include <stdbool.h>

/**
 * @brief Extrude selected faces along their averaged normal or a direction.
 *
 * Duplicates boundary vertices of selected faces, offsets them by
 * @p distance along the averaged face normal (or @p direction if non-NULL),
 * and creates side-wall triangles connecting original boundary edges
 * to the new offset edges.
 *
 * @param slot       The mesh slot to modify (not NULL).
 * @param sel        Face selection bitset (not NULL). Updated to select new faces.
 * @param distance   Extrusion distance (may be negative for inward).
 * @param direction  Optional 3-float unit direction vector. NULL = use face normal.
 * @return true on success, false on empty selection or error.
 *
 * Ownership: caller owns slot and sel. Slot is modified in-place.
 * Nullability: slot and sel must not be NULL. direction may be NULL.
 */
bool mesh_extrude(mesh_slot_t *slot,
                  mesh_sel_bitset_t *sel,
                  float distance,
                  const float *direction);

/**
 * @brief Extrude each selected face independently (no shared edges).
 *
 * Each face gets its own set of duplicated vertices and side walls,
 * creating separate pillars even for adjacent selected faces.
 *
 * @param slot     The mesh slot to modify (not NULL).
 * @param sel      Face selection bitset (not NULL). Updated to select new faces.
 * @param distance Extrusion distance.
 * @return true on success, false on empty selection or error.
 *
 * Ownership: caller owns slot and sel. Slot is modified in-place.
 * Nullability: slot and sel must not be NULL.
 */
bool mesh_extrude_individual(mesh_slot_t *slot,
                             mesh_sel_bitset_t *sel,
                             float distance);

#endif /* MESH_EXTRUDE_H */
