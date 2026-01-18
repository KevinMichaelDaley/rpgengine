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
    uintptr_t current = base + arena->offset;
    size_t aligned_offset = align_up((size_t)current, alignment) - base;
    if (aligned_offset > arena->capacity) {
        return NULL;
    }
    if (size > arena->capacity - aligned_offset) {
        return NULL;
    }
    void *ptr = arena->buffer + aligned_offset;
    if (size == 0u) {
        return ptr;
    }
    arena->offset = aligned_offset + size;
    return ptr;
}
