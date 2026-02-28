/**
 * @file mesh_uv_smart.h
 * @brief Smart UV unwrap: angle-based island detection and conformal flatten.
 *
 * Types: mesh_uv_island_t, mesh_uv_island_set_t.
 *
 * Algorithm:
 *  1. Detect sharp edges where dihedral angle > threshold.
 *  2. Flood-fill connected faces to form UV islands.
 *  3. Flatten each island to 2D via conformal mapping.
 *  4. Pack islands into [0,1] UV space.
 *
 * Ownership: mesh_uv_island_set_t owns its face_indices arrays.
 * Nullability: NULL pointers handled gracefully.
 * Thread safety: not thread-safe.
 */
#ifndef FERRUM_EDITOR_MESH_UV_SMART_H
#define FERRUM_EDITOR_MESH_UV_SMART_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ferrum/editor/mesh/mesh_slot.h"
#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */

/** @brief Maximum UV islands per mesh. */
#define MESH_UV_MAX_ISLANDS 256

/* ------------------------------------------------------------------ */
/* Types                                                               */
/* ------------------------------------------------------------------ */

/**
 * @brief A single UV island: a connected group of faces.
 *
 * face_indices is a heap-allocated array of face indices.
 */
typedef struct mesh_uv_island {
    uint32_t *face_indices;  /**< Heap-allocated face index array. */
    uint32_t  face_count;    /**< Number of faces in this island. */
    uint32_t  face_capacity; /**< Allocated capacity. */
} mesh_uv_island_t;

/**
 * @brief Set of UV islands for a mesh.
 */
typedef struct mesh_uv_island_set {
    mesh_uv_island_t islands[MESH_UV_MAX_ISLANDS]; /**< Island array. */
    uint32_t count;                                 /**< Active island count. */
} mesh_uv_island_set_t;

/* ------------------------------------------------------------------ */
/* Island detection (mesh_uv_island.c)                                 */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize an island set to empty.
 * @param set  Set to initialize. Not NULL.
 */
void mesh_uv_island_set_init(mesh_uv_island_set_t *set);

/**
 * @brief Free all island face_indices arrays.
 * @param set  Set to destroy. NULL is safe.
 */
void mesh_uv_island_set_destroy(mesh_uv_island_set_t *set);

/**
 * @brief Detect UV islands by splitting at sharp edges.
 *
 * Faces connected across edges with dihedral angle < @p angle_threshold
 * are grouped into the same island. Edges with angle >= threshold create
 * island boundaries.
 *
 * @param slot             Mesh to analyze. NULL returns 0.
 * @param out_islands      Output island set. NULL returns 0.
 * @param angle_threshold  Maximum dihedral angle (radians) for same-island.
 * @return Number of islands found.
 */
uint32_t mesh_uv_find_islands(const mesh_slot_t *slot,
                              mesh_uv_island_set_t *out_islands,
                              float angle_threshold);

/* ------------------------------------------------------------------ */
/* Smart unwrap (mesh_uv_smart.c)                                      */
/* ------------------------------------------------------------------ */

/**
 * @brief Perform smart UV unwrap on the mesh.
 *
 * Splits mesh into islands at sharp edges, flattens each island
 * via conformal mapping, and packs islands into [0,1] UV space.
 *
 * @param slot              Mesh. Not NULL.
 * @param angle_threshold   Edge angle threshold (radians).
 * @param stretch_weight    0=angle preserving, 1=area preserving.
 * @return true on success.
 */
bool mesh_uv_smart_unwrap(mesh_slot_t *slot,
                           float angle_threshold,
                           float stretch_weight);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_MESH_UV_SMART_H */
