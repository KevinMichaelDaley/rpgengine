#include "ferrum/memory/arena.h"

void arena_reset(arena_t *arena) {
    if (arena == NULL) {
        return;
    }
    arena->offset = 0;
}
