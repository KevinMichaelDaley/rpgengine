/**
 * @file edit_asset_registry.h
 * @brief Server-side asset registry — catalogs available assets.
 *
 * The registry scans configured asset directories on startup and
 * maintains a flat catalog of all discovered assets. Supports listing
 * by directory prefix, regex search, path completion, and type filtering.
 *
 * Thread safety: the registry is built on startup and is read-only
 * during editing. Runtime registration (edit_asset_registry_add) is
 * only called from the main thread.
 *
 * Public types: edit_asset_entry_t, edit_asset_registry_t (2).
 */
#ifndef FERRUM_EDITOR_EDIT_ASSET_REGISTRY_H
#define FERRUM_EDITOR_EDIT_ASSET_REGISTRY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------------------ */
/* Asset type enum                                                           */
/* ------------------------------------------------------------------------ */

/** Maximum path length for an asset (relative to project root). */
#define EDIT_ASSET_PATH_MAX 256

/** Asset type classification. */
typedef enum edit_asset_type {
    EDIT_ASSET_ANY      = 0, /**< Wildcard for queries (not stored). */
    EDIT_ASSET_MESH     = 1, /**< .glb, .obj mesh files. */
    EDIT_ASSET_TEXTURE  = 2, /**< .png, .ktx2, .jpg texture files. */
    EDIT_ASSET_MATERIAL = 3, /**< .mat material definition files. */
    EDIT_ASSET_PREFAB   = 4, /**< .prefab entity template files. */
    EDIT_ASSET_SCRIPT   = 5, /**< .lua, .wren, .ed script files. */
    EDIT_ASSET_UNKNOWN  = 6, /**< Unrecognized extension. */
} edit_asset_type_t;

/* ------------------------------------------------------------------------ */
/* Asset entry                                                               */
/* ------------------------------------------------------------------------ */

/**
 * @brief A single asset catalog entry.
 *
 * Paths are relative to the project asset root (e.g., "meshes/pillar.glb").
 * Hash is CRC32 of file contents for change detection.
 */
typedef struct edit_asset_entry {
    char              path[EDIT_ASSET_PATH_MAX]; /**< Relative path. */
    edit_asset_type_t type;                      /**< Asset type. */
    uint32_t          size;                      /**< File size in bytes. */
    uint32_t          hash;                      /**< CRC32 content hash. */
} edit_asset_entry_t;

/* ------------------------------------------------------------------------ */
/* Asset registry                                                            */
/* ------------------------------------------------------------------------ */

/**
 * @brief Server-side asset catalog.
 *
 * Owns a dynamically allocated array of entries. Capacity is fixed
 * at init time; count grows as assets are registered.
 */
typedef struct edit_asset_registry {
    edit_asset_entry_t *entries;   /**< Array of catalog entries. */
    uint32_t            count;    /**< Number of registered assets. */
    uint32_t            capacity; /**< Max entries (set at init). */
} edit_asset_registry_t;

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------ */

/**
 * @brief Initialize an empty asset registry.
 *
 * Allocates space for up to `capacity` entries.
 *
 * @param reg       Registry to initialize.
 * @param capacity  Maximum number of asset entries.
 */
void edit_asset_registry_init(edit_asset_registry_t *reg, uint32_t capacity);

/**
 * @brief Destroy the registry and free all memory.
 * @param reg  Registry to destroy. Safe to call on zeroed struct.
 */
void edit_asset_registry_destroy(edit_asset_registry_t *reg);

/* ------------------------------------------------------------------------ */
/* Registration                                                              */
/* ------------------------------------------------------------------------ */

/**
 * @brief Register a single asset manually.
 *
 * @param reg   Registry.
 * @param path  Relative asset path (copied, max EDIT_ASSET_PATH_MAX).
 * @param type  Asset type.
 * @param size  File size in bytes.
 * @param hash  CRC32 content hash.
 * @return true on success, false if full or duplicate path.
 */
bool edit_asset_registry_add(edit_asset_registry_t *reg,
                              const char *path,
                              edit_asset_type_t type,
                              uint32_t size, uint32_t hash);

/**
 * @brief Scan a directory tree and register all recognized assets.
 *
 * Recursively walks the directory, detecting asset type from file
 * extension. Paths are stored relative to `root_dir`.
 *
 * @param reg       Registry to populate.
 * @param root_dir  Absolute path to the asset root directory.
 * @return Number of assets registered.
 */
uint32_t edit_asset_registry_scan(edit_asset_registry_t *reg,
                                   const char *root_dir);

/* ------------------------------------------------------------------------ */
/* Queries                                                                   */
/* ------------------------------------------------------------------------ */

/**
 * @brief Get total number of registered assets.
 */
uint32_t edit_asset_registry_count(const edit_asset_registry_t *reg);

/**
 * @brief Find an asset by exact path.
 *
 * @param reg   Registry.
 * @param path  Exact relative path to look up.
 * @return Pointer to entry, or NULL if not found.
 */
const edit_asset_entry_t *edit_asset_registry_find(
    const edit_asset_registry_t *reg, const char *path);

/**
 * @brief List assets matching a directory prefix and optional type filter.
 *
 * @param reg       Registry.
 * @param prefix    Path prefix to match (e.g., "meshes/"). Empty = all.
 * @param type      Type filter (EDIT_ASSET_ANY = no filter).
 * @param out       Output array of entry pointers.
 * @param max       Maximum entries to return.
 * @return Number of matching entries written to out.
 */
uint32_t edit_asset_registry_list(const edit_asset_registry_t *reg,
                                   const char *prefix,
                                   edit_asset_type_t type,
                                   const edit_asset_entry_t **out,
                                   uint32_t max);

/**
 * @brief Search assets by regex pattern on path, with optional type filter.
 *
 * @param reg       Registry.
 * @param pattern   POSIX extended regex pattern.
 * @param type      Type filter (EDIT_ASSET_ANY = no filter).
 * @param out       Output array of entry pointers.
 * @param max       Maximum entries to return.
 * @return Number of matching entries written to out.
 */
uint32_t edit_asset_registry_search(const edit_asset_registry_t *reg,
                                     const char *pattern,
                                     edit_asset_type_t type,
                                     const edit_asset_entry_t **out,
                                     uint32_t max);

/**
 * @brief Path completion — return assets whose path starts with prefix.
 *
 * Unlike list(), this matches the full path (not just directory prefix).
 *
 * @param reg       Registry.
 * @param prefix    Path prefix to complete (e.g., "meshes/p").
 * @param out       Output array of entry pointers.
 * @param max       Maximum entries to return.
 * @return Number of matching entries written to out.
 */
uint32_t edit_asset_registry_complete(const edit_asset_registry_t *reg,
                                       const char *prefix,
                                       const edit_asset_entry_t **out,
                                       uint32_t max);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_EDIT_ASSET_REGISTRY_H */
