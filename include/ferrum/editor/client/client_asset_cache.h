/**
 * @file client_asset_cache.h
 * @brief Client-side asset cache — avoids re-downloading unchanged assets.
 *
 * Stores downloaded assets in a local directory (default: /tmp/ferrum_cache/)
 * with a JSON manifest tracking path → hash mappings. Uses LRU eviction
 * when the cache exceeds its size limit.
 *
 * Thread safety: NOT thread-safe. Caller must serialize access if used
 * from multiple threads.
 *
 * Public types: asset_cache_entry_t, asset_cache_t (2).
 */
#ifndef FERRUM_EDITOR_CLIENT_ASSET_CACHE_H
#define FERRUM_EDITOR_CLIENT_ASSET_CACHE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------------------ */
/* Constants                                                                 */
/* ------------------------------------------------------------------------ */

/** @brief Default cache size limit in bytes (512 MB). */
#define ASSET_CACHE_DEFAULT_LIMIT  (512ULL * 1024 * 1024)

/** @brief Maximum number of cached entries. */
#define ASSET_CACHE_MAX_ENTRIES    4096

/** @brief Maximum path length for cached assets. */
#define ASSET_CACHE_PATH_MAX       256

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief A single cached asset entry.
 */
typedef struct asset_cache_entry {
    char     path[ASSET_CACHE_PATH_MAX]; /**< Asset path (relative). */
    uint32_t hash;                       /**< CRC32 hash of content. */
    uint32_t size;                       /**< File size in bytes. */
    uint64_t last_access;                /**< Monotonic access counter for LRU. */
} asset_cache_entry_t;

/**
 * @brief Client-side asset cache.
 *
 * Maintains an in-memory index of cached assets and their locations
 * on disk. Entries are evicted LRU when total size exceeds the limit.
 */
typedef struct asset_cache {
    char                cache_dir[512];  /**< Root directory for cache files. */
    asset_cache_entry_t entries[ASSET_CACHE_MAX_ENTRIES]; /**< Entry array. */
    uint32_t            count;           /**< Number of cached entries. */
    uint64_t            total_size;      /**< Total cached bytes. */
    uint64_t            size_limit;      /**< Max cache size in bytes. */
    uint64_t            access_counter;  /**< Monotonic counter for LRU. */
} asset_cache_t;

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------ */

/**
 * @brief Initialize the asset cache.
 *
 * Creates the cache directory if it doesn't exist. Loads the manifest
 * file if present.
 *
 * @param cache      Cache to initialize (non-NULL).
 * @param cache_dir  Directory path for cache storage (non-NULL).
 * @param size_limit Maximum cache size in bytes (0 = default 512MB).
 * @return true on success.
 *
 * @note Side effects: creates directories, reads manifest file.
 */
bool asset_cache_init(asset_cache_t *cache, const char *cache_dir,
                       uint64_t size_limit);

/**
 * @brief Destroy the cache. Saves the manifest to disk.
 *
 * @param cache  Cache to destroy. NULL-safe.
 * @note Side effects: writes manifest file.
 */
void asset_cache_destroy(asset_cache_t *cache);

/* ------------------------------------------------------------------------ */
/* Operations                                                                */
/* ------------------------------------------------------------------------ */

/**
 * @brief Check if an asset is cached with the given hash.
 *
 * @param cache  Cache.
 * @param path   Asset path.
 * @param hash   Expected CRC32 hash.
 * @return true if the asset is cached with a matching hash.
 */
bool asset_cache_has(asset_cache_t *cache, const char *path, uint32_t hash);

/**
 * @brief Load a cached asset's data from disk.
 *
 * On success, *data_out is heap-allocated and must be freed by caller.
 *
 * @param cache     Cache.
 * @param path      Asset path.
 * @param data_out  Output data buffer (caller-owned).
 * @param size_out  Output data size.
 * @return true if found and loaded successfully.
 *
 * @note Ownership: *data_out is caller-owned.
 */
bool asset_cache_get(asset_cache_t *cache, const char *path,
                      void **data_out, uint32_t *size_out);

/**
 * @brief Store an asset in the cache.
 *
 * Writes the data to disk and adds/updates the manifest entry.
 * May trigger LRU eviction if the cache exceeds its size limit.
 *
 * @param cache  Cache.
 * @param path   Asset path.
 * @param hash   CRC32 hash of the data.
 * @param data   File data.
 * @param size   Data size in bytes.
 * @return true on success.
 *
 * @note Side effects: writes file to disk, may delete old files.
 */
bool asset_cache_put(asset_cache_t *cache, const char *path,
                      uint32_t hash, const void *data, uint32_t size);

/**
 * @brief Invalidate a cached asset (e.g., server reports it changed).
 *
 * Removes the entry from the index and deletes the file from disk.
 *
 * @param cache  Cache.
 * @param path   Asset path to invalidate.
 * @return true if the entry was found and removed.
 *
 * @note Side effects: deletes file from disk.
 */
bool asset_cache_invalidate(asset_cache_t *cache, const char *path);

/**
 * @brief Save the manifest to disk.
 *
 * The manifest is a simple text file with one entry per line:
 *   path hash size
 *
 * @param cache  Cache.
 * @return true on success.
 */
bool asset_cache_save_manifest(const asset_cache_t *cache);

/**
 * @brief Get the number of cached entries.
 */
uint32_t asset_cache_count(const asset_cache_t *cache);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_CLIENT_ASSET_CACHE_H */
