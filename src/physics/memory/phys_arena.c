/**
 * @file phys_arena.c
 * @brief Frame arena lifecycle and allocation.
 *
 * Non-static functions: init, destroy, alloc, reset (4).
 */

#include "ferrum/physics/phys_pool.h"

#include <stdlib.h>
#include <string.h>

int phys_frame_arena_init(phys_frame_arena_t *arena, size_t size) {
    if (!arena || size == 0) {
        return -1;
    }

    arena->base = malloc(size);
    if (!arena->base) {
        return -1;
    }

    arena->capacity = size;
    arena->offset = 0;
    return 0;
}

void phys_frame_arena_destroy(phys_frame_arena_t *arena) {
    if (!arena) {
        return;
    }
    free(arena->base);
    memset(arena, 0, sizeof(*arena));
}

void *phys_frame_arena_alloc(phys_frame_arena_t *arena, size_t size, size_t align) {
    if (!arena || !arena->base) {
        return NULL;
    }

    /* Compute aligned offset. align must be a power of two. */
    uintptr_t current = (uintptr_t)(arena->base + arena->offset);
    uintptr_t aligned = (current + (align - 1)) & ~(align - 1);
    size_t padding = (size_t)(aligned - current);
    size_t total = padding + size;

    if (arena->offset + total > arena->capacity) {
        return NULL;
    }

    arena->offset += total;
    return (void *)aligned;
}

void phys_frame_arena_reset(phys_frame_arena_t *arena) {
    if (!arena) {
        return;
    }
    arena->offset = 0;
}
