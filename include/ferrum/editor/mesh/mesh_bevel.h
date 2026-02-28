/**
 * @file mesh_bevel.h
 * @brief Edge bevel and vertex bevel (chamfer) operations.
 *
 * Public functions: mesh_bevel_edges, mesh_bevel_vertices.
 */
#ifndef MESH_BEVEL_H
#define MESH_BEVEL_H

#include "ferrum/editor/mesh/mesh_slot.h"
#include "ferrum/editor/mesh/mesh_edit.h"
#include "ferrum/editor/mesh/mesh_selection.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Bevel selected edges — replace each edge with a chamfer strip.
 *
 * For each selected edge, splits adjacent faces at @p amount distance
 * from the edge and fills the gap with new faces. With @p segments > 1,
 * creates a rounded profile.
 *
 * @param slot     Mesh to modify. Not NULL.
 * @param sel      Edge selection bitset (indices into edge table). Not NULL.
 * @param et       Edge table for the mesh. Not NULL.
 * @param amount   Bevel width from edge.
 * @param segments Number of bevel segments (1 = flat chamfer).
 * @return true on success, false on empty selection or error.
 *
 * Ownership: caller owns all parameters. Slot modified in-place.
 */
bool mesh_bevel_edges(mesh_slot_t *slot,
                      const mesh_sel_bitset_t *sel,
                      const mesh_edge_table_t *et,
                      float amount,
                      uint32_t segments);

/**
 * @brief Bevel selected vertices — replace each vertex with a polygon.
 *
 * Each selected vertex is replaced by a small polygon with one new
 * vertex per incident edge, positioned @p amount from the original.
 *
 * @param slot   Mesh to modify. Not NULL.
 * @param sel    Vertex selection bitset. Not NULL.
 * @param amount Bevel distance from vertex.
 * @return true on success, false on empty selection or error.
 *
 * Ownership: caller owns all parameters. Slot modified in-place.
 */
bool mesh_bevel_vertices(mesh_slot_t *slot,
                         const mesh_sel_bitset_t *sel,
                         float amount);

#endif /* MESH_BEVEL_H */
