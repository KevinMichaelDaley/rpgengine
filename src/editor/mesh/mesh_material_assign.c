/**
 * @file mesh_material_assign.c
 * @brief Assign materials to selected faces via polygroup mapping.
 *
 * Non-static functions (2 of 4): mesh_material_assign, mesh_material_get_face.
 */
#include "ferrum/editor/mesh/mesh_material.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Static: find or create polygroup for a material path                */
/* ------------------------------------------------------------------ */

/**
 * Find existing polygroup ID for the given material path, or allocate
 * a new one. Returns UINT16_MAX if map is full.
 */
static uint16_t find_or_create_pg_(mesh_material_map_t *map,
                                   const char *path) {
    /* Check for existing entry with same path */
    for (uint32_t i = 0; i < map->count; i++) {
        if (strcmp(map->paths[i], path) == 0) {
            return map->polygroup_ids[i];
        }
    }

    /* Allocate new entry */
    if (map->count >= MESH_MATERIAL_MAP_MAX) return UINT16_MAX;

    /* Find next unused polygroup ID (start from 1; 0 = default/none) */
    uint16_t next_pg = 1;
    for (uint32_t i = 0; i < map->count; i++) {
        if (map->polygroup_ids[i] >= next_pg) {
            next_pg = map->polygroup_ids[i] + 1;
        }
    }

    uint32_t idx = map->count;
    map->polygroup_ids[idx] = next_pg;
    strncpy(map->paths[idx], path, MESH_MATERIAL_PATH_MAX - 1);
    map->paths[idx][MESH_MATERIAL_PATH_MAX - 1] = '\0';
    map->count++;

    return next_pg;
}

/* ------------------------------------------------------------------ */
/* mesh_material_assign                                                */
/* ------------------------------------------------------------------ */

bool mesh_material_assign(mesh_slot_t *slot, mesh_material_map_t *map,
                          const mesh_sel_bitset_t *sel,
                          const char *material_path) {
    if (!slot || !map || !sel || !material_path) return false;

    uint16_t pg = find_or_create_pg_(map, material_path);
    if (pg == UINT16_MAX) return false;

    uint32_t fc = slot->index_count / 3;
    for (uint32_t f = 0; f < fc; f++) {
        if (mesh_sel_bitset_test(sel, f)) {
            slot->polygroup_ids[f] = pg;
        }
    }

    return true;
}

/* ------------------------------------------------------------------ */
/* mesh_material_get_face                                              */
/* ------------------------------------------------------------------ */

const char *mesh_material_get_face(const mesh_slot_t *slot,
                                   const mesh_material_map_t *map,
                                   uint32_t face) {
    if (!slot || !map) return NULL;
    uint32_t fc = slot->index_count / 3;
    if (face >= fc) return NULL;

    uint16_t pg = slot->polygroup_ids[face];
    return mesh_material_map_get(map, pg);
}
