/**
 * @file edit_selection_query.c
 * @brief Selection query operations and toggle.
 */

#include "ferrum/editor/edit_selection.h"
#include <string.h>

/**
 * @brief Binary search for id in the sorted array.
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

bool edit_selection_toggle(edit_selection_t *sel, uint32_t id) {
    if (!sel) return false;
    if (edit_selection_contains(sel, id)) {
        edit_selection_remove(sel, id);
        return false; /* Now deselected. */
    }
    edit_selection_add(sel, id);
    return true; /* Now selected. */
}

void edit_selection_clear(edit_selection_t *sel) {
    if (!sel) return;
    if (sel->count > 0) {
        sel->count = 0;
        sel->version++;
    }
}

bool edit_selection_contains(const edit_selection_t *sel, uint32_t id) {
    if (!sel || sel->count == 0) return false;
    bool found;
    bsearch_idx_(sel, id, &found);
    return found;
}
