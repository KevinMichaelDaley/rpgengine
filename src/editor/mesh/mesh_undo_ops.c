/**
 * @file mesh_undo_ops.c
 * @brief Mesh undo and redo operations.
 *
 * Non-static functions (2 of 4): mesh_undo, mesh_redo.
 */
#include "ferrum/editor/mesh/mesh_undo.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* mesh_undo                                                           */
/* ------------------------------------------------------------------ */

bool mesh_undo(mesh_undo_stack_t *stack, mesh_slot_t *slot) {
    if (!stack || !slot) return false;
    if (stack->cursor <= 0) return false;

    /* Move cursor back and restore that snapshot */
    stack->cursor--;
    mesh_undo_snapshot_t *snap = &stack->entries[stack->cursor];
    if (!snap->positions) { stack->cursor++; return false; }

    mesh_slot_clear(slot);
    mesh_slot_reserve_vertices(slot, snap->vertex_count);
    mesh_slot_reserve_indices(slot, snap->index_count);

    uint32_t vc = snap->vertex_count;
    uint32_t ic = snap->index_count;
    uint32_t fc = ic / 3;

    memcpy(slot->positions, snap->positions, vc * 3 * sizeof(float));
    memcpy(slot->normals, snap->normals, vc * 3 * sizeof(float));
    memcpy(slot->indices, snap->indices, ic * sizeof(uint32_t));
    if (slot->polygroup_ids && snap->polygroup_ids) {
        memcpy(slot->polygroup_ids, snap->polygroup_ids, fc * sizeof(uint16_t));
    }
    slot->vertex_count = vc;
    slot->index_count = ic;

    return true;
}

/* ------------------------------------------------------------------ */
/* mesh_redo                                                           */
/* ------------------------------------------------------------------ */

bool mesh_redo(mesh_undo_stack_t *stack, mesh_slot_t *slot) {
    if (!stack || !slot) return false;
    if (stack->cursor + 1 >= stack->count) return false;

    stack->cursor++;
    mesh_undo_snapshot_t *snap = &stack->entries[stack->cursor];
    if (!snap->positions) { stack->cursor--; return false; }

    mesh_slot_clear(slot);
    mesh_slot_reserve_vertices(slot, snap->vertex_count);
    mesh_slot_reserve_indices(slot, snap->index_count);

    uint32_t vc = snap->vertex_count;
    uint32_t ic = snap->index_count;
    uint32_t fc = ic / 3;

    memcpy(slot->positions, snap->positions, vc * 3 * sizeof(float));
    memcpy(slot->normals, snap->normals, vc * 3 * sizeof(float));
    memcpy(slot->indices, snap->indices, ic * sizeof(uint32_t));
    if (slot->polygroup_ids && snap->polygroup_ids) {
        memcpy(slot->polygroup_ids, snap->polygroup_ids, fc * sizeof(uint16_t));
    }
    slot->vertex_count = vc;
    slot->index_count = ic;

    return true;
}
