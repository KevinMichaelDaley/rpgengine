/**
 * @file collision_mesh_store_scan.c
 * @brief Scan a directory and load all collision mesh FVMA files.
 *
 * Non-static functions (1 / 4 limit):
 *   collision_mesh_store_load_all
 */

#include "ferrum/asset/collision_mesh_asset.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>

uint32_t collision_mesh_store_load_all(collision_mesh_store_t *store,
                                        const char *dir) {
    if (!store || !dir || !store->entries) return 0;

    DIR *d = opendir(dir);
    if (!d) return 0;

    uint32_t count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        /* Match files named "<N>.fvma". */
        const char *name = ent->d_name;
        char *endp = NULL;
        unsigned long entity_id = strtoul(name, &endp, 10);

        /* Must have parsed at least one digit and end with ".fvma". */
        if (endp == name) continue;
        if (strcmp(endp, ".fvma") != 0) continue;
        if (entity_id >= store->capacity) continue;

        if (collision_mesh_store_load_entry(store, (uint32_t)entity_id, dir)) {
            count++;
        }
    }

    closedir(d);
    return count;
}
