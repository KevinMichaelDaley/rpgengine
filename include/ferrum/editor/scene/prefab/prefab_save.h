/**
 * @file prefab_save.h
 * @brief Serialize prefab definitions to JSON (.fpfab format).
 *
 * Converts a prefab_def_t to compact JSON text suitable for
 * writing to .fpfab files.
 *
 * Ownership: no ownership taken; writes to caller-provided buffer.
 * Nullability: all pointer params must be non-NULL unless documented.
 * Error semantics: serialize returns 0 on NULL args; save returns false
 *                  on I/O error.
 * Side effects: save performs file I/O.
 *
 * Public types: none (0-type rule).
 */
#ifndef FERRUM_EDITOR_SCENE_PREFAB_SAVE_H
#define FERRUM_EDITOR_SCENE_PREFAB_SAVE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

/* Forward declaration. */
struct prefab_def;

/**
 * @brief Serialize a prefab definition to JSON text.
 *
 * @param def  Prefab definition (non-NULL).
 * @param buf  Output buffer (may be NULL for length query).
 * @param cap  Buffer capacity.
 * @return Bytes written (or needed if cap is insufficient).
 *         Returns 0 on NULL def.
 */
size_t prefab_serialize(const struct prefab_def *def, char *buf, size_t cap);

/**
 * @brief Save a prefab definition to a .fpfab file.
 *
 * @param path  File path to write (non-NULL).
 * @param def   Prefab definition (non-NULL).
 * @return true on success, false on I/O or serialization error.
 */
bool prefab_save(const char *path, const struct prefab_def *def);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_PREFAB_SAVE_H */
