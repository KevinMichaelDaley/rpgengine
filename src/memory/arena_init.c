#include "ferrum/memory/arena.h"

void arena_init(arena_t *arena, void *buffer, size_t capacity) {
    if (arena == NULL) {
        return;
    }
    arena->buffer = (uint8_t *)buffer;
    arena->capacity = capacity;
    atomic_store_explicit(&arena->offset, 0, memory_order_relaxed);
}
