/**
 * @file edit_skeleton_registry_load.c
 * @brief Skeleton registry disk loading via fskel_load().
 *
 * Non-static functions (1 / 4 limit):
 *   edit_skeleton_registry_load
 */

#include "ferrum/editor/edit_skeleton_registry.h"
#include "ferrum/animation/fskel_loader.h"

#include <stdlib.h>
#include <string.h>

/**
 * @brief Extract filename from a full path.
 *
 * Returns a pointer into the input string past the last '/' or '\\'
 * separator, or the start of the string if no separator is found.
 */
static const char *basename_(const char *path) {
    const char *last = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') last = p + 1;
    }
    return last;
}

bool edit_skeleton_registry_load(edit_skeleton_registry_t *reg,
                                  const char *full_path) {
    if (!reg || !full_path || full_path[0] == '\0') return false;

    /* Load skeleton from disk. */
    skeleton_def_t skel;
    memset(&skel, 0, sizeof(skel));
    mat4_t *ibms = NULL;
    uint32_t ibm_count = 0;

    if (!fskel_load(full_path, &skel, &ibms, &ibm_count)) {
        return false;
    }

    /* Use filename (without directory) as the registry key. */
    const char *name = basename_(full_path);

    /* Registry takes ownership of skel and ibms. On success, skel is
     * zeroed and ibms pointer is consumed — do NOT free them. */
    uint32_t idx = edit_skeleton_registry_add(reg, name, &skel,
                                               ibms, ibm_count);
    if (idx == UINT32_MAX) {
        /* Add failed (registry full) — clean up. */
        skeleton_def_destroy(&skel);
        free(ibms);
        return false;
    }

    return true;
}
