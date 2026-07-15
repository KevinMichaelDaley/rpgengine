/**
 * @file shadow_slotmap.c
 * @brief First-fit shadow-layer allocator (see shadow_slotmap.h).
 */
#include "ferrum/renderer/resource/shadow_slotmap.h"

#include <stddef.h>

void shadow_slotmap_init(shadow_slotmap_t *m, uint8_t *backing, uint32_t capacity)
{
    if (m == NULL || backing == NULL || capacity == 0u)
        return;
    m->slots = backing;
    m->capacity = capacity;
    m->used = 0u;
    for (uint32_t i = 0; i < capacity; ++i)
        backing[i] = 0u;
}

int32_t shadow_slotmap_alloc(shadow_slotmap_t *m, uint32_t count)
{
    if (m == NULL || m->slots == NULL || count == 0u || count > m->capacity)
        return -1;
    /* Linear first-fit: find the first window of `count` consecutive free layers. */
    uint32_t run = 0;
    for (uint32_t i = 0; i < m->capacity; ++i) {
        if (m->slots[i] == 0u) {
            if (++run == count) {
                uint32_t base = i + 1u - count;
                for (uint32_t j = base; j < base + count; ++j)
                    m->slots[j] = 1u;
                m->used += count;
                return (int32_t)base;
            }
        } else {
            run = 0;
        }
    }
    return -1; /* no run fits. */
}

void shadow_slotmap_free(shadow_slotmap_t *m, uint32_t base, uint32_t count)
{
    if (m == NULL || m->slots == NULL || base >= m->capacity)
        return;
    uint32_t end = base + count;
    if (end > m->capacity)
        end = m->capacity; /* clamp over-range. */
    for (uint32_t i = base; i < end; ++i) {
        if (m->slots[i] != 0u) {
            m->slots[i] = 0u;
            --m->used;
        }
    }
}

uint32_t shadow_slotmap_used(const shadow_slotmap_t *m)
{
    return (m != NULL) ? m->used : 0u;
}
