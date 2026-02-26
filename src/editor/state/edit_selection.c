/**
 * @file edit_selection.c
 * @brief Selection system — lifecycle and mutation operations.
 *
 * Uses a sorted array with binary search for O(log n) lookup.
 * Insertions maintain sorted order via memmove.
 */

#include "ferrum/editor/edit_selection.h"
#include <stdlib.h>
#include <string.h>

/* ----------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ----------------------------------------------------------------------- */

/**
 * @brief Binary search for id in the sorted array.
 * @return Index where id is (if found) or should be inserted (if not).
 */
static uint32_t bsearch_idx_(const edit_selection_t *sel, uint32_t id,
                              bool *found) {
    uint32_t lo = 0, hi = sel->count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (sel->ids[mid] < id)      lo = mid + 1;
        else if (sel->ids[mid] > id) hi = mid;
        else { *found = true; return mid; }
    }
    *found = false;
    return lo;
}

/* ----------------------------------------------------------------------- */
/* Lifecycle                                                                 */
/* ----------------------------------------------------------------------- */

bool edit_selection_init(edit_selection_t *sel) {
    if (!sel) return false;
    sel->ids = (uint32_t *)malloc(EDIT_SELECTION_MAX * sizeof(uint32_t));
    if (!sel->ids) return false;
    sel->count    = 0;
    sel->capacity = EDIT_SELECTION_MAX;
    sel->version  = 0;
    return true;
}

void edit_selection_destroy(edit_selection_t *sel) {
    if (!sel) return;
    free(sel->ids);
    sel->ids   = NULL;
    sel->count = 0;
}

/* ----------------------------------------------------------------------- */
/* Mutation                                                                  */
/* ----------------------------------------------------------------------- */

bool edit_selection_add(edit_selection_t *sel, uint32_t id) {
    if (!sel || sel->count >= sel->capacity) return false;

    bool found;
    uint32_t idx = bsearch_idx_(sel, id, &found);
    if (found) return false; /* Already selected. */

    /* Shift elements right to make room. */
    if (idx < sel->count) {
        memmove(&sel->ids[idx + 1], &sel->ids[idx],
                (sel->count - idx) * sizeof(uint32_t));
    }
    sel->ids[idx] = id;
    sel->count++;
    sel->version++;
    return true;
}

bool edit_selection_remove(edit_selection_t *sel, uint32_t id) {
    if (!sel || sel->count == 0) return false;

    bool found;
    uint32_t idx = bsearch_idx_(sel, id, &found);
    if (!found) return false;

    /* Shift elements left to fill gap. */
    if (idx + 1 < sel->count) {
        memmove(&sel->ids[idx], &sel->ids[idx + 1],
                (sel->count - idx - 1) * sizeof(uint32_t));
    }
    sel->count--;
    sel->version++;
    return true;
}

/* ----------------------------------------------------------------------- */
/* Query + toggle in selection_query.c                                       */
/* ----------------------------------------------------------------------- */
