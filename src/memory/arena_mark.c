#include "ferrum/memory/arena.h"

size_t arena_mark(const arena_t *arena) {
    if (arena == NULL) {
        return 0u;
    }
    return atomic_load_explicit(&arena->offset, memory_order_acquire);
}
