/**
 * @file srd_room_map_copy.c
 * @brief Deep-copy for room maps.
 *
 * Non-static functions (1): srd_room_map_copy
 */
#include "ferrum/procgen/srd/srd_room_map.h"

#include <stdlib.h>
#include <string.h>

int srd_room_map_copy(srd_room_map_t *dst, const srd_room_map_t *src) {
    if (!dst || !src || !src->ids) return -1;

    size_t total = (size_t)src->nx * (size_t)src->ny * (size_t)src->nz;
    uint8_t *ids = (uint8_t *)malloc(total);
    if (!ids) return -1;

    memcpy(ids, src->ids, total);

    dst->ids = ids;
    dst->nx = src->nx;
    dst->ny = src->ny;
    dst->nz = src->nz;
    dst->n_rooms = src->n_rooms;
    memcpy(dst->types, src->types, sizeof(src->types));
    memcpy(dst->adj, src->adj, sizeof(src->adj));

    return 0;
}
