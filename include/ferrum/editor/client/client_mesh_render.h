/**
 * @file client_mesh_render.h
 * @brief Client-side mesh data for rendering (data path, no GL).
 *
 * Types: client_mesh_data_t, client_mesh_edges_t.
 *
 * Converts FVMA binary blobs into CPU-side vertex/index arrays
 * and generates wireframe edge lists and selection highlight data.
 *
 * Ownership: all *_t types own their heap buffers; destroy to free.
 * Nullability: NULL pointers handled gracefully.
 * Thread safety: not thread-safe.
 */
#ifndef FERRUM_EDITOR_CLIENT_MESH_RENDER_H
#define FERRUM_EDITOR_CLIENT_MESH_RENDER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ferrum/editor/mesh/mesh_edit.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Types                                                               */
/* ------------------------------------------------------------------ */

/**
 * @brief CPU-side mesh data ready for GPU upload.
 */
typedef struct client_mesh_data {
    float    *positions;     /**< vec3 per vertex. Heap-allocated. */
    float    *normals;       /**< vec3 per vertex. Heap-allocated (may be NULL). */
    uint32_t *indices;       /**< Triangle indices. Heap-allocated. */
    uint32_t  vertex_count;
    uint32_t  index_count;
} client_mesh_data_t;

/**
 * @brief Wireframe edge list (unique edges from triangle soup).
 */
typedef struct client_mesh_edges {
    uint32_t *edge_indices;  /**< Pairs of vertex indices. Heap-allocated. */
    uint32_t  edge_count;    /**< Number of unique edges. */
} client_mesh_edges_t;

/* ------------------------------------------------------------------ */
/* FVMA → render data (client_mesh_render.c)                           */
/* ------------------------------------------------------------------ */

/**
 * @brief Deserialize FVMA binary into CPU render data.
 *
 * @param out       Output data. Not NULL.
 * @param fvma_buf  FVMA binary buffer. Not NULL.
 * @param fvma_size Buffer size.
 * @return true on success.
 */
bool client_mesh_data_from_fvma(client_mesh_data_t *out,
                                const uint8_t *fvma_buf, size_t fvma_size);

/**
 * @brief Free CPU render data.
 * @param data  Data to destroy. NULL is safe.
 */
void client_mesh_data_destroy(client_mesh_data_t *data);

/* ------------------------------------------------------------------ */
/* Edge extraction (client_mesh_edges.c)                               */
/* ------------------------------------------------------------------ */

/**
 * @brief Extract unique edges from triangle index buffer.
 *
 * @param out          Output edges. Not NULL.
 * @param indices      Triangle indices.
 * @param index_count  Number of indices (multiple of 3).
 * @return true on success.
 */
bool client_mesh_extract_edges(client_mesh_edges_t *out,
                               const uint32_t *indices,
                               uint32_t index_count);

/**
 * @brief Free edge data.
 * @param edges  Edges to destroy. NULL is safe.
 */
void client_mesh_edges_destroy(client_mesh_edges_t *edges);

/* ------------------------------------------------------------------ */
/* Selection highlight (client_mesh_highlight.c)                       */
/* ------------------------------------------------------------------ */

/**
 * @brief Generate highlight index buffer for selected faces.
 *
 * @param indices         Source triangle indices.
 * @param index_count     Number of indices.
 * @param sel_faces       Face selection bitset.
 * @param out_indices     Output: heap-allocated highlight indices.
 * @param out_count       Output: number of highlight indices.
 * @return true on success.
 */
bool client_mesh_face_highlight(const uint32_t *indices,
                                uint32_t index_count,
                                const mesh_sel_bitset_t *sel_faces,
                                uint32_t **out_indices,
                                uint32_t *out_count);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_CLIENT_MESH_RENDER_H */
