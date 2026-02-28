/**
 * @file cmd_browse.c
 * @brief Server-side browse command — list directory contents from registry.
 *
 * JSON args: {"prefix":"meshes/", "filter":"wall", "sort":"name"}
 * Returns array of asset path strings.
 *
 * Non-static functions: 1 (cmd_browse).
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_asset_registry.h"

#include <string.h>

bool cmd_browse(edit_dispatch_t *d, const json_value_t *args,
                json_value_t *result, json_arena_t *arena) {
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->asset_registry) return false;

    /* Extract optional prefix. */
    char prefix[EDIT_ASSET_PATH_MAX] = "";
    const json_value_t *pv = args ? json_object_get(args, "prefix") : NULL;
    if (pv && pv->type == JSON_STRING && pv->string.len > 0) {
        uint32_t n = pv->string.len;
        if (n >= sizeof(prefix)) n = sizeof(prefix) - 1;
        memcpy(prefix, pv->string.ptr, n);
        prefix[n] = '\0';
    }

    /* Extract optional filter (substring match). */
    char filter[128] = "";
    const json_value_t *fv = args ? json_object_get(args, "filter") : NULL;
    if (fv && fv->type == JSON_STRING && fv->string.len > 0) {
        uint32_t n = fv->string.len;
        if (n >= sizeof(filter)) n = sizeof(filter) - 1;
        memcpy(filter, fv->string.ptr, n);
        filter[n] = '\0';
    }

    /* Query registry by prefix. */
    const edit_asset_entry_t *entries[256];
    uint32_t count = edit_asset_registry_list(ctx->asset_registry,
                                               prefix, EDIT_ASSET_ANY,
                                               entries, 256);

    /* Apply filter if set (substring match on path). */
    const edit_asset_entry_t *filtered[256];
    uint32_t filt_count = 0;
    for (uint32_t i = 0; i < count && filt_count < 256; i++) {
        if (filter[0] == '\0' || strstr(entries[i]->path, filter) != NULL) {
            filtered[filt_count++] = entries[i];
        }
    }

    /* Cap to 100 results. */
    if (filt_count > 100) filt_count = 100;

    /* Build result array of path strings. */
    size_t items_sz = filt_count * sizeof(json_value_t);
    if (arena->used + items_sz > arena->cap) {
        filt_count = (uint32_t)((arena->cap - arena->used) /
                                sizeof(json_value_t));
        items_sz = filt_count * sizeof(json_value_t);
    }
    if (filt_count == 0) {
        result->type = JSON_ARRAY;
        result->array.items = NULL;
        result->array.count = 0;
        return true;
    }

    json_value_t *elems = (json_value_t *)(arena->buf + arena->used);
    arena->used += items_sz;

    uint32_t written = 0;
    for (uint32_t i = 0; i < filt_count; i++) {
        size_t len = strlen(filtered[i]->path);
        size_t str_sz = len + 1;
        if (arena->used + str_sz > arena->cap) break;
        char *str = (char *)(arena->buf + arena->used);
        arena->used += str_sz;
        memcpy(str, filtered[i]->path, str_sz);

        elems[written].type = JSON_STRING;
        elems[written].string.ptr = str;
        elems[written].string.len = (uint32_t)len;
        written++;
    }

    result->type = JSON_ARRAY;
    result->array.items = elems;
    result->array.count = written;
    return true;
}
