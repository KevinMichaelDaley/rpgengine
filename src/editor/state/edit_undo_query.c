/**
 * @file edit_undo_query.c
 * @brief Undo stack entry access by absolute index.
 *
 * Non-static functions (1 / 4 limit):
 *   edit_undo_entry_at
 */

#include "ferrum/editor/edit_undo.h"

const edit_undo_entry_t *edit_undo_entry_at(const edit_undo_stack_t *stack,
                                             uint32_t abs_index) {
    if (!stack) return NULL;
    if (abs_index < stack->base || abs_index >= stack->top) return NULL;
    uint32_t idx = abs_index % stack->capacity;
    return &stack->entries[idx];
}
