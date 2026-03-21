/**
 * @file cmd_skeleton_save.c
 * @brief Save skeleton to .fskel file.
 *
 * JSON args: {"path":"<filename.fskel>"} (optional — uses default path if omitted)
 *
 * Non-static functions (1 / 4 limit):
 *   cmd_skeleton_save
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_skeleton_registry.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/animation/fskel_loader.h"

#include <stdio.h>
#include <string.h>

bool cmd_skeleton_save(edit_dispatch_t *d, const json_value_t *args,
                        json_value_t *result, json_arena_t *arena) {
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->skeleton_registry) return false;

    /* Get path from args or use default. */
    const char *path = NULL;
    if (args) {
        const json_value_t *path_val = json_object_get(args, "path");
        if (path_val && path_val->type == JSON_STRING &&
            path_val->string.len > 0) {
            /* Use a static buffer since the string isn't null-terminated. */
            static char s_path[512];
            uint32_t len = path_val->string.len;
            if (len >= sizeof(s_path)) len = sizeof(s_path) - 1;
            memcpy(s_path, path_val->string.ptr, len);
            s_path[len] = '\0';
            path = s_path;
        }
    }

    if (!path) {
        result->type    = JSON_BOOL;
        result->boolean = false;
        return false;
    }

    /* Extract filename for registry lookup. */
    const char *fname = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/') fname = p + 1;
    }

    const edit_skeleton_entry_t *se =
        edit_skeleton_registry_get(ctx->skeleton_registry, fname);
    if (!se) return false;

    /* Build full path with asset dir if not absolute. */
    char full_path[512];
    if (path[0] == '/') {
        strncpy(full_path, path, sizeof(full_path) - 1);
        full_path[sizeof(full_path) - 1] = '\0';
    } else {
        snprintf(full_path, sizeof(full_path), "%s/%s",
                 ctx->asset_dir, path);
    }

    if (!fskel_write(full_path, &se->skel, se->ibms, se->ibm_count)) {
        return false;
    }

    result->type    = JSON_BOOL;
    result->boolean = true;
    return true;
}
