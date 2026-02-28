/**
 * @file mesh_undo.c
 * @brief Mesh undo/redo stack implementation.
 *
 * Non-static functions (4 of 4): init, destroy, push, undo.
 */
#include "ferrum/editor/mesh/mesh_undo.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Static: free a single snapshot                                      */
/* ------------------------------------------------------------------ */

static void free_snapshot_(mesh_undo_snapshot_t *snap) {
    free(snap->positions);
    free(snap->normals);
    free(snap->indices);
    free(snap->polygroup_ids);
    memset(snap, 0, sizeof(*snap));
}

/** Save current slot state into a snapshot. */
static bool save_snapshot_(mesh_undo_snapshot_t *snap,
                            const mesh_slot_t *slot) {
    uint32_t vc = slot->vertex_count;
    uint32_t ic = slot->index_count;
    uint32_t fc = ic / 3;

    snap->positions = malloc(vc * 3 * sizeof(float));
    snap->normals = malloc(vc * 3 * sizeof(float));
    snap->indices = malloc(ic * sizeof(uint32_t));
    snap->polygroup_ids = malloc(fc * sizeof(uint16_t));

    if (!snap->positions || !snap->normals || !snap->indices) {
        free_snapshot_(snap);
        return false;
    }

    memcpy(snap->positions, slot->positions, vc * 3 * sizeof(float));
    memcpy(snap->normals, slot->normals, vc * 3 * sizeof(float));
    memcpy(snap->indices, slot->indices, ic * sizeof(uint32_t));
    if (slot->polygroup_ids && snap->polygroup_ids) {
        memcpy(snap->polygroup_ids, slot->polygroup_ids, fc * sizeof(uint16_t));
    }
    snap->vertex_count = vc;
    snap->index_count = ic;
    return true;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

void mesh_undo_stack_init(mesh_undo_stack_t *stack) {
    if (!stack) return;
    memset(stack, 0, sizeof(*stack));
    stack->cursor = -1;
    stack->count = 0;
}

void mesh_undo_stack_destroy(mesh_undo_stack_t *stack) {
    if (!stack) return;
    for (int i = 0; i < stack->count; i++) {
        free_snapshot_(&stack->entries[i]);
    }
    memset(stack, 0, sizeof(*stack));
    stack->cursor = -1;
}

/* ------------------------------------------------------------------ */
/* Push                                                                */
/* ------------------------------------------------------------------ */

void mesh_undo_push(mesh_undo_stack_t *stack, const mesh_slot_t *slot) {
    if (!stack || !slot) return;

    /* Discard any redo entries above cursor */
    int new_pos = stack->cursor + 1;
    for (int i = new_pos; i < stack->count; i++) {
        free_snapshot_(&stack->entries[i]);
    }

    /* Wrap if at max */
    if (new_pos >= MESH_UNDO_MAX) {
        /* Shift everything down */
        free_snapshot_(&stack->entries[0]);
        for (int i = 1; i < MESH_UNDO_MAX; i++) {
            stack->entries[i-1] = stack->entries[i];
        }
        new_pos = MESH_UNDO_MAX - 1;
        memset(&stack->entries[new_pos], 0, sizeof(mesh_undo_snapshot_t));
    }

    save_snapshot_(&stack->entries[new_pos], slot);
    stack->cursor = new_pos;
    stack->count = new_pos + 1;
}
