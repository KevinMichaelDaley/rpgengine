/**
 * @file phys_pool_net.c
 * @brief Network authority buffer access for the triple-buffered body pool.
 *
 * The recv thread writes server-authoritative body state into bodies_net
 * and sets per-slot atomic dirty flags.  The prediction thread atomically
 * consumes dirty flags and reconciles before integrating.
 *
 * Non-static functions: get_net, mark_net_dirty, consume_net_dirty,
 *                       write_net (4).
 */

#include "ferrum/physics/phys_pool.h"

#include <string.h>

phys_body_t *phys_body_pool_get_net(phys_body_pool_t *pool, uint32_t index) {
    if (!pool || index >= pool->capacity || !pool->active[index]) {
        return NULL;
    }
    return &pool->bodies_net[index];
}

void phys_body_pool_mark_net_dirty(phys_body_pool_t *pool, uint32_t index) {
    if (!pool || index >= pool->capacity) return;
    atomic_store(&pool->net_dirty[index], 1);
}

bool phys_body_pool_consume_net_dirty(phys_body_pool_t *pool, uint32_t index) {
    if (!pool || index >= pool->capacity) return false;
    return atomic_exchange(&pool->net_dirty[index], 0) != 0;
}

void phys_body_pool_write_net(phys_body_pool_t *pool, uint32_t index,
                              const phys_body_t *body) {
    if (!pool || !body || index >= pool->capacity) return;
    pool->bodies_net[index] = *body;
    atomic_store(&pool->net_dirty[index], 1);
}
