/**
 * @file mesh_vao_format.h
 * @brief FVMA binary format — serialize/deserialize mesh_slot_t.
 *
 * Wire format (little-endian):
 *   [4]  magic 'FVMA'
 *   [4]  version (1)
 *   [4]  vertex_count
 *   [4]  index_count
 *   [4]  flags
 *   [4]  polygroup_count (unique, informational)
 *   --- attribute data (conditional on flags) ---
 *   positions, normals, tangents, uv0, uv1, colors, indices, polygroup_ids
 *
 * Ownership: deserialize allocates a new mesh_slot_t; caller destroys.
 * Nullability: NULL args return 0 / false.
 * Thread safety: stateless, thread-safe for distinct buffers.
 */
#ifndef FERRUM_EDITOR_MESH_VAO_FORMAT_H
#define FERRUM_EDITOR_MESH_VAO_FORMAT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ferrum/editor/mesh/mesh_slot.h"

/* ------------------------------------------------------------------------ */
/* Constants                                                                 */
/* ------------------------------------------------------------------------ */

/** @brief Magic bytes: 'F','V','M','A' = 0x414D5646 little-endian. */
#define MESH_VAO_MAGIC   0x414D5646u

/** @brief Current format version. */
#define MESH_VAO_VERSION 1u

/** @brief Header size in bytes (6 × 4 = 24). */
#define MESH_VAO_HEADER_SIZE 24u

/** @brief Flag bits for optional attributes. */
#define MESH_VAO_FLAG_NORMALS   (1u << 0)
#define MESH_VAO_FLAG_TANGENTS  (1u << 1)
#define MESH_VAO_FLAG_UV0       (1u << 2)
#define MESH_VAO_FLAG_UV1       (1u << 3)
#define MESH_VAO_FLAG_COLORS    (1u << 4)

/* ------------------------------------------------------------------------ */
/* API (mesh_vao_serialize.c / mesh_vao_deserialize.c)                       */
/* ------------------------------------------------------------------------ */

/**
 * @brief Compute serialized byte size for a mesh slot with given flags.
 *
 * @param slot   Mesh to measure. NULL returns 0.
 * @param flags  MESH_VAO_FLAG_* bitmask.
 * @return Total bytes needed, or 0 on error.
 */
size_t mesh_vao_serialized_size(const mesh_slot_t *slot, uint32_t flags);

/**
 * @brief Serialize a mesh slot to a byte buffer.
 *
 * @param slot     Mesh to serialize. Must not be NULL.
 * @param flags    Attribute flags.
 * @param buf      Output buffer. Must be >= serialized_size bytes.
 * @param buf_size Buffer capacity.
 * @return Bytes written, or 0 on error.
 */
size_t mesh_vao_serialize(const mesh_slot_t *slot, uint32_t flags,
                          uint8_t *buf, size_t buf_size);

/**
 * @brief Deserialize a byte buffer into a mesh slot.
 *
 * Allocates a new mesh_slot_t's internal buffers. Caller must call
 * mesh_slot_destroy() on the output.
 *
 * @param buf       Input buffer. Must not be NULL.
 * @param buf_size  Buffer size in bytes.
 * @param out       Output mesh slot. Must not be NULL.
 * @return true on success, false on bad magic/version/truncated/etc.
 */
bool mesh_vao_deserialize(const uint8_t *buf, size_t buf_size,
                          mesh_slot_t *out);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_MESH_VAO_FORMAT_H */
