/**
 * @file collision_mesh_store_io.c
 * @brief Collision mesh store disk I/O: remove, save, load.
 *
 * File naming convention: <dir>/<entity_id>.fvma
 *
 * Non-static functions (4 / 4 limit):
 *   collision_mesh_store_remove
 *   collision_mesh_store_save_entry
 *   collision_mesh_store_load_entry
 *   collision_mesh_store_save_all
 */

#include "ferrum/asset/collision_mesh_asset.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Helpers ---- */

/** Build the file path for an entity's collision mesh. */
static void build_path_(char *buf, size_t buf_size,
                         const char *dir, uint32_t entity_id) {
    snprintf(buf, buf_size, "%s/%u.fvma", dir, entity_id);
}

/* ---- Public API ---- */

void collision_mesh_store_remove(collision_mesh_store_t *store,
                                  uint32_t entity_id) {
    if (!store || !store->entries) return;
    if (entity_id >= store->capacity) return;

    free(store->entries[entity_id].data);
    store->entries[entity_id].data = NULL;
    store->entries[entity_id].size = 0;
}

bool collision_mesh_store_save_entry(const collision_mesh_store_t *store,
                                      uint32_t entity_id,
                                      const char *dir) {
    if (!store || !dir || !store->entries) return false;
    if (entity_id >= store->capacity) return false;

    const collision_mesh_entry_t *entry = &store->entries[entity_id];
    if (!entry->data || entry->size == 0) return false;

    char path[512];
    build_path_(path, sizeof(path), dir, entity_id);

    FILE *f = fopen(path, "wb");
    if (!f) return false;

    size_t written = fwrite(entry->data, 1, entry->size, f);
    fclose(f);

    return written == entry->size;
}

bool collision_mesh_store_load_entry(collision_mesh_store_t *store,
                                      uint32_t entity_id,
                                      const char *dir) {
    if (!store || !dir || !store->entries) return false;
    if (entity_id >= store->capacity) return false;

    char path[512];
    build_path_(path, sizeof(path), dir, entity_id);

    FILE *f = fopen(path, "rb");
    if (!f) return false;

    /* Get file size. */
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size <= 0) {
        fclose(f);
        return false;
    }

    uint8_t *buf = (uint8_t *)malloc((size_t)file_size);
    if (!buf) {
        fclose(f);
        return false;
    }

    size_t read_count = fread(buf, 1, (size_t)file_size, f);
    fclose(f);

    if (read_count != (size_t)file_size) {
        free(buf);
        return false;
    }

    /* Free any existing data. */
    free(store->entries[entity_id].data);
    store->entries[entity_id].data = buf;
    store->entries[entity_id].size = (size_t)file_size;
    return true;
}

uint32_t collision_mesh_store_save_all(const collision_mesh_store_t *store,
                                        const char *dir) {
    if (!store || !dir || !store->entries) return 0;

    uint32_t count = 0;
    for (uint32_t i = 0; i < store->capacity; i++) {
        if (store->entries[i].data) {
            if (collision_mesh_store_save_entry(store, i, dir)) {
                count++;
            }
        }
    }
    return count;
}
