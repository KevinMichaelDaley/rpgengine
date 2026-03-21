/**
 * @file undo_rebase_record.c
 * @brief Rebase-aware undo recording.
 *
 * Non-static functions (1 / 4 limit):
 *   edit_undo_record_rebase
 */

#include "ferrum/editor/undo_rebase.h"
#include "ferrum/editor/undo_conflict.h"
#include "ferrum/editor/edit_undo.h"
#include <string.h>

/** @brief Maximum displaced entries we can buffer during rebase. */
#define REBASE_MAX_DISPLACED 256

bool edit_undo_record_rebase(edit_undo_stack_t *stack,
                              const edit_undo_entry_t *entry,
                              const void *snapshot, uint32_t snapshot_size,
                              undo_branches_t *branches) {
    if (!stack || !entry) return false;

    /* If no branches storage or no redo entries, fall back to normal record. */
    uint32_t redo_count = edit_undo_redo_count(stack);
    if (!branches || redo_count == 0) {
        return edit_undo_record(stack, entry, snapshot, snapshot_size);
    }

    /* Step 1: Copy displaced entries (cursor..top) before they are truncated. */
    uint32_t displaced_count = redo_count;
    if (displaced_count > REBASE_MAX_DISPLACED) {
        displaced_count = REBASE_MAX_DISPLACED;
    }

    edit_undo_entry_t displaced[REBASE_MAX_DISPLACED];
    for (uint32_t i = 0; i < displaced_count; i++) {
        uint32_t abs_idx = stack->cursor + i;
        uint32_t ring_idx = abs_idx % stack->capacity;
        displaced[i] = stack->entries[ring_idx];
        /* Note: snapshot_data pointers may become stale after truncation.
         * We clear them for safety — orphan branches cannot recover
         * snapshot data from the compacted arena. */
        displaced[i].snapshot_data = NULL;
        displaced[i].snapshot_size = 0;
    }

    /* Step 2: Record the new entry (this truncates redo + records). */
    if (!edit_undo_record(stack, entry, snapshot, snapshot_size)) {
        return false;
    }

    /* Step 3: Separate displaced entries into conflicting and non-conflicting. */
    undo_conflict_key_t new_key = undo_conflict_key_extract(entry);

    bool conflicting[REBASE_MAX_DISPLACED];
    bool has_any_conflict = false;

    for (uint32_t i = 0; i < displaced_count; i++) {
        undo_conflict_key_t dk = undo_conflict_key_extract(&displaced[i]);
        conflicting[i] = undo_conflict_check(&new_key, &dk);
        if (conflicting[i]) has_any_conflict = true;
    }

    /* Step 4: Handle group coherence — if any entry in a group conflicts,
     * the whole group is conflicting. */
    for (uint32_t i = 0; i < displaced_count; i++) {
        if (!conflicting[i]) continue;
        uint32_t gid = displaced[i].group_id;
        if (gid == EDIT_UNDO_NO_GROUP) continue;

        /* Mark all entries in the same group as conflicting. */
        for (uint32_t j = 0; j < displaced_count; j++) {
            if (displaced[j].group_id == gid) {
                conflicting[j] = true;
            }
        }
    }

    /* Step 5: Re-record non-conflicting entries (rebased). */
    for (uint32_t i = 0; i < displaced_count; i++) {
        if (conflicting[i]) continue;
        edit_undo_record(stack, &displaced[i], NULL, 0);
    }

    /* Step 6: Push conflicting entries to a new orphan branch. */
    if (has_any_conflict) {
        /* Get the next branch slot (ring). */
        uint32_t slot = branches->head;
        undo_branch_t *branch = &branches->branches[slot];
        branch->count = 0;
        branch->diverge_id = stack->cursor - 1; /* Just before the new entry. */

        for (uint32_t i = 0; i < displaced_count; i++) {
            if (!conflicting[i]) continue;
            if (branch->count >= branch->capacity) break;
            branch->entries[branch->count++] = displaced[i];
        }

        /* Advance ring pointer. */
        branches->head = (branches->head + 1) % branches->max_branches;
        if (branches->count < branches->max_branches) {
            branches->count++;
        }
    }

    return true;
}
