/**
 * @file asset_ref_widget.c
 * @brief Asset reference selector widget — state management functions.
 *
 * Non-static functions (4 / 4-function rule):
 *   1. asset_ref_init
 *   2. asset_ref_set_path
 *   3. asset_ref_accept
 *   4. asset_ref_confirm
 */

#include "ferrum/editor/panels/asset_ref_widget.h"
#include <string.h>

/* ---- Static helpers ---- */

/**
 * @brief Extract the filename portion from a path into the display buffer.
 *
 * Finds the last '/' and copies everything after it. If no slash,
 * copies the entire string. Truncates to ASSET_REF_DISPLAY_MAX-1.
 */
static void extract_display_name(asset_ref_state_t *state) {
    const char *slash = strrchr(state->path, '/');
    const char *name = slash ? (slash + 1) : state->path;

    size_t len = strlen(name);
    if (len >= ASSET_REF_DISPLAY_MAX) {
        len = ASSET_REF_DISPLAY_MAX - 1;
    }
    memcpy(state->display, name, len);
    state->display[len] = '\0';
}

/* ---- Public API ---- */

void asset_ref_init(asset_ref_state_t *state, uint8_t filter_type) {
    if (!state) { return; }
    memset(state, 0, sizeof(*state));
    state->filter_type = filter_type;
}

void asset_ref_set_path(asset_ref_state_t *state, const char *path) {
    if (!state) { return; }

    if (!path) {
        state->path[0] = '\0';
        state->display[0] = '\0';
        return;
    }

    size_t len = strlen(path);
    if (len >= ASSET_REF_PATH_MAX) {
        len = ASSET_REF_PATH_MAX - 1;
    }
    memcpy(state->path, path, len);
    state->path[len] = '\0';

    extract_display_name(state);
}

void asset_ref_accept(asset_ref_state_t *state, const char *path) {
    if (!state) { return; }
    asset_ref_set_path(state, path);
    state->confirmed = false;
}

void asset_ref_confirm(asset_ref_state_t *state) {
    if (!state) { return; }
    state->confirmed = true;
}
