/**
 * @file mesh_transfer.h
 * @brief Mesh data transfer: copy, paste, OBJ import/export.
 *
 * Types: none.
 * Functions: mesh_copy, mesh_paste, mesh_import_obj, mesh_export_obj.
 *
 * Ownership: caller owns all mesh_slot_t pointers.
 * Nullability: NULL returns false.
 * Thread safety: not thread-safe.
 */
#ifndef FERRUM_EDITOR_MESH_TRANSFER_H
#define FERRUM_EDITOR_MESH_TRANSFER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ferrum/editor/mesh/mesh_slot.h"

#include <stdbool.h>

/**
 * @brief Deep copy mesh from src to dst.
 *
 * Clears dst first, then copies all vertex/index data.
 *
 * @param src  Source mesh. Not NULL.
 * @param dst  Destination mesh. Not NULL.
 * @return true on success.
 */
bool mesh_copy(const mesh_slot_t *src, mesh_slot_t *dst);

/**
 * @brief Append mesh data from src into dst.
 *
 * Vertex indices are offset by dst's current vertex count.
 * Does NOT clear dst.
 *
 * @param src  Source mesh. Not NULL.
 * @param dst  Destination mesh. Not NULL.
 * @return true on success.
 */
bool mesh_paste(const mesh_slot_t *src, mesh_slot_t *dst);

/**
 * @brief Export mesh to Wavefront OBJ file.
 *
 * Writes positions, normals, UVs (channel 0), and faces.
 *
 * @param slot  Mesh to export. Not NULL.
 * @param path  Output file path. Not NULL.
 * @return true on success.
 */
bool mesh_export_obj(const mesh_slot_t *slot, const char *path);

/**
 * @brief Import mesh from Wavefront OBJ file.
 *
 * Parses v, vn, vt, and f lines. Clears slot first.
 *
 * @param slot  Destination mesh (cleared first). Not NULL.
 * @param path  Input file path. Not NULL.
 * @return true on success.
 */
bool mesh_import_obj(mesh_slot_t *slot, const char *path);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_MESH_TRANSFER_H */
