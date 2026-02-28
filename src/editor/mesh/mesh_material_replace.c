/**
 * @file mesh_material_replace.c
 * @brief Bulk material replacement in polygroup→material map.
 */
#include "ferrum/editor/mesh/mesh_material_ops.h"

#include <string.h>

bool mesh_material_replace(mesh_material_map_t *map,
                           const char *old_path,
                           const char *new_path) {
    if (!map || !old_path || !new_path) return false;

    bool found = false;
    for (uint32_t i = 0; i < map->count; i++) {
        if (strcmp(map->paths[i], old_path) == 0) {
            /* Overwrite with new path */
            size_t len = strlen(new_path);
            if (len >= MESH_MATERIAL_PATH_MAX) {
                len = MESH_MATERIAL_PATH_MAX - 1;
            }
            memcpy(map->paths[i], new_path, len);
            map->paths[i][len] = '\0';
            found = true;
        }
    }
    return found;
}
