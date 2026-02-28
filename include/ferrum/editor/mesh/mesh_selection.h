/**
 * @file mesh_selection.h
 * @brief Mesh selection conversion and edge table.
 *
 * Provides selection conversion between modes (face↔vertex, face↔edge)
 * and an edge table for efficient edge lookup from triangle indices.
 *
 * Ownership: edge table is heap-allocated; call mesh_edge_table_destroy().
 * Nullability: NULL pointers handled gracefully.
 * Thread safety: not thread-safe.
 */
#ifndef FERRUM_EDITOR_MESH_SELECTION_H
#define FERRUM_EDITOR_MESH_SELECTION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "ferrum/editor/mesh/mesh_slot.h"
#include "ferrum/editor/mesh/mesh_edit.h"

/* ------------------------------------------------------------------------ */
/* Edge table                                                                */
/* ------------------------------------------------------------------------ */

/**
 * @brief A single edge: pair of vertex indices (v0 < v1).
 */
typedef struct mesh_edge {
    uint32_t v0; /**< Lower vertex index. */
    uint32_t v1; /**< Higher vertex index. */
} mesh_edge_t;

/**
 * @brief Edge table — sorted array of unique edges from triangle indices.
 *
 * Built from a mesh_slot_t's index buffer. Each edge appears once.
 * Edges are sorted by (v0, v1) for binary search.
 */
typedef struct mesh_edge_table {
    mesh_edge_t *edges;      /**< Sorted edge array. */
    uint32_t     edge_count; /**< Number of unique edges. */
    uint32_t     capacity;   /**< Allocated capacity. */
} mesh_edge_table_t;

/* Edge table lifecycle (mesh_edge_table.c) */

/**
 * @brief Build an edge table from a mesh slot's triangles.
 *
 * @param table  Table to populate. Must not be NULL.
 * @param slot   Mesh slot to extract edges from. Must not be NULL.
 * @return true on success.
 */
bool mesh_edge_table_build(mesh_edge_table_t *table, const mesh_slot_t *slot);

/**
 * @brief Free edge table memory. NULL-safe.
 */
void mesh_edge_table_destroy(mesh_edge_table_t *table);

/**
 * @brief Find edge index by vertex pair. Returns UINT32_MAX if not found.
 *
 * @param table  Edge table. NULL returns UINT32_MAX.
 * @param v0     First vertex index.
 * @param v1     Second vertex index (order doesn't matter).
 * @return Edge index, or UINT32_MAX.
 */
uint32_t mesh_edge_table_find(const mesh_edge_table_t *table,
                              uint32_t v0, uint32_t v1);

/* ------------------------------------------------------------------------ */
/* Selection conversion (mesh_sel_convert.c)                                 */
/* ------------------------------------------------------------------------ */

/**
 * @brief Convert face selection to vertex selection.
 *
 * For each selected face, selects all 3 of its vertices.
 * Output bitset is cleared first.
 */
void mesh_sel_convert_face_to_vertex(const mesh_slot_t *slot,
                                     const mesh_sel_bitset_t *faces,
                                     mesh_sel_bitset_t *out_verts);

/**
 * @brief Convert vertex selection to face selection.
 *
 * Selects a face only if ALL 3 of its vertices are selected.
 * Output bitset is cleared first.
 */
void mesh_sel_convert_vertex_to_face(const mesh_slot_t *slot,
                                     const mesh_sel_bitset_t *verts,
                                     mesh_sel_bitset_t *out_faces);

/**
 * @brief Convert face selection to edge selection.
 *
 * For each selected face, selects all 3 of its edges.
 * Requires a pre-built edge table.
 */
void mesh_sel_convert_face_to_edge(const mesh_slot_t *slot,
                                   const mesh_edge_table_t *table,
                                   const mesh_sel_bitset_t *faces,
                                   mesh_sel_bitset_t *out_edges);

/**
 * @brief Convert edge selection to face selection.
 *
 * Selects a face if ANY of its edges are selected.
 * Requires a pre-built edge table.
 */
void mesh_sel_convert_edge_to_face(const mesh_slot_t *slot,
                                   const mesh_edge_table_t *table,
                                   const mesh_sel_bitset_t *edges,
                                   mesh_sel_bitset_t *out_faces);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_MESH_SELECTION_H */
