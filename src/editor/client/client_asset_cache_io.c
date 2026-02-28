/**
 * @file client_asset_cache_io.c
 * @brief Asset cache — file I/O operations (get/put/invalidate/manifest).
 *
 * Non-static functions: get, put, invalidate, save_manifest (4).
 */

#define _DEFAULT_SOURCE
#include "ferrum/editor/client/client_asset_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

/* ----------------------------------------------------------------------- */
/* Internal helpers                                                          */
/* ----------------------------------------------------------------------- */

/** @brief Build the on-disk path for a cached asset. */
static void build_disk_path_(const asset_cache_t *cache, const char *path,
                              char *out, size_t out_cap) {
    snprintf(out, out_cap, "%s/%s", cache->cache_dir, path);
}

/** @brief Create parent directories for a file path. */
static void ensure_parents_(const char *path) {
    char tmp[600];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
}

/** @brief Find an entry by path. Returns index or -1. */
static int find_entry_(const asset_cache_t *cache, const char *path) {
    for (uint32_t i = 0; i < cache->count; i++) {
        if (strcmp(cache->entries[i].path, path) == 0) return (int)i;
    }
    return -1;
}

/** @brief Evict the least-recently-used entry. */
static void evict_lru_(asset_cache_t *cache) {
    if (cache->count == 0) return;

    /* Find entry with lowest last_access. */
    uint32_t victim = 0;
    uint64_t min_access = cache->entries[0].last_access;
    for (uint32_t i = 1; i < cache->count; i++) {
        if (cache->entries[i].last_access < min_access) {
            min_access = cache->entries[i].last_access;
            victim = i;
        }
    }

    /* Delete file from disk. */
    char disk_path[600];
    build_disk_path_(cache, cache->entries[victim].path,
                      disk_path, sizeof(disk_path));
    unlink(disk_path);

    cache->total_size -= cache->entries[victim].size;

    /* Swap-remove from array. */
    cache->count--;
    if (victim < cache->count) {
        cache->entries[victim] = cache->entries[cache->count];
    }
}

/* ----------------------------------------------------------------------- */
/* Operations                                                                */
/* ----------------------------------------------------------------------- */

bool asset_cache_get(asset_cache_t *cache, const char *path,
                      void **data_out, uint32_t *size_out) {
    if (!cache || !path || !data_out || !size_out) return false;
    *data_out = NULL;
    *size_out = 0;

    int idx = find_entry_(cache, path);
    if (idx < 0) return false;

    char disk_path[600];
    build_disk_path_(cache, path, disk_path, sizeof(disk_path));

    FILE *f = fopen(disk_path, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return false; }

    void *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return false; }

    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return false;
    }
    fclose(f);

    /* Update LRU access time. */
    cache->entries[idx].last_access = cache->access_counter++;

    *data_out = buf;
    *size_out = (uint32_t)sz;
    return true;
}

bool asset_cache_put(asset_cache_t *cache, const char *path,
                      uint32_t hash, const void *data, uint32_t size) {
    if (!cache || !path || !data || size == 0) return false;

    /* Evict until we have room. */
    while (cache->total_size + size > cache->size_limit && cache->count > 0) {
        evict_lru_(cache);
    }

    /* Check if we're updating an existing entry. */
    int idx = find_entry_(cache, path);
    if (idx >= 0) {
        cache->total_size -= cache->entries[idx].size;
    } else if (cache->count >= ASSET_CACHE_MAX_ENTRIES) {
        evict_lru_(cache);
    }

    /* Write file to disk. */
    char disk_path[600];
    build_disk_path_(cache, path, disk_path, sizeof(disk_path));
    ensure_parents_(disk_path);

    FILE *f = fopen(disk_path, "wb");
    if (!f) return false;
    if (fwrite(data, 1, size, f) != size) {
        fclose(f);
        return false;
    }
    fclose(f);

    /* Update or add index entry. */
    asset_cache_entry_t *e;
    if (idx >= 0) {
        e = &cache->entries[idx];
    } else {
        e = &cache->entries[cache->count++];
    }
    snprintf(e->path, sizeof(e->path), "%s", path);
    e->hash = hash;
    e->size = size;
    e->last_access = cache->access_counter++;
    cache->total_size += size;

    return true;
}

bool asset_cache_invalidate(asset_cache_t *cache, const char *path) {
    if (!cache || !path) return false;

    int idx = find_entry_(cache, path);
    if (idx < 0) return false;

    /* Delete file. */
    char disk_path[600];
    build_disk_path_(cache, path, disk_path, sizeof(disk_path));
    unlink(disk_path);

    cache->total_size -= cache->entries[idx].size;

    /* Swap-remove. */
    cache->count--;
    if ((uint32_t)idx < cache->count) {
        cache->entries[idx] = cache->entries[cache->count];
    }
    return true;
}

bool asset_cache_save_manifest(const asset_cache_t *cache) {
    if (!cache) return false;

    char mpath[600];
    snprintf(mpath, sizeof(mpath), "%s/manifest.txt", cache->cache_dir);

    FILE *f = fopen(mpath, "w");
    if (!f) return false;

    for (uint32_t i = 0; i < cache->count; i++) {
        const asset_cache_entry_t *e = &cache->entries[i];
        fprintf(f, "%s %u %u\n", e->path, e->hash, e->size);
    }
    fclose(f);
    return true;
}
