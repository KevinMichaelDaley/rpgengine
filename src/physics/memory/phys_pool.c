/**
 * @file phys_pool.c
 * @brief Body pool lifecycle and slot management.
 *
 * Non-static functions: init, destroy, add, is_active (4).
 */

#include "ferrum/physics/phys_pool.h"

#include <stdlib.h>
#include <string.h>

int phys_body_pool_init(phys_body_pool_t *pool, uint32_t capacity) {
    if (!pool || capacity == 0) {
        return -1;
    }

    pool->bodies_curr = calloc(capacity, sizeof(phys_body_t));
    pool->bodies_next = calloc(capacity, sizeof(phys_body_t));
    pool->active = calloc(capacity, sizeof(uint8_t));

    if (!pool->bodies_curr || !pool->bodies_next || !pool->active) {
        free(pool->bodies_curr);
        free(pool->bodies_next);
        free(pool->active);
        memset(pool, 0, sizeof(*pool));
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
    free(pool->bodies_curr);
    free(pool->bodies_next);
    free(pool->active);
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
