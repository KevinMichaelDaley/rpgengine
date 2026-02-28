/**
 * @file mesh_triangulate.h
 * @brief Triangulate and quadrangulate operations.
 *
 * Functions: mesh_triangulate, mesh_tris_to_quads.
 */
#ifndef MESH_TRIANGULATE_H
#define MESH_TRIANGULATE_H

#include "ferrum/editor/mesh/mesh_slot.h"
#include "ferrum/editor/mesh/mesh_edit.h"

#include <stdbool.h>

/**
 * @brief Triangulate selected n-gon faces (future use).
 *
 * Currently a no-op since all faces are already triangles in
 * the mesh_slot_t representation. Provided for API completeness.
 *
 * @param slot Mesh to modify.
 * @param sel  Face selection.
 * @return true.
 */
bool mesh_triangulate(mesh_slot_t *slot, const mesh_sel_bitset_t *sel);

/**
 * @brief Convert triangle pairs into quads (merge coplanar adjacent tris).
 *
 * Finds pairs of adjacent triangles that are nearly coplanar
 * (normal dot product > threshold) and marks them as belonging
 * to the same polygroup. Does not change topology (still stored
 * as triangles internally) but tags merged pairs.
 *
 * @param slot      Mesh to modify. Not NULL.
 * @param threshold Normal similarity threshold (0..1, 0.99 = nearly flat).
 * @return Number of triangle pairs merged, or 0 on error.
 */
uint32_t mesh_tris_to_quads(mesh_slot_t *slot, float threshold);

#endif /* MESH_TRIANGULATE_H */
