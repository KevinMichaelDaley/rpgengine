/**
 * @file srd_room_map_ops.c
 * @brief Room management operations: add, type get/set, SDF stamp.
 *
 * Non-static functions (4): srd_room_map_add_room, srd_room_map_get_type,
 *                           srd_room_map_set_type, srd_room_map_stamp_from_sdf
 */
#include "ferrum/procgen/srd/srd_room_map.h"

uint8_t srd_room_map_add_room(srd_room_map_t *map, srd_room_type_t type) {
    if (!map || map->n_rooms >= SRD_ROOM_MAP_MAX_ROOMS) return 0;
    int idx = map->n_rooms;
    map->n_rooms++;
    map->types[idx] = type;
    /* Room ID is 1-based: idx 0 → ID 1 */
    return (uint8_t)(idx + 1);
}

srd_room_type_t srd_room_map_get_type(const srd_room_map_t *map, uint8_t room_id) {
    if (!map || room_id == 0 || room_id > (uint8_t)map->n_rooms)
        return SRD_ROOM_GENERIC;
    return map->types[room_id - 1];
}

void srd_room_map_set_type(srd_room_map_t *map, uint8_t room_id, srd_room_type_t type) {
    if (!map || room_id == 0 || room_id > (uint8_t)map->n_rooms) return;
    map->types[room_id - 1] = type;
}

void srd_room_map_stamp_from_sdf(srd_room_map_t *map,
                                 const srd_sdf_grid_t *grid,
                                 uint8_t room_id) {
    if (!map || !map->ids || !grid || !grid->values || room_id == 0) return;

    /* Dimensions must match */
    if (map->nx != grid->nx || map->ny != grid->ny || map->nz != grid->nz) return;

    int total = map->nx * map->ny * map->nz;
    for (int i = 0; i < total; i++) {
        /* Only assign to unowned voxels (id == 0) that are inside (SDF < 0) */
        if (grid->values[i] < 0.0f && map->ids[i] == 0) {
            map->ids[i] = room_id;
        }
    }
}
