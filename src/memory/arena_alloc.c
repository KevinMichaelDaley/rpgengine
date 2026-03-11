#include <stdatomic.h>
#include <stddef.h>

#include "ferrum/memory/arena.h"

static size_t align_up(size_t value, size_t alignment) {
    return (value + (alignment - 1u)) & ~(alignment - 1u);
}

void *arena_alloc(arena_t *arena, size_t alignment, size_t size) {
    if (arena == NULL || alignment == 0u) {
        return NULL;
    }

    uintptr_t base = (uintptr_t)arena->buffer;

    /* CAS loop: atomically bump the offset. */
    size_t old_offset = atomic_load_explicit(&arena->offset,
                                             memory_order_relaxed);
    for (;;) {
        uintptr_t current = base + old_offset;
        uintptr_t aligned = (uintptr_t)align_up((size_t)current, alignment);
        if (aligned < base) {
            return NULL; /* address space wraparound */
        }
        size_t aligned_offset = (size_t)(aligned - base);
        if (aligned_offset > arena->capacity) {
            return NULL;
        }
        if (size > arena->capacity - aligned_offset) {
            return NULL;
        }
        size_t new_offset = aligned_offset + size;
        if (size == 0u) {
            new_offset = old_offset; /* zero-size alloc: don't advance */
        }

        if (atomic_compare_exchange_weak_explicit(
                &arena->offset, &old_offset, new_offset,
                memory_order_acquire, memory_order_relaxed)) {
            return arena->buffer + aligned_offset;
        }
        /* CAS failed — old_offset was reloaded by compare_exchange, retry. */
    }
}
