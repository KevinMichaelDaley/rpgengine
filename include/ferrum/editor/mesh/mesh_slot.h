/**
 * @file mesh_slot.h
 * @brief Editable mesh slot — single indexed triangle mesh.
 *
 * A mesh_slot_t holds vertex attribute buffers (positions, normals,
 * tangents, UVs, colors) and triangle indices. All buffers grow
 * dynamically via reserve / add operations.
 *
 * Ownership: init() allocates, destroy() frees. The caller owns the
 * mesh_slot_t struct; internal buffers are heap-allocated.
 *
 * Nullability: NULL slot pointer is handled gracefully (returns false/0).
 * Thread safety: not thread-safe — access from one thread at a time.
 */
#ifndef FERRUM_EDITOR_MESH_SLOT_H
#define FERRUM_EDITOR_MESH_SLOT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------------------ */
/* Constants                                                                 */
/* ------------------------------------------------------------------------ */

/** @brief Maximum vertices per mesh slot. */
#define MESH_SLOT_MAX_VERTICES  65536

/** @brief Maximum indices per mesh slot (65536 * 3 = 196608). */
#define MESH_SLOT_MAX_INDICES   196608

/** @brief Number of UV channels. */
#define MESH_SLOT_UV_CHANNELS   2

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief Single editable mesh — indexed triangle mesh with full vertex
 *        attributes.
 *
 * Buffers are interleaved per-component:
 *   positions[vertex_count * 3]  (x, y, z)
 *   normals[vertex_count * 3]    (nx, ny, nz)
 *   tangents[vertex_count * 4]   (tx, ty, tz, tw)
 *   uvs[ch][vertex_count * 2]    (u, v)
 *   colors[vertex_count * 4]     (r, g, b, a)
 *   indices[index_count]         (triangle indices, u32)
 *   polygroup_ids[face_count]    (one u16 per triangle)
 */
typedef struct mesh_slot {
    float    *positions;     /**< vec3 per vertex. */
    float    *normals;       /**< vec3 per vertex. */
    float    *tangents;      /**< vec4 per vertex. */
    float    *uvs[MESH_SLOT_UV_CHANNELS]; /**< vec2 per vertex per channel. */
    float    *colors;        /**< vec4 per vertex. */
    uint32_t *indices;       /**< Triangle index buffer. */
    uint16_t *polygroup_ids; /**< Per-face polygroup ID. */

    uint32_t  vertex_count;    /**< Current number of vertices. */
    uint32_t  vertex_capacity; /**< Allocated vertex capacity. */
    uint32_t  index_count;     /**< Current number of indices. */
    uint32_t  index_capacity;  /**< Allocated index capacity. */
} mesh_slot_t;

/* ------------------------------------------------------------------------ */
/* Lifecycle (mesh_slot.c)                                                   */
/* ------------------------------------------------------------------------ */

/**
 * @brief Initialize a mesh slot with pre-allocated capacity.
 *
 * @param slot           Slot to initialize. Must not be NULL.
 * @param vert_capacity  Initial vertex capacity (0 is valid).
 * @param idx_capacity   Initial index capacity (0 is valid).
 * @return true on success, false on NULL or allocation failure.
 *
 * Side effects: allocates heap memory for all attribute buffers.
 * Ownership: caller owns the slot; call mesh_slot_destroy() to free.
 */
bool mesh_slot_init(mesh_slot_t *slot, uint32_t vert_capacity,
                    uint32_t idx_capacity);

/**
 * @brief Free all buffers owned by the slot.
 *
 * @param slot  Slot to destroy. NULL is a safe no-op.
 *
 * Side effects: frees heap memory; zeroes the struct.
 */
void mesh_slot_destroy(mesh_slot_t *slot);

/**
 * @brief Reset counts to zero without freeing buffers.
 *
 * @param slot  Slot to clear. NULL is a safe no-op.
 */
void mesh_slot_clear(mesh_slot_t *slot);

/**
 * @brief Get the number of triangular faces (index_count / 3).
 *
 * @param slot  Slot to query. NULL returns 0.
 * @return Face count.
 */
uint32_t mesh_slot_face_count(const mesh_slot_t *slot);

/* ------------------------------------------------------------------------ */
/* Capacity (mesh_slot_reserve.c)                                            */
/* ------------------------------------------------------------------------ */

/**
 * @brief Ensure vertex buffers can hold at least `count` vertices.
 *
 * Grows by doubling until capacity >= count. Fails if count exceeds
 * MESH_SLOT_MAX_VERTICES.
 *
 * @param slot   Slot. Must not be NULL.
 * @param count  Required vertex capacity.
 * @return true on success, false on NULL, OOM, or exceeds max.
 */
bool mesh_slot_reserve_vertices(mesh_slot_t *slot, uint32_t count);

/**
 * @brief Ensure index buffer can hold at least `count` indices.
 *
 * @param slot   Slot. Must not be NULL.
 * @param count  Required index capacity.
 * @return true on success, false on NULL, OOM, or exceeds max.
 */
bool mesh_slot_reserve_indices(mesh_slot_t *slot, uint32_t count);

/* ------------------------------------------------------------------------ */
/* Add geometry (mesh_slot_add.c)                                            */
/* ------------------------------------------------------------------------ */

/**
 * @brief Append a vertex with position and normal.
 *
 * Other attributes (tangent, UV, color) are zero-initialized.
 * Auto-grows if capacity is insufficient.
 *
 * @param slot  Slot. Must not be NULL.
 * @param pos   Position xyz (3 floats). Must not be NULL.
 * @param nrm   Normal xyz (3 floats). Must not be NULL.
 * @return Vertex index, or UINT32_MAX on failure.
 */
uint32_t mesh_slot_add_vertex(mesh_slot_t *slot, const float *pos,
                              const float *nrm);

/**
 * @brief Append a triangle (3 indices + polygroup ID).
 *
 * Auto-grows index buffer if needed.
 *
 * @param slot       Slot. Must not be NULL.
 * @param i0, i1, i2 Vertex indices.
 * @param polygroup  Polygroup ID for this face.
 * @return true on success.
 */
bool mesh_slot_add_triangle(mesh_slot_t *slot, uint32_t i0, uint32_t i1,
                            uint32_t i2, uint16_t polygroup);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_MESH_SLOT_H */
