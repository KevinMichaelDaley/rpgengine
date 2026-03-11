#include "ferrum/memory/arena.h"

void arena_reset(arena_t *arena) {
    if (arena == NULL) {
        return;
    }
    atomic_store_explicit(&arena->offset, 0, memory_order_release);
}
