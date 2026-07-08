/**
 * @file srd_room_map.c
 * @brief Core lifecycle and voxel access for srd_room_map_t.
 *
 * Non-static functions (4): srd_room_map_init, srd_room_map_destroy,
 *                           srd_room_map_get, srd_room_map_set
 */
#include "ferrum/procgen/srd/srd_room_map.h"

#include <stdlib.h>
#include <string.h>

/* ── Helpers ───────────────────────────────────────────────────── */

static inline int in_bounds(const srd_room_map_t *m, int x, int y, int z) {
    return x >= 0 && x < m->nx &&
           y >= 0 && y < m->ny &&
           z >= 0 && z < m->nz;
}

static inline int flat_index(const srd_room_map_t *m, int x, int y, int z) {
    return z * m->ny * m->nx + y * m->nx + x;
}

/* ── Public API ────────────────────────────────────────────────── */

int srd_room_map_init(srd_room_map_t *map, int nx, int ny, int nz) {
    if (!map || nx <= 0 || ny <= 0 || nz <= 0) return -1;

    int total = nx * ny * nz;
    uint8_t *ids = (uint8_t *)calloc((size_t)total, sizeof(uint8_t));
    if (!ids) return -1;

    memset(map, 0, sizeof(*map));
    map->ids = ids;
    map->nx = nx;
    map->ny = ny;
    map->nz = nz;
    map->n_rooms = 0;

    return 0;
}

void srd_room_map_destroy(srd_room_map_t *map) {
    if (!map) return;
    free(map->ids);
    memset(map, 0, sizeof(*map));
}

uint8_t srd_room_map_get(const srd_room_map_t *map, int x, int y, int z) {
    if (!map || !map->ids || !in_bounds(map, x, y, z)) return 0;
    return map->ids[flat_index(map, x, y, z)];
}

void srd_room_map_set(srd_room_map_t *map, int x, int y, int z, uint8_t room_id) {
    if (!map || !map->ids || !in_bounds(map, x, y, z)) return;
    map->ids[flat_index(map, x, y, z)] = room_id;
}
