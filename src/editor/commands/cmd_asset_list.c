/**
 * @file cmd_asset_list.c
 * @brief List assets from the registry with optional prefix and type filter.
 *
 * JSON args: {"prefix": "meshes/", "type": "mesh"} (all optional)
 * Returns array of asset info strings: "path (size, type)".
 *
 * Non-static functions: 1 (cmd_asset_list).
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_asset_registry.h"

#include <stdio.h>
#include <string.h>

/** Map type name string to enum. */
static edit_asset_type_t type_from_string_(const char *s) {
    if (!s) return EDIT_ASSET_ANY;
    if (strcmp(s, "mesh") == 0)     return EDIT_ASSET_MESH;
    if (strcmp(s, "texture") == 0)  return EDIT_ASSET_TEXTURE;
    if (strcmp(s, "material") == 0) return EDIT_ASSET_MATERIAL;
    if (strcmp(s, "prefab") == 0)   return EDIT_ASSET_PREFAB;
    if (strcmp(s, "script") == 0)   return EDIT_ASSET_SCRIPT;
    return EDIT_ASSET_ANY;
}

/** Map enum to short type label. */
static const char *type_label_(edit_asset_type_t t) {
    switch (t) {
        case EDIT_ASSET_MESH:     return "mesh";
        case EDIT_ASSET_TEXTURE:  return "tex";
        case EDIT_ASSET_MATERIAL: return "mat";
        case EDIT_ASSET_PREFAB:   return "prefab";
        case EDIT_ASSET_SCRIPT:   return "script";
        default:                  return "?";
    }
}

bool cmd_asset_list(edit_dispatch_t *d, const json_value_t *args,
                    json_value_t *result, json_arena_t *arena) {
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->asset_registry) return false;

    /* Extract optional prefix. */
    const char *prefix = "";
    char prefix_buf[EDIT_ASSET_PATH_MAX];
    const json_value_t *pv = args ? json_object_get(args, "prefix") : NULL;
    if (pv && pv->type == JSON_STRING && pv->string.len > 0) {
        uint32_t n = pv->string.len;
        if (n >= sizeof(prefix_buf)) n = sizeof(prefix_buf) - 1;
        memcpy(prefix_buf, pv->string.ptr, n);
        prefix_buf[n] = '\0';
        prefix = prefix_buf;
    }

    /* Extract optional type filter. */
    edit_asset_type_t type_filter = EDIT_ASSET_ANY;
    char type_buf[32];
    const json_value_t *tv = args ? json_object_get(args, "type") : NULL;
    if (tv && tv->type == JSON_STRING && tv->string.len > 0) {
        uint32_t n = tv->string.len;
        if (n >= sizeof(type_buf)) n = sizeof(type_buf) - 1;
        memcpy(type_buf, tv->string.ptr, n);
        type_buf[n] = '\0';
        type_filter = type_from_string_(type_buf);
    }

    /* Query registry. */
    const edit_asset_entry_t *entries[256];
    uint32_t count = edit_asset_registry_list(ctx->asset_registry,
                                               prefix, type_filter,
                                               entries, 256);

    /* Build result array of info strings. */
    size_t items_sz = count * sizeof(json_value_t);
    if (arena->used + items_sz > arena->cap) {
        /* Truncate to fit. */
        count = (uint32_t)((arena->cap - arena->used) / sizeof(json_value_t));
        items_sz = count * sizeof(json_value_t);
    }
    if (count == 0) {
        result->type = JSON_ARRAY;
        result->array.items = NULL;
        result->array.count = 0;
        return true;
    }

    json_value_t *elems = (json_value_t *)(arena->buf + arena->used);
    arena->used += items_sz;

    uint32_t written = 0;
    for (uint32_t i = 0; i < count; i++) {
        char buf[384];
        int n = snprintf(buf, sizeof(buf), "%s (%u, %s)",
                         entries[i]->path, entries[i]->size,
                         type_label_(entries[i]->type));
        if (n <= 0) continue;

        size_t str_sz = (size_t)n + 1;
        if (arena->used + str_sz > arena->cap) break;
        char *str = (char *)(arena->buf + arena->used);
        arena->used += str_sz;
        memcpy(str, buf, str_sz);

        elems[written].type = JSON_STRING;
        elems[written].string.ptr = str;
        elems[written].string.len = (uint32_t)n;
        written++;
    }

    result->type = JSON_ARRAY;
    result->array.items = elems;
    result->array.count = written;
    return true;
}
