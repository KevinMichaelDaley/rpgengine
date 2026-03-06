/**
 * @file phys_pool.c
 * @brief Body pool lifecycle and slot management.
 *
 * Non-static functions: init, destroy, add, is_active (4).
 */

#include "ferrum/physics/phys_pool.h"
#include "ferrum/memory/vm_alloc.h"

#include <string.h>

int phys_body_pool_init(phys_body_pool_t *pool, uint32_t capacity) {
    if (!pool || capacity == 0) {
        return -1;
    }

    /* Use demand-paged virtual memory so only touched pages consume RAM. */
    size_t body_bytes   = (size_t)capacity * sizeof(phys_body_t);
    size_t active_bytes = (size_t)capacity * sizeof(uint8_t);
    size_t dirty_bytes  = (size_t)capacity * sizeof(atomic_uchar);

    pool->bodies_curr     = vm_reserve(body_bytes);
    pool->bodies_next     = vm_reserve(body_bytes);
    pool->bodies_ccd_prev = vm_reserve(body_bytes);
    pool->bodies_net      = vm_reserve(body_bytes);
    pool->active    = vm_reserve(active_bytes);
    pool->net_dirty = vm_reserve(dirty_bytes);

    if (!pool->bodies_curr || !pool->bodies_next || !pool->bodies_ccd_prev
        || !pool->bodies_net || !pool->active || !pool->net_dirty) {
        phys_body_pool_destroy(pool);
        return -1;
    }

    pool->capacity = capacity;
    pool->count = 0;
    return 0;
}

void phys_body_pool_destroy(phys_body_pool_t *pool) {
    if (!pool) {
        return;
    }
    size_t body_bytes   = (size_t)pool->capacity * sizeof(phys_body_t);
    size_t active_bytes = (size_t)pool->capacity * sizeof(uint8_t);
    size_t dirty_bytes  = (size_t)pool->capacity * sizeof(atomic_uchar);

    vm_release(pool->bodies_curr,     body_bytes);
    vm_release(pool->bodies_next,     body_bytes);
    vm_release(pool->bodies_ccd_prev, body_bytes);
    vm_release(pool->bodies_net,      body_bytes);
    vm_release(pool->active,          active_bytes);
    vm_release(pool->net_dirty,       dirty_bytes);
    memset(pool, 0, sizeof(*pool));
}

int phys_body_pool_add(phys_body_pool_t *pool, const phys_body_t *body, uint32_t *index_out) {
    if (!pool || !body || !index_out) {
        return -1;
    }
    if (pool->count >= pool->capacity) {
        return -1;
    }

    /* Linear scan for first free slot. */
    for (uint32_t i = 0; i < pool->capacity; i++) {
        if (!pool->active[i]) {
            pool->bodies_curr[i] = *body;
            pool->bodies_next[i] = *body;
            pool->bodies_ccd_prev[i] = *body;
            pool->bodies_net[i]  = *body;
            pool->active[i] = 1;
            pool->count++;
            *index_out = i;
            return 0;
        }
    }

    return -1;  /* Should not reach here if count < capacity. */
}

bool phys_body_pool_is_active(const phys_body_pool_t *pool, uint32_t index) {
    if (!pool || index >= pool->capacity) {
        return false;
    }
    return pool->active[index] != 0;
}
