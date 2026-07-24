/**
 * @file refl_slots.c
 * @brief Atlas slot pool (see refl_slots.h).
 */
#include "ferrum/renderer/gi/refl_slots.h"

#include <stddef.h>

/* Free-list sentinel within u16 links (capacity is capped below this). */
#define SLOT_END 0xFFFFu

void refl_slot_pool_init(refl_slot_pool_t *pool, uint16_t *storage,
                         uint32_t capacity)
{
    if (pool == NULL)
        return;
    if (capacity > 0xFFFEu)
        capacity = 0xFFFEu;
    pool->next = storage;
    pool->capacity = (storage != NULL) ? capacity : 0u;
    pool->used = 0u;
    pool->head = (pool->capacity > 0u) ? 0u : REFL_SLOT_NONE;
    for (uint32_t i = 0; i < pool->capacity; ++i)
        storage[i] = (i + 1u < pool->capacity) ? (uint16_t)(i + 1u)
                                               : SLOT_END;
}

uint32_t refl_slot_alloc(refl_slot_pool_t *pool)
{
    if (pool == NULL || pool->head == REFL_SLOT_NONE ||
        pool->head >= pool->capacity)
        return REFL_SLOT_NONE;
    uint32_t slot = pool->head;
    uint16_t nx = pool->next[slot];
    pool->head = (nx == SLOT_END) ? REFL_SLOT_NONE : nx;
    pool->next[slot] = SLOT_END;   /* marks "allocated" for double-free. */
    pool->used += 1u;
    return slot;
}

void refl_slot_free(refl_slot_pool_t *pool, uint32_t slot)
{
    if (pool == NULL || slot >= pool->capacity)
        return;
    /* Already free? An allocated slot links SLOT_END and is not the head;
     * walk-free detection would be O(n) -- instead track via the invariant
     * that a freed slot re-enters the list head, so freeing the current
     * head again is the only O(1)-detectable double free. Guard both. */
    if (pool->head == slot)
        return;
    if (pool->next[slot] != SLOT_END)
        return;                     /* linked into the free list already. */
    pool->next[slot] = (pool->head == REFL_SLOT_NONE)
                           ? SLOT_END
                           : (uint16_t)pool->head;
    pool->head = slot;
    if (pool->used > 0u)
        pool->used -= 1u;
}
