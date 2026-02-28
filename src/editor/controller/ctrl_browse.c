/**
 * @file ctrl_browse.c
 * @brief Browse result cache — stores numbered asset references.
 *
 * Non-static functions: 4 (init, clear, set, expand).
 */

#include "ferrum/editor/ctrl_browse.h"

#include <string.h>
#include <stdlib.h>

void ctrl_browse_init(ctrl_browse_t *browse) {
    if (!browse) return;
    memset(browse, 0, sizeof(*browse));
}

void ctrl_browse_clear(ctrl_browse_t *browse) {
    if (!browse) return;
    browse->count = 0;
}

void ctrl_browse_set(ctrl_browse_t *browse, const char *const *paths,
                     uint32_t count) {
    if (!browse) return;
    if (!paths) { browse->count = 0; return; }

    uint32_t n = count;
    if (n > CTRL_BROWSE_MAX_RESULTS) n = CTRL_BROWSE_MAX_RESULTS;

    for (uint32_t i = 0; i < n; i++) {
        if (paths[i]) {
            size_t len = strlen(paths[i]);
            if (len >= CTRL_BROWSE_PATH_MAX) len = CTRL_BROWSE_PATH_MAX - 1;
            memcpy(browse->paths[i], paths[i], len);
            browse->paths[i][len] = '\0';
        } else {
            browse->paths[i][0] = '\0';
        }
    }
    browse->count = n;
}

const char *ctrl_browse_expand(const ctrl_browse_t *browse, const char *token) {
    if (!browse || !token) return NULL;
    if (token[0] != '#') return NULL;
    if (token[1] == '\0') return NULL;

    /* Parse 1-based index after '#'. */
    char *end = NULL;
    long idx = strtol(token + 1, &end, 10);
    if (end == token + 1 || *end != '\0') return NULL;
    if (idx < 1 || (uint32_t)idx > browse->count) return NULL;

    return browse->paths[idx - 1];
}
