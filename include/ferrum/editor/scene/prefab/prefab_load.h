/**
 * @file prefab_load.h
 * @brief Deserialize prefab definitions from JSON (.fpfab format).
 *
 * Parses .fpfab JSON text into a prefab_def_t.
 *
 * Ownership: output def is value-typed (no heap allocations).
 * Nullability: all pointer params must be non-NULL.
 * Error semantics: returns false on parse error, wrong version, or NULL args.
 *
 * Public types: none (0-type rule).
 */
#ifndef FERRUM_EDITOR_SCENE_PREFAB_LOAD_H
#define FERRUM_EDITOR_SCENE_PREFAB_LOAD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

/* Forward declaration. */
struct prefab_def;

/**
 * @brief Deserialize a prefab definition from JSON text.
 *
 * @param json  JSON text (non-NULL).
 * @param len   Length of JSON text.
 * @param def   Output definition (non-NULL).
 * @return true on success, false on parse error or wrong version.
 */
bool prefab_deserialize(const char *json, size_t len,
                        struct prefab_def *def);

/**
 * @brief Load a prefab definition from a .fpfab file.
 *
 * @param path  File path to read (non-NULL).
 * @param def   Output definition (non-NULL).
 * @return true on success, false on I/O or parse error.
 */
bool prefab_load(const char *path, struct prefab_def *def);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_PREFAB_LOAD_H */
