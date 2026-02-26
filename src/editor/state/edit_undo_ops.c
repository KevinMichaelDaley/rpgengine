/**
 * @file edit_undo_ops.c
 * @brief Undo/redo step and peek operations.
 */

#include "ferrum/editor/edit_undo.h"

/** @brief Get the ring index for an absolute offset. */
static uint32_t ring_idx_(const edit_undo_stack_t *stack, uint32_t offset) {
    return offset % stack->capacity;
}

/* ----------------------------------------------------------------------- */
/* Peek                                                                      */
/* ----------------------------------------------------------------------- */

const edit_undo_entry_t *edit_undo_peek_undo(const edit_undo_stack_t *stack) {
    if (!stack || stack->cursor <= stack->base) return NULL;
    uint32_t idx = ring_idx_(stack, stack->cursor - 1);
    return &stack->entries[idx];
}

const edit_undo_entry_t *edit_undo_peek_redo(const edit_undo_stack_t *stack) {
    if (!stack || stack->cursor >= stack->top) return NULL;
    uint32_t idx = ring_idx_(stack, stack->cursor);
    return &stack->entries[idx];
}

/* ----------------------------------------------------------------------- */
/* Undo step                                                                 */
/* ----------------------------------------------------------------------- */

uint32_t edit_undo_step(edit_undo_stack_t *stack) {
    if (!stack || stack->cursor <= stack->base) return 0;

    /* Move cursor back one entry. */
    stack->cursor--;
    uint32_t idx = ring_idx_(stack, stack->cursor);
    uint32_t gid = stack->entries[idx].group_id;
    uint32_t undone = 1;

    /* If this entry is grouped, undo all entries in the same group. */
    if (gid != EDIT_UNDO_NO_GROUP) {
        while (stack->cursor > stack->base) {
            uint32_t prev_idx = ring_idx_(stack, stack->cursor - 1);
            if (stack->entries[prev_idx].group_id != gid) break;
            stack->cursor--;
            undone++;
        }
    }

    return undone;
}

/* ----------------------------------------------------------------------- */
/* Redo step                                                                 */
/* ----------------------------------------------------------------------- */

uint32_t edit_undo_redo(edit_undo_stack_t *stack) {
    if (!stack || stack->cursor >= stack->top) return 0;

    uint32_t idx = ring_idx_(stack, stack->cursor);
    uint32_t gid = stack->entries[idx].group_id;
    stack->cursor++;
    uint32_t redone = 1;

    /* If grouped, redo all entries in the same group. */
    if (gid != EDIT_UNDO_NO_GROUP) {
        while (stack->cursor < stack->top) {
            uint32_t next_idx = ring_idx_(stack, stack->cursor);
            if (stack->entries[next_idx].group_id != gid) break;
            stack->cursor++;
            redone++;
        }
    }

    return redone;
}
