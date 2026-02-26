/**
 * @file edit_selection_info.c
 * @brief Selection read-only query operations.
 */

#include "ferrum/editor/edit_selection.h"
#include <stddef.h>

uint32_t edit_selection_count(const edit_selection_t *sel) {
    if (!sel) return 0;
    return sel->count;
}

const uint32_t *edit_selection_ids(const edit_selection_t *sel) {
    if (!sel || sel->count == 0) return NULL;
    return sel->ids;
}
