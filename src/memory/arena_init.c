#include "ferrum/memory/arena.h"

void arena_init(arena_t *arena, void *buffer, size_t capacity) {
    if (arena == NULL) {
        return;
    }
    arena->buffer = (uint8_t *)buffer;
    arena->capacity = capacity;
    arena->offset = 0;
}
