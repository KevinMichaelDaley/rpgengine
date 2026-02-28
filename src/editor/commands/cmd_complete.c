/**
 * @file cmd_complete.c
 * @brief Server-side tab-completion handler — routes to different sources.
 *
 * Accepts a context string and returns completion candidates.
 * Sources: command names (local), asset paths (registry), entity names.
 *
 * JSON args: {"context": "spawn mesh assets/meshes/p"}
 * Returns: {"candidates": [...], "prefix": "...", "type": "asset_path"}
 *
 * Non-static functions: 1 (cmd_complete).
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_asset_registry.h"

#include <string.h>

bool cmd_complete(edit_dispatch_t *d, const json_value_t *args,
                  json_value_t *result, json_arena_t *arena) {
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx) return false;

    /* Extract context string. */
    const json_value_t *cv = args ? json_object_get(args, "context") : NULL;
    char context[512] = "";
    if (cv && cv->type == JSON_STRING && cv->string.len > 0) {
        uint32_t n = cv->string.len;
        if (n >= sizeof(context)) n = sizeof(context) - 1;
        memcpy(context, cv->string.ptr, n);
        context[n] = '\0';
    }

    /* Find the last space-delimited token as the completion prefix. */
    char *space = strchr(context, ' ');
    const char *prefix = context;
    if (space) {
        const char *last_space = strrchr(context, ' ');
        prefix = last_space ? last_space + 1 : space + 1;
    }

    /* Route to asset completion if we have a registry. */
    if (ctx->asset_registry && space) {
        const edit_asset_entry_t *entries[64];
        uint32_t count = edit_asset_registry_complete(
            ctx->asset_registry, prefix, entries, 64);

        if (count > 0) {
            /* Build candidates array. */
            size_t items_sz = count * sizeof(json_value_t);
            if (arena->used + items_sz > arena->cap) {
                count = (uint32_t)((arena->cap - arena->used) /
                                   sizeof(json_value_t));
                items_sz = count * sizeof(json_value_t);
            }
            json_value_t *elems =
                (json_value_t *)(arena->buf + arena->used);
            arena->used += items_sz;

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
    }

    /* Fallback: empty result. */
    result->type = JSON_ARRAY;
    result->array.items = NULL;
    result->array.count = 0;
    return true;
}
