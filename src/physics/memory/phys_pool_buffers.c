/**
 * @file phys_pool_buffers.c
 * @brief Body pool buffer access, swap, and removal.
 *
 * Non-static functions: get_curr, get_next, swap_buffers, remove (4).
 */

#include "ferrum/physics/phys_pool.h"

#include <string.h>

phys_body_t *phys_body_pool_get_curr(phys_body_pool_t *pool, uint32_t index) {
    if (!pool || index >= pool->capacity || !pool->active[index]) {
        return NULL;
    }
    return &pool->bodies_curr[index];
}

phys_body_t *phys_body_pool_get_next(phys_body_pool_t *pool, uint32_t index) {
    if (!pool || index >= pool->capacity || !pool->active[index]) {
        return NULL;
    }
    return &pool->bodies_next[index];
}

void phys_body_pool_swap_buffers(phys_body_pool_t *pool) {
    if (!pool) {
        return;
    }
    /* Simple double-buffer swap: curr ↔ next. */
    phys_body_t *tmp = pool->bodies_curr;
    pool->bodies_curr = pool->bodies_next;
    pool->bodies_next = tmp;
}

void phys_body_pool_remove(phys_body_pool_t *pool, uint32_t index) {
    if (!pool || index >= pool->capacity || !pool->active[index]) {
        return;
    }
    memset(&pool->bodies_curr[index], 0, sizeof(phys_body_t));
    memset(&pool->bodies_next[index], 0, sizeof(phys_body_t));
    memset(&pool->bodies_ccd_prev[index], 0, sizeof(phys_body_t));
    memset(&pool->bodies_net[index], 0, sizeof(phys_body_t));
    atomic_store(&pool->net_dirty[index], 0);
    pool->active[index] = 0;
    pool->count--;
}
