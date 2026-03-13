/**
 * @file edit_autosave_check.c
 * @brief Autosave timing checks and save completion.
 */

#include "ferrum/editor/protocol/edit_autosave.h"

bool edit_autosave_should_save(const edit_autosave_t *autosave,
                               uint64_t now_ms) {
    if (!autosave || !autosave->initialized) return false;

    /* Force save always triggers */
    if (autosave->force_pending) return true;

    /* Interval save only if dirty */
    if (!autosave->dirty) return false;

    uint64_t elapsed = now_ms - autosave->last_save_ms;
    return elapsed >= autosave->interval_ms;
}

void edit_autosave_did_save(edit_autosave_t *autosave, uint64_t now_ms) {
    if (!autosave || !autosave->initialized) return;
    autosave->dirty         = false;
    autosave->force_pending = false;
    autosave->last_save_ms  = now_ms;
}
