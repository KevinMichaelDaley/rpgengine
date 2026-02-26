/**
 * @file edit_undo_group.c
 * @brief Undo group management and query operations.
 */

#include "ferrum/editor/edit_undo.h"

/* ----------------------------------------------------------------------- */
/* Groups                                                                    */
/* ----------------------------------------------------------------------- */

uint32_t edit_undo_begin_group(edit_undo_stack_t *stack) {
    if (!stack) return EDIT_UNDO_NO_GROUP;
    stack->active_group = stack->next_group++;
    /* Guard against wrapping to 0 (reserved for no-group). */
    if (stack->next_group == EDIT_UNDO_NO_GROUP) stack->next_group = 1;
    return stack->active_group;
}

void edit_undo_end_group(edit_undo_stack_t *stack) {
    if (!stack) return;
    stack->active_group = EDIT_UNDO_NO_GROUP;
}

/* ----------------------------------------------------------------------- */
/* Query                                                                     */
/* ----------------------------------------------------------------------- */

uint32_t edit_undo_count(const edit_undo_stack_t *stack) {
    if (!stack) return 0;
    return stack->cursor - stack->base;
}

uint32_t edit_undo_redo_count(const edit_undo_stack_t *stack) {
    if (!stack) return 0;
    return stack->top - stack->cursor;
}
