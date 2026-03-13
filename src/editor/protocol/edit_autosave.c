/**
 * @file edit_autosave.c
 * @brief Autosave lifecycle and state management.
 */

#include "ferrum/editor/protocol/edit_autosave.h"

#include <string.h>

bool edit_autosave_init(edit_autosave_t *autosave,
                        const edit_autosave_config_t *config) {
    if (!autosave) return false;
    if (!config || !config->save_path) return false;

    memset(autosave, 0, sizeof(*autosave));

    autosave->interval_ms = config->interval_ms > 0
                                ? config->interval_ms
                                : EDIT_AUTOSAVE_DEFAULT_INTERVAL_MS;

    size_t path_len = strlen(config->save_path);
    if (path_len >= EDIT_AUTOSAVE_MAX_PATH) return false;
    memcpy(autosave->save_path, config->save_path, path_len + 1);

    autosave->last_save_ms    = 0;
    autosave->dirty           = false;
    autosave->force_pending   = false;
    autosave->initialized     = true;
    return true;
}

void edit_autosave_destroy(edit_autosave_t *autosave) {
    if (!autosave || !autosave->initialized) return;
    autosave->initialized = false;
}

void edit_autosave_mark_dirty(edit_autosave_t *autosave) {
    if (!autosave || !autosave->initialized) return;
    autosave->dirty = true;
}

void edit_autosave_request_force(edit_autosave_t *autosave) {
    if (!autosave || !autosave->initialized) return;
    autosave->force_pending = true;
}
