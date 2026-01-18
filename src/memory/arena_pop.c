#include "ferrum/memory/arena.h"

int arena_pop_to_mark(arena_t *arena, size_t mark) {
    if (arena == NULL || mark > arena->capacity) {
        return -1;
    }
    if (mark > arena->offset) {
        return -1;
    }
    arena->offset = mark;
    return 0;
}
