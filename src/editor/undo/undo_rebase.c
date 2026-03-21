/**
 * @file undo_rebase.c
 * @brief Orphan branch lifecycle and query operations.
 *
 * Non-static functions (4 / 4 limit):
 *   undo_branches_init
 *   undo_branches_destroy
 *   undo_branches_count
 *   undo_branches_get
 */

#include "ferrum/editor/undo_rebase.h"
#include "ferrum/editor/edit_undo.h"
#include <stdlib.h>
#include <string.h>

bool undo_branches_init(undo_branches_t *branches,
                         uint32_t max_branches, uint32_t max_entries) {
    if (!branches || max_branches == 0 || max_entries == 0) return false;
    memset(branches, 0, sizeof(*branches));

    branches->branches = (undo_branch_t *)calloc(max_branches,
                                                   sizeof(undo_branch_t));
    if (!branches->branches) return false;

    /* Pre-allocate entry arrays for each branch slot. */
    for (uint32_t i = 0; i < max_branches; i++) {
        branches->branches[i].entries =
            (edit_undo_entry_t *)calloc(max_entries,
                                         sizeof(edit_undo_entry_t));
        if (!branches->branches[i].entries) {
            /* Cleanup on failure. */
            for (uint32_t j = 0; j < i; j++) {
                free(branches->branches[j].entries);
            }
            free(branches->branches);
            branches->branches = NULL;
            return false;
        }
        branches->branches[i].capacity = max_entries;
        branches->branches[i].count    = 0;
    }

    branches->max_branches = max_branches;
    branches->count        = 0;
    branches->head         = 0;
    branches->entry_cap    = max_entries;
    return true;
}

void undo_branches_destroy(undo_branches_t *branches) {
    if (!branches || !branches->branches) return;
    for (uint32_t i = 0; i < branches->max_branches; i++) {
        free(branches->branches[i].entries);
    }
    free(branches->branches);
    branches->branches = NULL;
    branches->max_branches = 0;
    branches->count = 0;
}

uint32_t undo_branches_count(const undo_branches_t *branches) {
    if (!branches) return 0;
    return branches->count;
}

const undo_branch_t *undo_branches_get(const undo_branches_t *branches,
                                        uint32_t index) {
    if (!branches || index >= branches->count) return NULL;

    /* Branches are stored in a ring. Index 0 = oldest. */
    uint32_t real_idx;
    if (branches->count < branches->max_branches) {
        real_idx = index;
    } else {
        real_idx = (branches->head + index) % branches->max_branches;
    }
    return &branches->branches[real_idx];
}
