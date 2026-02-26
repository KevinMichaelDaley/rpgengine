/**
 * @file edit_undo.c
 * @brief Undo stack lifecycle and recording operations.
 */

#include "ferrum/editor/edit_undo.h"
#include <stdlib.h>
#include <string.h>

/* ----------------------------------------------------------------------- */
/* Internal helpers                                                          */
/* ----------------------------------------------------------------------- */

/** @brief Get the ring index for an absolute offset. */
static uint32_t ring_idx_(const edit_undo_stack_t *stack, uint32_t offset) {
    return offset % stack->capacity;
}

/**
 * @brief Allocate snapshot bytes from the arena.
 *
 * If the arena is full, evicts the oldest entries (advancing base)
 * until enough space is available. Returns NULL only if size > arena_cap.
 */
static void *arena_snapshot_alloc_(edit_undo_stack_t *stack, uint32_t size) {
    if (size == 0) return NULL;
    if (size > stack->arena_cap) return NULL;

    /* If not enough room, reset arena and evict old entries. */
    if (stack->arena_used + size > stack->arena_cap) {
        /* Evict oldest half of entries to reclaim arena space. */
        uint32_t count = stack->cursor - stack->base;
        uint32_t evict = (count > 1) ? count / 2 : count;
        stack->base += evict;
        if (stack->base > stack->cursor)
            stack->base = stack->cursor;

        /* Reset arena and re-copy surviving entries' snapshots. */
        stack->arena_used = 0;
        for (uint32_t i = stack->base; i < stack->cursor; ++i) {
            edit_undo_entry_t *e = &stack->entries[ring_idx_(stack, i)];
            if (e->snapshot_data && e->snapshot_size > 0) {
                if (stack->arena_used + e->snapshot_size <= stack->arena_cap) {
                    void *new_ptr = stack->arena_buf + stack->arena_used;
                    memcpy(new_ptr, e->snapshot_data, e->snapshot_size);
                    e->snapshot_data = new_ptr;
                    stack->arena_used += e->snapshot_size;
                    stack->arena_used = (stack->arena_used + 7) & ~(size_t)7;
                } else {
                    e->snapshot_data = NULL;
                    e->snapshot_size = 0;
                }
            }
        }
    }

    if (stack->arena_used + size > stack->arena_cap) return NULL;

    void *ptr = stack->arena_buf + stack->arena_used;
    stack->arena_used += size;
    stack->arena_used = (stack->arena_used + 7) & ~(size_t)7;
    return ptr;
}

/* ----------------------------------------------------------------------- */
/* Lifecycle                                                                 */
/* ----------------------------------------------------------------------- */

bool edit_undo_init(edit_undo_stack_t *stack, uint32_t capacity,
                    size_t arena_size) {
    if (!stack || capacity == 0 || arena_size == 0) return false;

    stack->entries = (edit_undo_entry_t *)calloc(capacity,
                                                  sizeof(edit_undo_entry_t));
    if (!stack->entries) return false;

    stack->arena_buf = (uint8_t *)malloc(arena_size);
    if (!stack->arena_buf) {
        free(stack->entries);
        stack->entries = NULL;
        return false;
    }

    stack->capacity     = capacity;
    stack->arena_cap    = arena_size;
    stack->arena_used   = 0;
    stack->base         = 0;
    stack->top          = 0;
    stack->cursor       = 0;
    stack->next_group   = 1;
    stack->active_group = EDIT_UNDO_NO_GROUP;
    return true;
}

void edit_undo_destroy(edit_undo_stack_t *stack) {
    if (!stack) return;
    free(stack->entries);
    free(stack->arena_buf);
    stack->entries   = NULL;
    stack->arena_buf = NULL;
    stack->capacity  = 0;
}

void edit_undo_clear(edit_undo_stack_t *stack) {
    if (!stack) return;
    stack->base       = 0;
    stack->top        = 0;
    stack->cursor     = 0;
    stack->arena_used = 0;
}

/* ----------------------------------------------------------------------- */
/* Recording                                                                 */
/* ----------------------------------------------------------------------- */

bool edit_undo_record(edit_undo_stack_t *stack, const edit_undo_entry_t *entry,
                      const void *snapshot, uint32_t snapshot_size) {
    if (!stack || !entry) return false;

    /* Truncate redo history: new action after undo discards forward entries. */
    stack->top = stack->cursor;

    /* If ring is full, advance base to evict oldest. */
    if (stack->top - stack->base >= stack->capacity) {
        stack->base = stack->top - stack->capacity + 1;
    }

    uint32_t idx = ring_idx_(stack, stack->top);
    stack->entries[idx] = *entry;

    /* Assign active group if in a group. */
    if (stack->active_group != EDIT_UNDO_NO_GROUP) {
        stack->entries[idx].group_id = stack->active_group;
    }

    /* Copy snapshot into arena if provided. */
    if (snapshot && snapshot_size > 0) {
        void *arena_ptr = arena_snapshot_alloc_(stack, snapshot_size);
        if (arena_ptr) {
            memcpy(arena_ptr, snapshot, snapshot_size);
            stack->entries[idx].snapshot_data = arena_ptr;
            stack->entries[idx].snapshot_size = snapshot_size;
        } else {
            stack->entries[idx].snapshot_data = NULL;
            stack->entries[idx].snapshot_size = 0;
        }
    } else {
        stack->entries[idx].snapshot_data = NULL;
        stack->entries[idx].snapshot_size = 0;
    }

    stack->top++;
    stack->cursor = stack->top;
    return true;
}
