/**
 * @file phys_arena_query.c
 * @brief Frame arena query functions.
 *
 * Non-static functions: used, remaining (2).
 */

#include "ferrum/physics/phys_pool.h"

size_t phys_frame_arena_used(const phys_frame_arena_t *arena) {
    if (!arena) {
        return 0;
    }
    return arena->arena.offset;
}

size_t phys_frame_arena_remaining(const phys_frame_arena_t *arena) {
    if (!arena) {
        return 0;
    }
    return arena->arena.capacity - arena->arena.offset;
}
