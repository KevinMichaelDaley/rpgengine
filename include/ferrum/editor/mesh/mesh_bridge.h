/**
 * @file mesh_bridge.h
 * @brief Bridge edges/faces and connect vertices.
 *
 * Functions: mesh_bridge_edges, mesh_connect_vertices.
 */
#ifndef MESH_BRIDGE_H
#define MESH_BRIDGE_H

#include "ferrum/editor/mesh/mesh_slot.h"
#include "ferrum/editor/mesh/mesh_edit.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Bridge two edge loops by creating connecting faces.
 *
 * Takes two arrays of vertex indices (edge loops of equal size)
 * and creates quad strips connecting them.
 *
 * @param slot      Mesh to modify. Not NULL.
 * @param loop_a    First edge loop vertex indices. Not NULL.
 * @param loop_b    Second edge loop vertex indices. Not NULL.
 * @param loop_len  Number of vertices in each loop (must be equal).
 * @return true on success.
 */
bool mesh_bridge_edges(mesh_slot_t *slot,
                       const uint32_t *loop_a,
                       const uint32_t *loop_b,
                       uint32_t loop_len);

/**
 * @brief Connect two vertices by splitting a face.
 *
 * Finds a face containing both v0 and v1 (they must be on the same
 * face but not adjacent). Creates a new edge between them by splitting
 * the face into two.
 *
 * @param slot Mesh to modify. Not NULL.
 * @param v0   First vertex.
 * @param v1   Second vertex.
 * @return true on success.
 */
bool mesh_connect_vertices(mesh_slot_t *slot, uint32_t v0, uint32_t v1);

#endif /* MESH_BRIDGE_H */
