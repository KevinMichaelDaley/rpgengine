/**
 * @file ghost_table.c
 * @brief Ghost table: server entity → client entity mapping (core ops).
 *
 * Linear-scan fixed-capacity table.  Suitable for small-to-medium
 * entity counts typical in replication (hundreds, not thousands).
 *
 * Non-static functions: 4 (init, create, lookup, destroy).
 * See ghost_table_util.c for clear and count.
 */

#include "ferrum/net/ghost_table.h"

#include <string.h>

void net_ghost_table_init(net_ghost_table_t *table,
                          net_ghost_entry_t *entries,
                          uint32_t capacity) {
    if (!table) { return; }
    table->entries = entries;
    table->capacity = capacity;
    table->count = 0;
    if (entries && capacity > 0) {
        memset(entries, 0, capacity * sizeof(net_ghost_entry_t));
    }
}

int net_ghost_table_create(net_ghost_table_t *table,
                           uint32_t server_id,
                           net_ghost_entity_t local) {
    if (!table || !table->entries) { return NET_GHOST_ERR_INVALID; }

    /* Check for duplicate. */
    for (uint32_t i = 0; i < table->capacity; i++) {
        if (table->entries[i].used && table->entries[i].server_id == server_id) {
            return NET_GHOST_DUPLICATE;
        }
    }

    /* Find a free slot. */
    for (uint32_t i = 0; i < table->capacity; i++) {
        if (!table->entries[i].used) {
            table->entries[i].server_id = server_id;
            table->entries[i].local = local;
            table->entries[i].used = 1;
            table->count++;
            return NET_GHOST_OK;
        }
    }

    return NET_GHOST_FULL;
}

int net_ghost_table_lookup(const net_ghost_table_t *table,
                           uint32_t server_id,
                           net_ghost_entity_t *out) {
    if (!table || !table->entries || !out) { return NET_GHOST_ERR_INVALID; }

    for (uint32_t i = 0; i < table->capacity; i++) {
        if (table->entries[i].used && table->entries[i].server_id == server_id) {
            *out = table->entries[i].local;
            return NET_GHOST_OK;
        }
    }

    return NET_GHOST_NOT_FOUND;
}

int net_ghost_table_destroy(net_ghost_table_t *table,
                            uint32_t server_id) {
    if (!table || !table->entries) { return NET_GHOST_ERR_INVALID; }

    for (uint32_t i = 0; i < table->capacity; i++) {
        if (table->entries[i].used && table->entries[i].server_id == server_id) {
            table->entries[i].used = 0;
            table->count--;
            return NET_GHOST_OK;
        }
    }

    return NET_GHOST_NOT_FOUND;
}
