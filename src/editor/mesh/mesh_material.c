/**
 * @file mesh_material.c
 * @brief Material map lifecycle and query.
 *
 * Non-static functions (4 of 4): init, destroy, get, count.
 */
#include "ferrum/editor/mesh/mesh_material.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* mesh_material_map_init                                              */
/* ------------------------------------------------------------------ */

void mesh_material_map_init(mesh_material_map_t *map) {
    if (!map) return;
    memset(map, 0, sizeof(*map));
}

/* ------------------------------------------------------------------ */
/* mesh_material_map_destroy                                           */
/* ------------------------------------------------------------------ */

void mesh_material_map_destroy(mesh_material_map_t *map) {
    if (!map) return;
    memset(map, 0, sizeof(*map));
}

/* ------------------------------------------------------------------ */
/* mesh_material_map_get                                               */
/* ------------------------------------------------------------------ */

const char *mesh_material_map_get(const mesh_material_map_t *map, uint16_t pg) {
    if (!map) return NULL;
    for (uint32_t i = 0; i < map->count; i++) {
        if (map->polygroup_ids[i] == pg) {
            return map->paths[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* mesh_material_map_count                                             */
/* ------------------------------------------------------------------ */

uint32_t mesh_material_map_count(const mesh_material_map_t *map) {
    if (!map) return 0;
    return map->count;
}
