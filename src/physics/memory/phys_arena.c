/**
 * @file phys_arena.c
 * @brief Frame arena lifecycle and allocation.
 *
 * Delegates to the general-purpose arena_t allocator while owning
 * the backing buffer via malloc/free.
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

    arena->buffer = malloc(size);
    if (!arena->buffer) {
        return -1;
    }

    arena_init(&arena->arena, arena->buffer, size);
    return 0;
}

void phys_frame_arena_destroy(phys_frame_arena_t *arena) {
    if (!arena) {
        return;
    }
    free(arena->buffer);
    memset(arena, 0, sizeof(*arena));
}

void *phys_frame_arena_alloc(phys_frame_arena_t *arena, size_t size, size_t align) {
    if (!arena || !arena->buffer) {
        return NULL;
    }

    return arena_alloc(&arena->arena, align, size);
}

void phys_frame_arena_reset(phys_frame_arena_t *arena) {
    if (!arena) {
        return;
    }
    arena_reset(&arena->arena);
}
