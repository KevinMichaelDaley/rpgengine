/**
 * @file edit_serialize.h
 * @brief Level serialization — save/load entity data as JSON.
 *
 * Serializes entity store contents to JSON format and deserializes
 * JSON back into an entity store. Supports both buffer and file I/O.
 *
 * JSON format:
 *   {"version":1,"entities":[{"id":N,"type":"box","pos":[x,y,z],
 *     "rot":[rx,ry,rz],"scale":[sx,sy,sz]}, ...]}
 *
 * Thread safety: not thread-safe; call from tick thread only.
 */
#ifndef FERRUM_EDITOR_EDIT_SERIALIZE_H
#define FERRUM_EDITOR_EDIT_SERIALIZE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

/* Forward declaration. */
struct edit_entity_store;

/* ------------------------------------------------------------------------ */
/* Buffer serialization                                                      */
/* ------------------------------------------------------------------------ */

/**
 * @brief Serialize all active entities to a JSON buffer.
 *
 * @param store  Entity store to serialize.
 * @param buf    Output buffer (NULL for length query).
 * @param cap    Capacity of output buffer.
 * @return Bytes written (or required if cap insufficient).
 */
size_t edit_level_serialize(const struct edit_entity_store *store,
                            char *buf, size_t cap);

/**
 * @brief Deserialize entities from a JSON buffer.
 *
 * Clears the store before loading. Entities are restored at
 * their original IDs where possible.
 *
 * @param store     Entity store to populate.
 * @param json      JSON string.
 * @param json_len  Length of JSON string.
 * @return true on success, false on parse error or format mismatch.
 */
bool edit_level_deserialize(struct edit_entity_store *store,
                            const char *json, size_t json_len);

/* ------------------------------------------------------------------------ */
/* File I/O                                                                  */
/* ------------------------------------------------------------------------ */

/**
 * @brief Save entity store to a JSON file.
 *
 * Validates path (rejects directory traversal). Overwrites existing file.
 *
 * @param store  Entity store to save.
 * @param path   File path.
 * @return true on success.
 */
bool edit_level_save(const struct edit_entity_store *store, const char *path);

/**
 * @brief Load entities from a JSON file into the store.
 *
 * Clears the store before loading. Validates path.
 *
 * @param store  Entity store to populate.
 * @param path   File path.
 * @return true on success, false on I/O or parse error.
 */
bool edit_level_load(struct edit_entity_store *store, const char *path);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_EDIT_SERIALIZE_H */
