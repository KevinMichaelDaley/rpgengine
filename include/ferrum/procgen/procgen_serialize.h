/**
 * @file procgen_serialize.h
 * @brief Serialize fr_dungeon_layout_t to JSON and engine-compatible formats.
 */

#ifndef FERRUM_PROCGEN_SERIALIZE_H
#define FERRUM_PROCGEN_SERIALIZE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>
#include "ferrum/procgen/procgen_layout.h"

/**
 * @brief Serialize a dungeon layout to a JSON file.
 *
 * Output format is compatible with the engine's edit_level_deserialize:
 *   { "entities": [ { "id": N, "type": "...", "pos": [x,y,z],
 *                     "rot": [0,0,0], "scale": [sx,sy,sz] }, ... ] }
 *
 * @param layout   Layout to serialize.
 * @param path     Output file path.
 * @param err_buf  Error buffer.
 * @param err_cap  Error buffer capacity.
 * @return 0 on success, -1 on error.
 */
int procgen_serialize_to_json(const fr_dungeon_layout_t *layout,
                              const char *path,
                              char *err_buf, uint32_t err_cap);

/**
 * @brief Serialize a dungeon layout to a JSON string in memory.
 *
 * @param layout    Layout to serialize.
 * @param buf       Output buffer.
 * @param buf_cap   Buffer capacity.
 * @param out_len   Set to bytes written (excluding null terminator).
 * @return 0 on success, -1 on buffer overflow.
 */
int procgen_serialize_to_json_buf(const fr_dungeon_layout_t *layout,
                                  char *buf, uint32_t buf_cap,
                                  uint32_t *out_len);

/**
 * @brief Write a dungeon layout as a JSON file suitable for the engine
 *        level loader, including spawn and marker entities.
 *
 * Calls procgen_serialize_to_json internally with the same format.
 */
int procgen_serialize_level(const fr_dungeon_layout_t *layout,
                            const char *path,
                            char *err_buf, uint32_t err_cap);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PROCGEN_SERIALIZE_H */
