/**
 * @file cmd_load_skeleton.c
 * @brief Load skeleton command — loads an .fskel file into the skeleton registry.
 *
 * JSON args: {"path":"humanoid.fskel"}
 * Response result: {"joints":N, "path":"humanoid.fskel"}
 *
 * Non-static functions: cmd_load_skeleton (1).
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_skeleton_registry.h"

#include <stdio.h>
#include <string.h>

bool cmd_load_skeleton(edit_dispatch_t *d, const json_value_t *args,
                        json_value_t *result, json_arena_t *arena) {
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->skeleton_registry) return false;

    /* Extract path argument. */
    const json_value_t *path_val = json_object_get(args, "path");
    if (!path_val || path_val->type != JSON_STRING ||
        path_val->string.len == 0) {
        return false;
    }

    char path[256];
    uint32_t plen = path_val->string.len;
    if (plen >= sizeof(path)) plen = sizeof(path) - 1;
    memcpy(path, path_val->string.ptr, plen);
    path[plen] = '\0';

    /* Build full path: asset_dir/path.
     * Use the asset_dir from the context if available, else "asset_src". */
    const char *asset_dir = ctx->asset_dir ? ctx->asset_dir : "asset_src";
    char full_path[512];
    int n = snprintf(full_path, sizeof(full_path), "%s/%s", asset_dir, path);
    if (n < 0 || (size_t)n >= sizeof(full_path)) return false;

    /* Load via registry (handles fskel_load + storage). */
    if (!edit_skeleton_registry_load(ctx->skeleton_registry, full_path)) {
        return false;
    }

    /* Look up the loaded skeleton to return joint count. */
    /* Extract filename for lookup. */
    const char *filename = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') filename = p + 1;
    }

    const edit_skeleton_entry_t *entry =
        edit_skeleton_registry_get(ctx->skeleton_registry, filename);
    if (!entry) return false;

    /* Build result: {"joints":N, "path":"name.fskel"} */
    size_t needed = 2 * sizeof(const char *)
                  + 2 * sizeof(uint32_t)
                  + 2 * sizeof(json_value_t);
    needed = (needed + 7) & ~(size_t)7;
    if (arena->used + needed > arena->cap) return false;

    const char **keys = (const char **)(arena->buf + arena->used);
    arena->used += (2 * sizeof(const char *) + 7) & ~(size_t)7;
    uint32_t *klens = (uint32_t *)(arena->buf + arena->used);
    arena->used += (2 * sizeof(uint32_t) + 7) & ~(size_t)7;
    json_value_t *vals = (json_value_t *)(arena->buf + arena->used);
    arena->used += (2 * sizeof(json_value_t) + 7) & ~(size_t)7;

    keys[0] = "joints";  klens[0] = 6;
    keys[1] = "path";    klens[1] = 4;

    vals[0].type = JSON_NUMBER;
    vals[0].number = (double)entry->skel.joint_count;

    vals[1].type = JSON_STRING;
    vals[1].string.ptr = entry->path;
    vals[1].string.len = (uint32_t)strlen(entry->path);

    result->type = JSON_OBJECT;
    result->object.keys = keys;
    result->object.key_lens = klens;
    result->object.vals = vals;
    result->object.count = 2;

    return true;
}
