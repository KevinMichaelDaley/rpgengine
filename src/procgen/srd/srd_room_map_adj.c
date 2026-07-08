/**
 * @file srd_room_map_adj.c
 * @brief Room adjacency computation and volume counting.
 *
 * Non-static functions (4): srd_room_map_compute_adjacency,
 *                           srd_room_map_are_adjacent,
 *                           srd_room_map_set_adjacent,
 *                           srd_room_map_count_volume
 */
#include "ferrum/procgen/srd/srd_room_map.h"

#include <string.h>

/* ── Helpers ───────────────────────────────────────────────────── */

/** @brief 6-connected neighbour offsets (face-adjacent). */
static const int DX[6] = { 1, -1,  0,  0,  0,  0};
static const int DY[6] = { 0,  0,  1, -1,  0,  0};
static const int DZ[6] = { 0,  0,  0,  0,  1, -1};

static inline int adj_index(uint8_t a, uint8_t b) {
    return (int)(a - 1) * SRD_ROOM_MAP_MAX_ROOMS + (int)(b - 1);
}

/* ── Public API ────────────────────────────────────────────────── */

void srd_room_map_compute_adjacency(srd_room_map_t *map) {
    if (!map || !map->ids) return;

    /* Clear adjacency matrix */
    memset(map->adj, 0, sizeof(map->adj));

    for (int z = 0; z < map->nz; z++) {
        for (int y = 0; y < map->ny; y++) {
            for (int x = 0; x < map->nx; x++) {
                uint8_t id = map->ids[z * map->ny * map->nx + y * map->nx + x];
                if (id == 0) continue;

                /* Check 6 face-adjacent neighbours */
                for (int d = 0; d < 6; d++) {
                    int nx = x + DX[d];
                    int ny = y + DY[d];
                    int nz = z + DZ[d];
                    if (nx < 0 || nx >= map->nx ||
                        ny < 0 || ny >= map->ny ||
                        nz < 0 || nz >= map->nz)
                        continue;

                    uint8_t nid = map->ids[nz * map->ny * map->nx + ny * map->nx + nx];
                    if (nid != 0 && nid != id) {
                        map->adj[adj_index(id, nid)] = 1;
                        map->adj[adj_index(nid, id)] = 1;
                    }
                }
            }
        }
    }
}

bool srd_room_map_are_adjacent(const srd_room_map_t *map, uint8_t a, uint8_t b) {
    if (!map || a == 0 || b == 0 ||
        a > (uint8_t)map->n_rooms || b > (uint8_t)map->n_rooms)
        return false;
    return map->adj[adj_index(a, b)] != 0;
}

void srd_room_map_set_adjacent(srd_room_map_t *map, uint8_t a, uint8_t b, bool adjacent) {
    if (!map || a == 0 || b == 0 ||
        a > (uint8_t)map->n_rooms || b > (uint8_t)map->n_rooms)
        return;
    uint8_t val = adjacent ? 1 : 0;
    map->adj[adj_index(a, b)] = val;
    map->adj[adj_index(b, a)] = val;
}

int srd_room_map_count_volume(const srd_room_map_t *map, uint8_t room_id) {
    if (!map || !map->ids || room_id == 0) return 0;
    int total = map->nx * map->ny * map->nz;
    int count = 0;
    for (int i = 0; i < total; i++) {
        if (map->ids[i] == room_id) count++;
    }
    return count;
}
