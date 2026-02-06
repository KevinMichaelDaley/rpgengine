/**
 * @file phys_pool_query.c
 * @brief Body pool query functions.
 *
 * Non-static functions: active_count (1).
 */

#include "ferrum/physics/phys_pool.h"

uint32_t phys_body_pool_active_count(const phys_body_pool_t *pool) {
    if (!pool) {
        return 0;
    }
    return pool->count;
}
