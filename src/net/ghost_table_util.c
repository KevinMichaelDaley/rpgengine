/**
 * @file ghost_table_util.c
 * @brief Ghost table utility functions (clear, count).
 *
 * Non-static functions: 2 (clear, count).
 */

#include "ferrum/net/ghost_table.h"

void net_ghost_table_clear(net_ghost_table_t *table) {
    if (!table || !table->entries) { return; }
    for (uint32_t i = 0; i < table->capacity; i++) {
        table->entries[i].used = 0;
    }
    table->count = 0;
}

uint32_t net_ghost_table_count(const net_ghost_table_t *table) {
    if (!table) { return 0; }
    return table->count;
}
