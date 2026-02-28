/**
 * @file cmd_asset_search.c
 * @brief Search assets by regex, and provide path completions.
 *
 * Non-static functions: 2 (cmd_asset_search, cmd_asset_complete).
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_asset_registry.h"

#include <stdio.h>
#include <string.h>

bool cmd_asset_search(edit_dispatch_t *d, const json_value_t *args,
                      json_value_t *result, json_arena_t *arena) {
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->asset_registry) return false;

    /* Extract required pattern. */
    const json_value_t *pv = args ? json_object_get(args, "pattern") : NULL;
    if (!pv || pv->type != JSON_STRING || pv->string.len == 0) return false;

    char pattern[256];
    uint32_t n = pv->string.len;
    if (n >= sizeof(pattern)) n = sizeof(pattern) - 1;
    memcpy(pattern, pv->string.ptr, n);
    pattern[n] = '\0';

    /* Search registry. */
    const edit_asset_entry_t *entries[256];
    uint32_t count = edit_asset_registry_search(ctx->asset_registry,
                                                 pattern, EDIT_ASSET_ANY,
                                                 entries, 256);

    /* Build result array of path strings. */
    json_value_t *elems = NULL;
    if (count > 0) {
        size_t items_sz = count * sizeof(json_value_t);
        if (arena->used + items_sz > arena->cap) {
            count = (uint32_t)((arena->cap - arena->used) / sizeof(json_value_t));
            items_sz = count * sizeof(json_value_t);
        }
        elems = (json_value_t *)(arena->buf + arena->used);
        arena->used += items_sz;
    }

    uint32_t written = 0;
    for (uint32_t i = 0; i < count; i++) {
        size_t len = strlen(entries[i]->path);
        size_t str_sz = len + 1;
        if (arena->used + str_sz > arena->cap) break;
        char *str = (char *)(arena->buf + arena->used);
        arena->used += str_sz;
        memcpy(str, entries[i]->path, str_sz);

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

bool cmd_asset_complete(edit_dispatch_t *d, const json_value_t *args,
                        json_value_t *result, json_arena_t *arena) {
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->asset_registry) return false;

    /* Extract prefix. */
    const json_value_t *pv = args ? json_object_get(args, "prefix") : NULL;
    char prefix[EDIT_ASSET_PATH_MAX] = "";
    if (pv && pv->type == JSON_STRING && pv->string.len > 0) {
        uint32_t n = pv->string.len;
        if (n >= sizeof(prefix)) n = sizeof(prefix) - 1;
        memcpy(prefix, pv->string.ptr, n);
        prefix[n] = '\0';
    }

    /* Get completions. */
    const edit_asset_entry_t *entries[256];
    uint32_t count = edit_asset_registry_complete(ctx->asset_registry,
                                                   prefix, entries, 256);

    /* Build result array. */
    json_value_t *elems = NULL;
    if (count > 0) {
        size_t items_sz = count * sizeof(json_value_t);
        if (arena->used + items_sz > arena->cap) {
            count = (uint32_t)((arena->cap - arena->used) / sizeof(json_value_t));
            items_sz = count * sizeof(json_value_t);
        }
        elems = (json_value_t *)(arena->buf + arena->used);
        arena->used += items_sz;
    }

    uint32_t written = 0;
    for (uint32_t i = 0; i < count; i++) {
        size_t len = strlen(entries[i]->path);
        size_t str_sz = len + 1;
        if (arena->used + str_sz > arena->cap) break;
        char *str = (char *)(arena->buf + arena->used);
        arena->used += str_sz;
        memcpy(str, entries[i]->path, str_sz);

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
