/**
 * @file client_asset_cache.c
 * @brief Asset cache — lifecycle and index operations.
 *
 * Non-static functions: init, destroy, has, count (4).
 */

#define _DEFAULT_SOURCE
#include "ferrum/editor/client/client_asset_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

/* ----------------------------------------------------------------------- */
/* Internal helpers                                                          */
/* ----------------------------------------------------------------------- */

/** @brief Create directory and parents. */
static bool mkdirs_(const char *path) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return mkdir(tmp, 0755) == 0 || errno == EEXIST;
}

/** @brief Find an entry by path. Returns index or -1. */
static int find_entry_(const asset_cache_t *cache, const char *path) {
    for (uint32_t i = 0; i < cache->count; i++) {
        if (strcmp(cache->entries[i].path, path) == 0) return (int)i;
    }
    return -1;
}

/** @brief Load manifest from disk. */
static void load_manifest_(asset_cache_t *cache) {
    char mpath[600];
    snprintf(mpath, sizeof(mpath), "%s/manifest.txt", cache->cache_dir);
    FILE *f = fopen(mpath, "r");
    if (!f) return;

    char line[512];
    while (fgets(line, sizeof(line), f) && cache->count < ASSET_CACHE_MAX_ENTRIES) {
        asset_cache_entry_t *e = &cache->entries[cache->count];
        uint32_t hash = 0, size = 0;
        if (sscanf(line, "%255s %u %u", e->path, &hash, &size) == 3) {
            e->hash = hash;
            e->size = size;
            e->last_access = cache->access_counter++;
            cache->total_size += size;
            cache->count++;
        }
    }
    fclose(f);
}

/* ----------------------------------------------------------------------- */
/* Lifecycle                                                                 */
/* ----------------------------------------------------------------------- */

bool asset_cache_init(asset_cache_t *cache, const char *cache_dir,
                       uint64_t size_limit) {
    if (!cache || !cache_dir) return false;

    memset(cache, 0, sizeof(*cache));
    snprintf(cache->cache_dir, sizeof(cache->cache_dir), "%s", cache_dir);
    cache->size_limit = size_limit > 0 ? size_limit : ASSET_CACHE_DEFAULT_LIMIT;

    mkdirs_(cache->cache_dir);
    load_manifest_(cache);
    return true;
}

void asset_cache_destroy(asset_cache_t *cache) {
    if (!cache) return;
    asset_cache_save_manifest(cache);
}

/* ----------------------------------------------------------------------- */
/* Queries                                                                   */
/* ----------------------------------------------------------------------- */

bool asset_cache_has(asset_cache_t *cache, const char *path, uint32_t hash) {
    if (!cache || !path) return false;
    int idx = find_entry_(cache, path);
    if (idx < 0) return false;
    /* Bump LRU access time on every query. */
    cache->entries[idx].last_access = cache->access_counter++;
    return cache->entries[idx].hash == hash;
}

uint32_t asset_cache_count(const asset_cache_t *cache) {
    return cache ? cache->count : 0;
}
