/**
 * @file refl_slots.h
 * @brief Atlas tile-slot pool for streamed reflection probes (rpg-wlh9):
 *        a fixed atlas of N octahedral tile slots; probes take a slot when
 *        their chunk pages in and release it on evict. O(1) alloc/free via
 *        an intrusive free-list in caller-owned storage. No allocation.
 */
#ifndef FERRUM_RENDERER_GI_REFL_SLOTS_H
#define FERRUM_RENDERER_GI_REFL_SLOTS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Returned by refl_slot_alloc when the pool is exhausted. */
#define REFL_SLOT_NONE 0xFFFFFFFFu

/** Slot pool over caller-owned u16 storage (one entry per slot). */
typedef struct refl_slot_pool {
    uint16_t *next;      /**< caller-owned [capacity] free-list links. */
    uint32_t capacity;
    uint32_t head;       /**< first free slot (capacity = none). */
    uint32_t used;
} refl_slot_pool_t;

/** Initialise: every slot free. NULL storage -> zero-capacity pool. */
void refl_slot_pool_init(refl_slot_pool_t *pool, uint16_t *storage,
                         uint32_t capacity);

/** Take a free slot; REFL_SLOT_NONE when exhausted. */
uint32_t refl_slot_alloc(refl_slot_pool_t *pool);

/** Release @p slot. Out-of-range and double frees are ignored. */
void refl_slot_free(refl_slot_pool_t *pool, uint32_t slot);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_GI_REFL_SLOTS_H */
