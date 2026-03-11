#include "ferrum/memory/arena.h"

int arena_pop_to_mark(arena_t *arena, size_t mark) {
    if (arena == NULL || mark > arena->capacity) {
        return -1;
    }
    size_t current = atomic_load_explicit(&arena->offset,
                                          memory_order_acquire);
    if (mark > current) {
        return -1;
    }
    atomic_store_explicit(&arena->offset, mark, memory_order_release);
    return 0;
}
